#include "Snowglobe.h"

#include "Diag.h"
#include "GLState.h"

//FFGLSDK.h includes every other scoped binding and omits this one (SDK
//b1afaf9), so it has to be asked for by name.
#include <ffglex/FFGLScopedFBOBinding.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <string>

using namespace ffglex;

namespace snowglobe
{
namespace
{
/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;
/// The most one frame may advance the plugin's clock (a stall, a seek).
constexpr int64_t kMaxStepUs = 100000;
/// Resolume's FFT buffer.
constexpr int kBins = 64;
/// The most beat crossings one frame places.
constexpr int kMaxBeatsPerFrame = 8;
/// Kicks closer than this on the plugin's clock are one kick.
constexpr int64_t kKickMergeUs = 50000;
/// A band-0 onset is a kick only while the band is within 12 dB of the
/// loudest band (chosen; SPEC-snowglobe.md, Triggers). A hat's splatter into
/// 40-160 Hz crossed earshot's threshold once the kicks stopped and band 0's
/// running deviation had decayed: 35 dB under the hat's own band, it fired.
constexpr float kKickDominance = 0.25f;
/// Water's n_D at 20 C (GPA Table 23's 0 % row): the kCpuWaterIndex hook.
constexpr double kWaterIndex = 1.33303;

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

/// Two unit vectors spanning the plane normal to n.
void planeOf( V3 n, V3& e1, V3& e2 )
{
	const V3 helper = std::fabs( n.y ) < 0.9 ? V3{ 0.0, 1.0, 0.0 } : V3{ 1.0, 0.0, 0.0 };
	V3 a            = cross( n, helper );
	a               = ( 1.0 / std::sqrt( dot( a, a ) ) ) * a;
	e1              = a;
	e2              = cross( n, a );
}
} // namespace

static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount, "the About run no longer matches StoatworksAbout.h" );

//---------------------------------------------------------------------------
SnowglobePlugin::SnowglobePlugin() :
	hostOrder( HostOrder() )
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );
	SetTimeSupported( true );

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		params[ id ] = InfoOf( id ).defaultValue;

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
	{
		const ControlInfo& info = InfoOf( id );
		switch( info.kind )
		{
		case Kind::Standard: SetParamInfo( id, info.name, FF_TYPE_STANDARD, params[ id ] ); break;
		case Kind::Option:
			SetOptionParamInfo( id, info.name, static_cast< unsigned int >( info.optionCount ), params[ id ] );
			for( int i = 0; i < info.optionCount; ++i )
				SetParamElementInfo( id, static_cast< unsigned int >( i ), info.options[ i ], static_cast< float >( i ) );
			break;
		case Kind::Devices:
		{
			//The device menu (marbling's shape, earshot's keys), unlisted: the
			//devices are asked for on the first frame under Kick, never here.
			SetOptionParamInfo( id, info.name, kInputSlots, 0.0f );
			const auto& slots = audio.Slots();
			for( int i = 0; i < kInputSlots; ++i )
				SetParamElementInfo( id, static_cast< unsigned int >( i ), slots[ static_cast< size_t >( i ) ].label.c_str(),
				                     slots[ static_cast< size_t >( i ) ].value );
			SetParamRange( id, 0.0f, 8388608.0f );
			break;
		}
		case Kind::Event: SetParamInfo( id, info.name, FF_TYPE_EVENT, false ); break;
		case Kind::Buffer:
			SetBufferParamInfo( id, info.name, kBins, FF_USAGE_FFT );
			for( int i = 0; i < kBins; ++i )
				SetParamElementInfo( id, static_cast< unsigned int >( i ), "", 0.0f );
			break;
		}
		SetParamGroup( id, info.group );
	}

	// The About block, last. Inline rather than through a helper: SetParamInfo
	// is protected on CFFGLPlugin, so nothing outside the class can call it.
	SetParamInfo( PT_ABOUT_TEXT, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_TEXT + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = PT_ABOUT_TEXT; i < PT_COUNT; ++i )
		SetParamGroup( i, "About" );
}

//---------------------------------------------------------------------------
GLint SnowglobePlugin::where( const FFGLShader& shader, const char* name )
{
	//Keyed by the literal's address: every name here is a string literal.
	const auto key = std::make_pair( shader.GetGLID(), name );
	for( const auto& entry : uniformCache )
		if( entry.first == key )
			return entry.second;
	const GLint location = glGetUniformLocation( shader.GetGLID(), name );
	uniformCache.emplace_back( key, location );
	return location;
}
void SnowglobePlugin::uniform( const FFGLShader& shader, const char* name, int v )
{
	glUniform1i( where( shader, name ), v );
}
void SnowglobePlugin::uniform( const FFGLShader& shader, const char* name, float v )
{
	glUniform1f( where( shader, name ), v );
}

//---------------------------------------------------------------------------
FFResult SnowglobePlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	struct Stage
	{
		FFGLShader* shader;
		shaders::Pass pass;
		const char* name;
	};
	const Stage stages[] = {
		{ &sceneShader, shaders::Pass::Scene, "scene" },
		{ &flakeShader, shaders::Pass::Flake, "flake" },
		{ &compositeShader, shaders::Pass::Composite, "composite" },
	};
	for( const Stage& stage : stages )
	{
		const std::string vertex   = shaders::AssembleVertex( stage.pass );
		const std::string fragment = shaders::AssembleFragment( stage.pass );
		if( stage.shader->Compile( vertex.c_str(), fragment.c_str() ) )
			continue;
		//Returning FF_FAIL is invisible to the operator: the plugin simply
		//does nothing in Resolume. These lines are the only record of which
		//pass it was.
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Snowglobe: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}
	//The flakes' instance buffer: two vec4 per flake, divisor 1.
	GLint savedVao = 0, savedBuffer = 0;
	glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &savedVao );
	glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &savedBuffer );
	glGenVertexArrays( 1, &flakeVao );
	glGenBuffers( 1, &flakeVbo );
	glBindVertexArray( flakeVao );
	glBindBuffer( GL_ARRAY_BUFFER, flakeVbo );
	glBufferData( GL_ARRAY_BUFFER, static_cast< GLsizeiptr >( kMaxFlakes * 8 * sizeof( float ) ), nullptr, GL_DYNAMIC_DRAW );
	for( GLuint a = 0; a < 2; ++a )
	{
		glEnableVertexAttribArray( a );
		glVertexAttribPointer( a, 4, GL_FLOAT, GL_FALSE, 8 * sizeof( float ), reinterpret_cast< const void* >( static_cast< uintptr_t >( a * 4 * sizeof( float ) ) ) );
		glVertexAttribDivisor( a, 1 );
	}
	glBindVertexArray( static_cast< GLuint >( savedVao ) );
	glBindBuffer( GL_ARRAY_BUFFER, static_cast< GLuint >( savedBuffer ) );
	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
void SnowglobePlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw = hostTime;

	//Resolume has been seen sending both seconds and milliseconds through
	//SetTime, and nothing in the call says which. Vote on it against the wall
	//clock for a few frames, then stop asking (marbling's, rosette's).
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale  = millisVotes > secondsVotes ? 0.001 : 1.0;
				settledJump = true;//wall time to the host's own: no step across it
			}
		}
	}
	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;

	double seconds = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
	if( cpuHooks & kCpuFloatClock )
		seconds = static_cast< double >( static_cast< float >( seconds ) );
	nowUs = std::llround( seconds * 1e6 );
	if( ++clockFrames == 60 )
		diag::info( "host clock at frame 60: raw=" + std::to_string( raw ) + " scale=" + std::to_string( clockScale )
		            + " seconds=" + std::to_string( seconds ) );
}

void SnowglobePlugin::SetSampleRate( unsigned int rate )
{
	CFFGLPlugin::SetSampleRate( rate );
	hostRate.store( rate, std::memory_order_relaxed );
}

void SnowglobePlugin::SetBeatInfo( float newBpm, float newBarPhase )
{
	CFFGLPlugin::SetBeatInfo( newBpm, newBarPhase );
	if( newBpm > 0.0f && std::isfinite( newBpm ) )
		bpm = newBpm;
	if( std::isfinite( newBarPhase ) )
		barPhase = newBarPhase - std::floor( newBarPhase );
}

Physics SnowglobePlugin::CurrentPhysics() const
{
	Physics p;
	p.fill  = FillAt( FillFromParam( params[ PT_FILL ] ) );
	p.shape = static_cast< Shape >( OptionIndex( params[ PT_SNOW ], static_cast< int >( Shape::Count ) ) );
	p.aMax  = SizeFromParam( params[ PT_SIZE ], p.shape );
	p.aMin  = p.aMax * RangeFromParam( params[ PT_RANGE ] );
	p.count = countOverride >= 0 ? std::min( countOverride, kMaxFlakes ) : CountFromParam( params[ PT_COUNT_FLAKES ] );
	return p;
}

void SnowglobePlugin::refreshMenu()
{
	const auto& slots = audio.Slots();
	for( int i = 0; i < kInputSlots; ++i )
		SetParamElementInfo( PT_INPUT, static_cast< unsigned int >( i ), slots[ static_cast< size_t >( i ) ].label.c_str(),
		                     slots[ static_cast< size_t >( i ) ].value );
	RaiseParamEvent( PT_INPUT, FF_EVENT_FLAG_ELEMENTS );
}

//---------------------------------------------------------------------------
// Kicks: earshot's onsets in the lowest band (40-160 Hz), each placed inside
// the frame on the audio clock (SPEC-snowglobe.md, Triggers).
//---------------------------------------------------------------------------
void SnowglobePlugin::handleKicks( int64_t frameStartUs, int64_t frameUs, std::vector< ShakeEvent >& events )
{
	if( audio.EnsureListed() )
		refreshMenu();
	audio.Want( params[ PT_INPUT ] );
	audio.Apply();
	//Resolume's 64 bins are linear to half the host's audio engine rate, 375 Hz
	//each at 48 kHz, and earshot shares each between the bands it overlaps
	//(earshot 0.1.1). Until the host sends a rate, earshot's 48 kHz.
	if( !( cpuHooks & kCpuIgnoreHostRate ) )
		audio.SetHostRate( hostRate.load( std::memory_order_relaxed ) );
	audio.SetHostCentreOnlyForTest( ( cpuHooks & kCpuHostCentreOnly ) != 0 );
	float bins[ kBins ] = {};
	int binCount        = 0;
	if( const ParamInfo* info = FindParamInfo( PT_AUDIO ) )
	{
		binCount = static_cast< int >( std::min< size_t >( info->elements.size(), kBins ) );
		for( int i = 0; i < binCount; ++i )
			bins[ i ] = info->elements[ static_cast< size_t >( i ) ].value;
	}
	const earshot::Frame& f = audio.Update( static_cast< double >( frameUs ) * 1e-6, bins, binCount );
	float loudest = 0.0f;
	for( int b = 0; b < f.bandCount && b < earshot::kMaxBands; ++b )
		loudest = std::max( loudest, f.bands[ b ].level );
	std::vector< int64_t > times;
	for( int b = 0; b < f.bandCount && b < earshot::kMaxBands; ++b )
	{
		const earshot::Band& band = f.bands[ b ];
		//The first frame primes: counts already standing are not onsets.
		const bool dominant = band.level >= kKickDominance * loudest || ( cpuHooks & kCpuNoDominance );
		if( onsetsPrimed && band.onsetCount != lastOnsetCount[ b ] && ( ( b == kKickBand && dominant ) || ( cpuHooks & kCpuAnyBand ) ) )
		{
			int64_t at = frameUs;
			if( f.source != earshot::Source::Host && !( cpuHooks & kCpuEventsAtFrame ) && band.lastOnsetTime >= 0.0 )
				at = frameUs - std::llround( ( f.audioTime - band.lastOnsetTime ) * 1e6 );
			times.push_back( std::clamp( at, frameStartUs, frameUs ) );
		}
		lastOnsetCount[ b ] = band.onsetCount;
	}
	onsetsPrimed = true;
	std::sort( times.begin(), times.end() );
	const double S = ShakeFromParam( params[ PT_SHAKE ] );
	for( int64_t t : times )
		if( t - lastKickUs >= kKickMergeUs )
		{
			events.push_back( { t, S } );
			lastKickUs = t;
		}
}

std::vector< ShakeEvent > SnowglobePlugin::gatherEvents( int64_t frameStartUs, int64_t frameUs )
{
	const TriggerOn source = static_cast< TriggerOn >( OptionIndex( params[ PT_TRIGGER_ON ], static_cast< int >( TriggerOn::Count ) ) );
	const double S         = ShakeFromParam( params[ PT_SHAKE ] );
	std::vector< ShakeEvent > events;

	//The button, at the frame: a full shake each press.
	if( cpuHooks & kCpuDropPresses )
		shakePresses = std::min( shakePresses, 1 );
	for( ; shakePresses > 0; --shakePresses )
		events.push_back( { frameUs, 1.0 } );

	//The beat: each quarter of the bar, the crossing placed inside the frame
	//by interpolating the phase. A host that never moves barPhase gets a
	//clock of the plugin's own at its BPM.
	if( barPhase != lastBarPhase && lastBarPhase >= 0.0f )
		hostBeatSeen = true;
	lastBarPhase = barPhase;
	const double tempo    = bpm > 1.0f ? static_cast< double >( bpm ) : 120.0;
	const double phaseNow = hostBeatSeen ? std::clamp( static_cast< double >( barPhase ), 0.0, 1.0 )
	                                     : std::fmod( static_cast< double >( frameUs ) * 1e-6 * tempo / 240.0, 1.0 );
	const int64_t dt = frameUs - frameStartUs;
	if( source == TriggerOn::Beat && lastPhase >= 0.0 && dt > 0 )
	{
		double p0 = lastPhase, p1 = phaseNow;
		if( p1 < p0 )
			p1 += 1.0;
		if( p1 > p0 )
		{
			const double quarter = 0.25;
			double boundary      = ( std::floor( p0 / quarter + 1e-9 ) + 1.0 ) * quarter;
			for( int n = 0; boundary <= p1 + 1e-12 && n < kMaxBeatsPerFrame; ++n, boundary += quarter )
			{
				const double inside = ( cpuHooks & kCpuEventsAtFrame ) ? 1.0 : ( boundary - p0 ) / ( p1 - p0 );
				events.push_back( { frameStartUs + std::llround( inside * static_cast< double >( dt ) ), S } );
			}
		}
	}
	lastPhase = phaseNow;

	if( source == TriggerOn::Kick )
		handleKicks( frameStartUs, frameUs, events );
	else if( cpuHooks & kCpuIgnoreGate )
	{
		//The negative control: what a plugin that lists and opens regardless does.
		audio.EnsureListed();
		audio.Want( params[ PT_INPUT ] );
		audio.Apply();
	}
	else
		audio.Release();

	//The harness's own shakes, if due.
	for( auto it = queued.begin(); it != queued.end(); )
	{
		if( it->us <= frameUs )
		{
			const int64_t at = ( cpuHooks & kCpuEventsAtFrame ) ? frameUs : std::max( it->us, frameStartUs );
			events.push_back( { at, it->strength } );
			it = queued.erase( it );
		}
		else
			++it;
	}
	std::stable_sort( events.begin(), events.end(), []( const ShakeEvent& a, const ShakeEvent& b ) { return a.us < b.us; } );
	for( const ShakeEvent& e : events )
		shakeLog.push_back( { e.us, e.strength } );
	if( shakeLog.size() > 100000 )
		shakeLog.erase( shakeLog.begin(), shakeLog.begin() + 50000 );
	return events;
}

//---------------------------------------------------------------------------
// The flakes: each centre projected through the fill (Optics.cpp), its two
// semi-axes through the projection's Jacobian, orthogonalised by the 2x2
// SVD, a semi-axis under half a pixel widened to it, the coverage scaled so
// it integrates to the true area. Sorted back to front.
//---------------------------------------------------------------------------
void SnowglobePlugin::buildFlakes( const Physics& p, int width, int height, double globePx )
{
	(void)width;
	(void)height;
	globe.Positions( clockUs, p, positions );
	drawn.clear();
	const double n     = ( cpuHooks & kCpuWaterIndex ) ? kWaterIndex : p.fill.n;
	const double scale = globePx / SilhouetteTangent();
	const bool refract = !( cpuHooks & kCpuFlakesStraight );
	const auto& flakes = globe.Flakes();
	for( int i = 0; i < static_cast< int >( positions.size() ); ++i )
	{
		const V3 x      = positions[ static_cast< size_t >( i ) ];
		const double P[ 3 ] = { x.x / kRadius, x.y / kRadius, x.z / kRadius };
		const Projection pr = ProjectPoint( P, n, refract );
		if( !pr.seen )
			continue;
		const double r = globe.RadiusOf( i, p ) / kRadius;
		//The image is {M v : |v| <= 1}: for a disc M is the Jacobian on the
		//disc's plane (2 x 2), for a bead the whole Jacobian (2 x 3), whose
		//outline is the bead's silhouette to first order. Either way
		//M M^T = [[a, b], [b, c]], and the ellipse's semi-axes are its
		//eigenvectors, scaled by the square roots of its eigenvalues.
		double a = 0.0, b = 0.0, c = 0.0;
		const double sr = scale * r;
		if( p.shape == Shape::Flakes )
		{
			V3 e1{ 1.0, 0.0, 0.0 }, e2{ 0.0, 1.0, 0.0 };
			planeOf( flakes[ static_cast< size_t >( i ) ].n, e1, e2 );
			const V3 axes[ 2 ] = { e1, e2 };
			double M[ 2 ][ 2 ];
			for( int k = 0; k < 2; ++k )
				for( int row = 0; row < 2; ++row )
					M[ row ][ k ] = sr * ( pr.J[ row ][ 0 ] * axes[ k ].x + pr.J[ row ][ 1 ] * axes[ k ].y + pr.J[ row ][ 2 ] * axes[ k ].z );
			a = M[ 0 ][ 0 ] * M[ 0 ][ 0 ] + M[ 0 ][ 1 ] * M[ 0 ][ 1 ];
			b = M[ 0 ][ 0 ] * M[ 1 ][ 0 ] + M[ 0 ][ 1 ] * M[ 1 ][ 1 ];
			c = M[ 1 ][ 0 ] * M[ 1 ][ 0 ] + M[ 1 ][ 1 ] * M[ 1 ][ 1 ];
		}
		else
			for( int k = 0; k < 3; ++k )
			{
				a += sr * sr * pr.J[ 0 ][ k ] * pr.J[ 0 ][ k ];
				b += sr * sr * pr.J[ 0 ][ k ] * pr.J[ 1 ][ k ];
				c += sr * sr * pr.J[ 1 ][ k ] * pr.J[ 1 ][ k ];
			}
		const double h = 0.5 * ( a - c );
		const double q = std::sqrt( h * h + b * b );
		const double l1 = std::max( 0.5 * ( a + c ) + q, 0.0 ), l2 = std::max( 0.5 * ( a + c ) - q, 0.0 );
		double ux = 1.0, uy = 0.0;
		if( q > 1e-30 )
		{
			ux = h + q;
			uy = b;
			if( std::fabs( ux ) + std::fabs( uy ) < 1e-30 )
			{
				ux = b;
				uy = q - h;
			}
			const double len = std::sqrt( ux * ux + uy * uy );
			ux /= len;
			uy /= len;
		}
		const double s1 = std::sqrt( l1 ), s2 = std::sqrt( l2 );
		const double w1 = std::max( s1, 0.5 ), w2 = std::max( s2, 0.5 );
		DrawnFlake d;
		d.index  = i;
		d.cx     = scale * pr.xi[ 0 ];
		d.cy     = scale * pr.xi[ 1 ];
		d.a1x    = w1 * ux;
		d.a1y    = w1 * uy;
		d.a2x    = -w2 * uy;
		d.a2y    = w2 * ux;
		//The shader's coverage is clamp(0.5 - (r - 1) / |grad r|) with r the
		//ellipse's own radius (|grad r| depends on the direction alone), and
		//it integrates in closed form to pi w1 w2 + (pi / 24)(w1/w2 + w2/w1)
		//(pi/12 for a circle, as Steiner's parallel curves give); the shader
		//box-filters it over each pixel, so the coverage that sums to the
		//true area pi s1 s2 is this (AGENTS.md, "Flake coverage").
		d.alpha  = ( s1 * s2 ) / ( w1 * w2 + ( w1 / w2 + w2 / w1 ) / 24.0 );
		d.behind = P[ 2 ] < 0.0;
		drawn.push_back( d );
	}
	//Back to front: the card's far side first (z ascending), the index
	//breaking ties so the order never depends on the sort.
	std::vector< std::pair< double, int > > order;
	order.reserve( drawn.size() );
	for( size_t k = 0; k < drawn.size(); ++k )
		order.emplace_back( positions[ static_cast< size_t >( drawn[ k ].index ) ].z, static_cast< int >( k ) );
	std::sort( order.begin(), order.end() );
	std::vector< DrawnFlake > sorted;
	sorted.reserve( drawn.size() );
	for( const auto& o : order )
		sorted.push_back( drawn[ static_cast< size_t >( o.second ) ] );
	drawn.swap( sorted );
	behindCount = 0;
	instances.assign( drawn.size() * 8, 0.0f );
	for( size_t k = 0; k < drawn.size(); ++k )
	{
		const DrawnFlake& d = drawn[ k ];
		float* v            = &instances[ k * 8 ];
		v[ 0 ]              = static_cast< float >( d.cx );
		v[ 1 ]              = static_cast< float >( d.cy );
		v[ 2 ]              = static_cast< float >( d.a1x );
		v[ 3 ]              = static_cast< float >( d.a1y );
		v[ 4 ]              = static_cast< float >( d.a2x );
		v[ 5 ]              = static_cast< float >( d.a2y );
		v[ 6 ]              = static_cast< float >( d.alpha );
		if( d.behind )
			++behindCount;
	}
}

void SnowglobePlugin::drawFlakes( int first, int count, int width, int height )
{
	if( count <= 0 )
		return;
	glUseProgram( flakeShader.GetGLID() );
	uniform( flakeShader, "Hooks", hooks );
	glUniform4i( where( flakeShader, "Viewport" ), 0, 0, width, height );
	glUniform2f( where( flakeShader, "Size" ), static_cast< float >( width ), static_cast< float >( height ) );
	uniform( flakeShader, "GlobePx", static_cast< float >( frame.globePx ) );
	glBindVertexArray( flakeVao );
	glBindBuffer( GL_ARRAY_BUFFER, flakeVbo );
	glBufferSubData( GL_ARRAY_BUFFER, 0, static_cast< GLsizeiptr >( count * 8 * sizeof( float ) ), &instances[ static_cast< size_t >( first ) * 8 ] );
	glDrawArraysInstanced( GL_TRIANGLES, 0, 6, count );
	glBindVertexArray( 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
}

//---------------------------------------------------------------------------
FFResult SnowglobePlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr || pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct* input = pgl->inputTextures[ 0 ];

	//The host's state, put back however this returns.
	ScopedGLState restore;
	const GLint* hostViewport = restore.saved.viewport;
	glDisable( GL_BLEND );
	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	const int width  = hostViewport[ 2 ];
	const int height = hostViewport[ 3 ];
	if( width <= 0 || height <= 0 || input->Width == 0 || input->Height == 0 )
		return FF_FAIL;

	//---------------------------------------------------------------------
	// The clock: the sum of the host's steps (each at most 0.1 s), in
	// integer microseconds from the first frame.
	//---------------------------------------------------------------------
	UpdateClock();
	int64_t stepUs = 0;
	if( lastNowUs != INT64_MIN && !settledJump )
		stepUs = std::clamp< int64_t >( nowUs - lastNowUs, 0, kMaxStepUs );
	settledJump = false;
	lastNowUs   = nowUs;
	const int64_t frameStartUs = clockUs;
	clockUs += stepUs;

	//---------------------------------------------------------------------
	// The physics: started settled on the first frame, shaken, advanced.
	//---------------------------------------------------------------------
	const Physics physics = CurrentPhysics();
	if( !globe.Started() )
		globe.Start( physics );
	const bool resized = targetW != width || targetH != height;
	if( resized && targetW != 0 && ( cpuHooks & kCpuResizeRestarts ) )
		globe.Start( physics );//the negative control: a resize loses the flakes
	const std::vector< ShakeEvent > events = gatherEvents( frameStartUs, clockUs );
	globe.Advance( clockUs, events, physics );

	const double globePx = 0.5 * GlobeFromParam( params[ PT_GLOBE ] ) * static_cast< double >( height );
	buildFlakes( physics, width, height, globePx );

	frame.clockUs   = clockUs;
	frame.stateUs   = globe.StateUs();
	frame.drawn     = static_cast< int >( drawn.size() );
	frame.behind    = behindCount;
	frame.suspended = globe.Suspended( physics.count );
	frame.stateHash = globe.Hash( physics.count );
	frame.globePx   = globePx;
	frame.index     = ( cpuHooks & kCpuWaterIndex ) ? kWaterIndex : physics.fill.n;

	//---------------------------------------------------------------------
	// The GL: flakes behind, the scene, flakes in front, into the target;
	// then the composite onto the host's.
	//---------------------------------------------------------------------
	if( !target.Ensure( width, height, GL_RGBA32F, PassBuffer::Sampling::Nearest ) )
	{
		diag::error( "could not allocate the target" );
		return FF_FAIL;
	}
	targetW = width;
	targetH = height;
	{
		ScopedFBOBinding bindTarget( target.GetGLID(), ScopedFBOBinding::RB_REVERT );
		glViewport( 0, 0, width, height );
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		glClear( GL_COLOR_BUFFER_BIT );
		glEnable( GL_BLEND );
		setOverBlend();
		drawFlakes( 0, behindCount, width, height );

		glUseProgram( sceneShader.GetGLID() );
		bindUnit( 0, input->Handle );
		uniform( sceneShader, "Hooks", hooks );
		glUniform4i( where( sceneShader, "Viewport" ), 0, 0, width, height );
		glUniform2f( where( sceneShader, "Size" ), static_cast< float >( width ), static_cast< float >( height ) );
		uniform( sceneShader, "InputTexture", 0 );
		glUniform2i( where( sceneShader, "InputSize" ), static_cast< int >( input->Width ), static_cast< int >( input->Height ) );
		uniform( sceneShader, "Index", static_cast< float >( frame.index ) );
		uniform( sceneShader, "GlobePx", static_cast< float >( globePx ) );
		uniform( sceneShader, "XiSil", static_cast< float >( SilhouetteTangent() ) );
		uniform( sceneShader, "ViewDistance", static_cast< float >( kViewDistance ) );
		uniform( sceneShader, "ClipAspect", static_cast< float >( static_cast< double >( input->Width ) / static_cast< double >( input->Height ) ) );
		quad.Draw();
		unbindTextureUnits( 1 );

		drawFlakes( behindCount, static_cast< int >( drawn.size() ) - behindCount, width, height );
		glDisable( GL_BLEND );
		glUseProgram( 0 );
	}

	glBindFramebuffer( GL_FRAMEBUFFER, pgl->HostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	glUseProgram( compositeShader.GetGLID() );
	bindUnit( 0, input->Handle );
	bindUnit( 1, target.TextureID() );
	uniform( compositeShader, "Hooks", hooks );
	glUniform4i( where( compositeShader, "Viewport" ), hostViewport[ 0 ], hostViewport[ 1 ], width, height );
	glUniform2f( where( compositeShader, "Size" ), static_cast< float >( width ), static_cast< float >( height ) );
	uniform( compositeShader, "InputTexture", 0 );
	uniform( compositeShader, "Globe", 1 );
	glUniform2i( where( compositeShader, "InputSize" ), static_cast< int >( input->Width ), static_cast< int >( input->Height ) );
	uniform( compositeShader, "MixAmount", std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) );
	quad.Draw();
	if( cpuHooks & kCpuLeakState )
		return FF_SUCCESS;//the negative control: the program and two textures left bound
	unbindTextureUnits( 2 );
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult SnowglobePlugin::DeInitGL()
{
	uniformCache.clear();
	sceneShader.FreeGLResources();
	flakeShader.FreeGLResources();
	compositeShader.FreeGLResources();
	quad.Release();
	if( flakeVbo != 0 )
		glDeleteBuffers( 1, &flakeVbo );
	if( flakeVao != 0 )
		glDeleteVertexArrays( 1, &flakeVao );
	flakeVbo = flakeVao = 0;
	target.Destroy();
	targetW = targetH = 0;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult SnowglobePlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

char* SnowglobePlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult SnowglobePlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

FFResult SnowglobePlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= hostOrder.size() )
		return FF_FAIL;
	const unsigned int id = hostOrder[ index ];

	// The About buttons open a browser and store nothing.
	if( id >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( id - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	//The press counts on the rising edge, once.
	if( id == PT_SHAKE_NOW )
	{
		const bool down = value >= 0.5f;
		if( down && !shakeHeld )
			++shakePresses;
		shakeHeld = down;
	}
	//Audio Input is only remembered here; it opens from the frame, under
	//Kick (Audio.h).
	params[ id ] = value;
	return FF_SUCCESS;
}

float SnowglobePlugin::GetFloatParameter( unsigned int index )
{
	return index < hostOrder.size() ? params[ hostOrder[ index ] ] : 0.0f;
}

} // namespace snowglobe
