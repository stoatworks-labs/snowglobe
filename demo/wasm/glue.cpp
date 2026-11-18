/*
    The page's way in to the plugin: what an FFGL host does to a plugin
    instance, as C functions JavaScript can call. (woofer's glue, which is
    mirrorball's and fountain's, with snowglobe's names, its beat and its
    harness's track.)

    Nothing here is the plugin, and nothing here does the plugin's work. The
    plugin is `source/Snowglobe.cpp` and everything it calls -- the flakes and
    the swirl (Globe.cpp), the fill (Fill.cpp), the projection through the
    glass (Optics.cpp), the audio input and earshot under it -- compiled
    UNMODIFIED into the same WebAssembly module as this file (see
    demo/tools/build-wasm.sh for the list). This file only plays the host:

      - it constructs a `SnowglobePlugin`, as EffectPlugin.cpp's registration
        would, and calls InitGL / DeInitGL on it;
      - it reads the parameter declarations back through the FFGL SDK's own
        host-facing getters (CFFGLPluginManager::GetParamName and the rest,
        the SDK compiled unmodified too), so the page's panel is built from
        the plugin's constructor rather than from a copy of it;
      - it forwards the host's calls: SetFloatParameter, SetTime,
        SetBeatInfo and ProcessOpenGL with one input texture, as
        `tools/sgtest`'s Rig does.

    One thing here is the plugin's HARNESS rather than a host: the audio.
    sgtest never opens a device; it hands the plugin its track through
    earshot's feed() (`AudioInput::Feed`, reached by `AudioForTest()`, which
    is `Engine::feedBegin` and `Engine::feed`), the path a device's samples
    take inside earshot. So does this page. `musicAt` is sgtest's own track,
    copied from tools/sgtest/main.cpp character for character, and so are the
    lines of the Rig's Render that feed it (both held to sgtest by
    demo/tools/check_shaders.py). A file the visitor chooses is decoded by the
    page and reaches feed() through `sg_feed`, as `sgtest --wav` does.

    No audio device exists in this module: earshot is built with
    EARSHOT_CAPTURE=OFF (NoCapture.cpp; miniaudio is not compiled), and
    `sg_prepare` sets SNOWGLOBE_NO_DEVICES=1, verify.sh's switch, so the
    plugin's AudioInput never lists, looks up or opens one. Nothing calls
    selectDevice with a name.

    GL calls go to the page's WebGL2 context through emscripten's GL library;
    demo/wasm/gl_shim.cpp replaces the four entry points that cannot be passed
    straight through.
*/
#include "Controls.h"
#include "Diag.h"
#include "Fill.h"
#include "Globe.h"
#include "Snowglobe.h"

#include <emscripten/emscripten.h>

#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace snowglobe;

namespace
{
//---------------------------------------------------------------------------
// tools/sgtest/main.cpp's track, verbatim (check_shaders.py compares).
//---------------------------------------------------------------------------
constexpr int kRate = 48000;

double musicAt( double t )
{
	if( t < 0.0 )
		return 0.0;
	const double beat = std::fmod( t, 0.5 );
	double s          = 0.0;
	s += 0.6 * std::exp( -beat / 0.06 ) * std::sin( 2.0 * kPi * ( 55.0 + 60.0 * std::exp( -beat / 0.02 ) ) * beat );
	const double off = std::fmod( t + 0.25, 0.5 );
	//A hat: a burst of deterministic noise, 30 ms, differenced (a first-order
	//high-pass, -33 dB at 160 Hz) so it puts nothing in the kick's band.
	const uint32_t n = static_cast< uint32_t >( t * kRate );
	s += 0.25 * std::exp( -off / 0.01 ) * ( Hash01( n, 7u, 0u ) - Hash01( n - 1u, 7u, 0u ) );
	return s;
}

//---------------------------------------------------------------------------
// One plugin instance and the host-side structs it is handed.
//---------------------------------------------------------------------------
struct Instance
{
	SnowglobePlugin plugin;
	bool initialised = false;

	FFGLTextureStruct input {};
	FFGLTextureStruct* inputs[ 1 ] = { &input };
	ProcessOpenGLStruct process {};

	/// The feed's clock, as sgtest's Rig keeps it: samples up to
	/// llround( fedUntil * 48000 ) have gone to feed().
	double fedUntil = 0.0;
	uint64_t sent   = 0;

	std::string scratch;///< a string handed back to JS lives here until the next call
};

float asFloat( FFMixed mixed )
{
	float value = 0.0f;
	static_assert( sizeof( value ) == sizeof( mixed.UIntValue ), "FFMixed carries a float in its bits" );
	std::memcpy( &value, &mixed.UIntValue, sizeof( value ) );
	return value;
}

const char* hold( Instance* instance, std::string text )
{
	instance->scratch = std::move( text );
	return instance->scratch.c_str();
}

Shape shapeOf( float snow )
{
	return static_cast< Shape >( OptionIndex( snow, static_cast< int >( Shape::Count ) ) );
}
} // namespace

extern "C"
{
//---------------------------------------------------------------------------
// Lifetime.
//---------------------------------------------------------------------------

/// Before the first instance: give the plugin's log (Diag.cpp) somewhere to
/// go, a directory in the page's in-memory file system named through the
/// override Diag.cpp reads (its own `mkdir -p` goes through system(), which a
/// browser does not have); and SNOWGLOBE_NO_DEVICES=1, verify.sh's switch,
/// so AudioInput never lists, looks up or opens a device (a build with no
/// capture backend would find none anyway).
EMSCRIPTEN_KEEPALIVE void sg_prepare()
{
	setenv( "SNOWGLOBE_LOG_DIR", "/snowglobe/logs", 1 );
	setenv( "SNOWGLOBE_NO_DEVICES", "1", 1 );
	mkdir( "/snowglobe", 0755 );
	mkdir( "/snowglobe/logs", 0755 );
}

EMSCRIPTEN_KEEPALIVE Instance* sg_new()
{
	return new Instance();
}

EMSCRIPTEN_KEEPALIVE void sg_delete( Instance* instance )
{
	if( instance == nullptr )
		return;
	if( instance->initialised )
		instance->plugin.DeInitGL();
	delete instance;
}

/// InitGL, with the viewport a host would pass. 1 on success.
EMSCRIPTEN_KEEPALIVE int sg_init_gl( Instance* instance, int width, int height )
{
	FFGLViewportStruct viewport {};
	viewport.width        = static_cast< GLuint >( width );
	viewport.height       = static_cast< GLuint >( height );
	instance->initialised = instance->plugin.InitGL( &viewport ) == FF_SUCCESS;
	return instance->initialised ? 1 : 0;
}

//---------------------------------------------------------------------------
// The declarations, read as a host reads them.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int sg_param_count( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetNumParams() );
}

EMSCRIPTEN_KEEPALIVE const char* sg_param_name( Instance* instance, int index )
{
	const char* name = instance->plugin.GetParamName( static_cast< unsigned int >( index ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE int sg_param_type( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamType( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* sg_param_group( Instance* instance, int index )
{
	return hold( instance, instance->plugin.GetParamGroup( static_cast< unsigned int >( index ) ) );
}

/// The declared default of a numeric parameter (FF_TYPE_STANDARD already
/// clamped into 0..1 by the SDK, as a host sees it).
EMSCRIPTEN_KEEPALIVE float sg_param_default( Instance* instance, int index )
{
	return asFloat( instance->plugin.GetParamDefault( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE int sg_param_element_count( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetNumParamElements( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* sg_param_element_name( Instance* instance, int index, int element )
{
	const char* name = instance->plugin.GetParamElementName( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE float sg_param_element_value( Instance* instance, int index, int element )
{
	return asFloat( instance->plugin.GetParamElementDefault( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) ) );
}

EMSCRIPTEN_KEEPALIVE int sg_param_usage( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamUsage( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE int sg_min_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMinInputs() );
}

EMSCRIPTEN_KEEPALIVE int sg_max_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMaxInputs() );
}

//---------------------------------------------------------------------------
// The host's calls.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int sg_set_float( Instance* instance, int index, float value )
{
	return instance->plugin.SetFloatParameter( static_cast< unsigned int >( index ), value ) == FF_SUCCESS ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE float sg_get_float( Instance* instance, int index )
{
	return instance->plugin.GetFloatParameter( static_cast< unsigned int >( index ) );
}

/// The host's beat, as Resolume sends it every frame and `sgtest --beat`
/// does: the BPM and the bar's phase, 0..1.
EMSCRIPTEN_KEEPALIVE void sg_beat( Instance* instance, float bpm, float barPhase )
{
	instance->plugin.SetBeatInfo( bpm, barPhase );
}

/// One frame: SetTime, then ProcessOpenGL into whatever the page has bound,
/// with one input of clipWidth x clipHeight whose GL name is `clip` (a texture
/// the page registered with emscripten's GL tables). 1 on success.
EMSCRIPTEN_KEEPALIVE int sg_process( Instance* instance, double seconds, int clip, int clipWidth, int clipHeight )
{
	instance->plugin.SetTime( seconds );
	instance->input.Width = instance->input.HardwareWidth = static_cast< FFUInt32 >( clipWidth );
	instance->input.Height = instance->input.HardwareHeight = static_cast< FFUInt32 >( clipHeight );
	instance->input.Handle                                 = static_cast< GLuint >( clip );
	instance->process.numInputTextures                     = 1;
	instance->process.inputTextures                        = instance->inputs;
	instance->process.HostFBO                              = 0;
	return instance->plugin.ProcessOpenGL( &instance->process ) == FF_SUCCESS ? 1 : 0;
}

//---------------------------------------------------------------------------
// The audio, as sgtest hands it over. Each source is a fresh instance (the
// page's rule): a feed, once begun, is what earshot's Engine reads until a
// selectDevice, and nothing here selects.
//---------------------------------------------------------------------------

/// sgtest's track up to the page's time `t`: the Rig's Render, line for line
/// (check_shaders.py holds these lines to it), with the track as its audio,
/// at 48 kHz, the same sample on both channels. sgtest's t is frame / fps.
/// Returns the samples fed.
EMSCRIPTEN_KEEPALIVE int sg_feed_track( Instance* instance, double t )
{
	SnowglobePlugin& plugin       = instance->plugin;
	double& fedUntil              = instance->fedUntil;
	const int audioRate           = kRate;
	double ( *const audio )( double ) = musicAt;
	const int before = static_cast< int >( std::llround( fedUntil * audioRate ) );
	const int after  = static_cast< int >( std::llround( t * audioRate ) );
	// From here to `fedUntil = t;`, sgtest's Rig, line for line (check_shaders.py).
	const int from = static_cast< int >( std::llround( fedUntil * audioRate ) );
	const int to   = static_cast< int >( std::llround( t * audioRate ) );
	if( to > from )
	{
		std::vector< float > pcm( static_cast< size_t >( to - from ) * 2 );
		for( int k = from; k < to; ++k )
			pcm[ static_cast< size_t >( k - from ) * 2 ] = pcm[ static_cast< size_t >( k - from ) * 2 + 1 ] =
				static_cast< float >( audio( static_cast< double >( k ) / audioRate ) );
		plugin.AudioForTest().Feed( pcm.data(), to - from, audioRate, 2 );
	}
	fedUntil = t;
	const int fed = after > before ? after - before : 0;
	instance->sent += static_cast< uint64_t >( fed );
	return fed;
}

/// A file the visitor chose, decoded by the page: `frames` interleaved frames
/// of `channels` at `rate`, into feed() as `sgtest --wav` hands a WAV over.
EMSCRIPTEN_KEEPALIVE void sg_feed( Instance* instance, const float* interleaved, int frames, int rate, int channels )
{
	instance->plugin.AudioForTest().Feed( interleaved, frames, rate, channels );
	instance->sent += static_cast< uint64_t >( std::max( frames, 0 ) );
}

/// Samples fed so far (either path).
EMSCRIPTEN_KEEPALIVE double sg_fed( Instance* instance )
{
	return static_cast< double >( instance->sent );
}

//---------------------------------------------------------------------------
// For the panel's read-outs: Controls.cpp's conversion for a parameter, the
// one Snowglobe.cpp's CurrentPhysics, gatherEvents and ProcessOpenGL apply
// to it. Size's depends on Snow (flakes or beads), so the Snow value comes
// too. NaN where the plugin uses the host's 0..1 as it is (Mix), or an index.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE double sg_convert( Instance* instance, int index, float v, float snow )
{
	(void)instance;
	switch( static_cast< unsigned int >( index ) )
	{
	case PT_SHAKE: return ShakeFromParam( v );
	case PT_FILL: return FillFromParam( v );
	case PT_SIZE: return SizeFromParam( v, shapeOf( snow ) );
	case PT_RANGE: return RangeFromParam( v );
	case PT_COUNT_FLAKES: return static_cast< double >( CountFromParam( v ) );
	case PT_GLOBE: return GlobeFromParam( v );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

/// And back: the host's 0..1 for a value in the plugin's units, by the
/// inverse Controls.cpp pairs with each conversion (the one its constructor
/// sets the defaults with). NaN for a parameter with no inverse.
EMSCRIPTEN_KEEPALIVE double sg_param_from( Instance* instance, int index, double value, float snow )
{
	(void)instance;
	switch( static_cast< unsigned int >( index ) )
	{
	case PT_FILL: return ParamFromFill( value );
	case PT_SIZE: return ParamFromSize( value, shapeOf( snow ) );
	case PT_RANGE: return ParamFromRange( value );
	case PT_COUNT_FLAKES: return ParamFromCount( static_cast< int >( std::lround( value ) ) );
	case PT_GLOBE: return ParamFromGlobe( value );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

/// The fill at glycerol fraction w (Fill.cpp's FillAt, the fits the plugin
/// uses): which = 0 viscosity mPa s, 1 density kg/m^3, 2 n_D.
EMSCRIPTEN_KEEPALIVE double sg_fill( double w, int which )
{
	const FillProps f = FillAt( w );
	return which == 0 ? f.mu * 1000.0 : which == 1 ? f.rho : f.n;
}

//---------------------------------------------------------------------------
// What the line under the picture reports: the plugin's own state, through
// the accessors its harness reads (LastFrame(), ShakeLog(), GlobeForTest(),
// CurrentPhysics(), AudioForTest()).
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE double sg_frame( Instance* instance, int which )
{
	const FrameData& f = instance->plugin.LastFrame();
	switch( which )
	{
	case 0: return static_cast< double >( f.clockUs );
	case 1: return static_cast< double >( f.drawn );
	case 2: return static_cast< double >( f.behind );
	case 3: return static_cast< double >( f.suspended );
	case 4: return f.globePx;
	case 5: return f.index;
	case 6: return static_cast< double >( f.stateHash & 0xffffffffu );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

/// Shakes so far, the last one's instant on the plugin's clock (us, -1 for
/// none) and its strength S.
EMSCRIPTEN_KEEPALIVE double sg_shakes( Instance* instance, int which )
{
	const auto& log = instance->plugin.ShakeLog();
	if( which == 0 )
		return static_cast< double >( instance->plugin.GlobeForTest().ShakeCount() );
	if( log.empty() )
		return which == 1 ? -1.0 : 0.0;
	return which == 1 ? static_cast< double >( log.back().us ) : log.back().strength;
}

/// The swirl's spin at the centre, rad/s (|W|), and its decay time 1 / (nu k^2), s.
EMSCRIPTEN_KEEPALIVE double sg_swirl( Instance* instance, int which )
{
	Globe& globe = instance->plugin.GlobeForTest();
	if( which == 0 )
	{
		const V3 w = globe.Swirl();
		return std::sqrt( dot( w, w ) );
	}
	return 1.0 / globe.DecayRate( instance->plugin.CurrentPhysics() );
}

/// The physics the controls give now: 0 w, 1 the largest radius (m), 2 the
/// smallest (m), 3 the count, 4 viscosity (mPa s), 5 n.
EMSCRIPTEN_KEEPALIVE double sg_physics( Instance* instance, int which )
{
	const Physics p = instance->plugin.CurrentPhysics();
	switch( which )
	{
	case 0: return p.fill.w;
	case 1: return p.aMax;
	case 2: return p.aMin;
	case 3: return static_cast< double >( p.count );
	case 4: return p.fill.mu * 1000.0;
	case 5: return p.fill.n;
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

/// earshot's last frame, as the plugin's kick detector read it: 0 its source
/// (None, Device -- which feed() is -- or Host), 1 the audio seconds analysed,
/// 2 band 0's onsets (the kick band), 3 band 0's level, 4 the loudest band's
/// level.
EMSCRIPTEN_KEEPALIVE double sg_audio( Instance* instance, int which )
{
	const earshot::Frame& f = instance->plugin.AudioForTest().Frame();
	float loudest           = 0.0f;
	for( int b = 0; b < f.bandCount && b < earshot::kMaxBands; ++b )
		loudest = std::max( loudest, f.bands[ b ].level );
	switch( which )
	{
	case 0: return static_cast< double >( static_cast< int >( f.source ) );
	case 1: return f.audioTime;
	case 2: return f.bandCount > 0 ? static_cast< double >( f.bands[ kKickBand ].onsetCount ) : 0.0;
	case 3: return f.bandCount > 0 ? static_cast< double >( f.bands[ kKickBand ].level ) : 0.0;
	case 4: return static_cast< double >( loudest );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

/// The plugin's log file (Diag.cpp), in the page's in-memory file system.
EMSCRIPTEN_KEEPALIVE const char* sg_log_path()
{
	static std::string path;
	path = diag::logPath();
	return path.c_str();
}

} // extern "C"
