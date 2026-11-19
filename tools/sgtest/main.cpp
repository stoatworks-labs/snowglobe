/**
    sgtest -- render Snowglobe offline, and measure what its flakes and its
    glass do.

    It drives the REAL plugin class, through the same ProcessOpenGL a host
    calls, on a synthetic clock, in a headless CGL context, with audio fed
    through earshot's feed() exactly as a capture device would deliver it. No
    step opens an audio device (SNOWGLOBE_NO_DEVICES=1 is set first thing; the
    --coreaudio check runs children with an interposer that counts).

    Every claim is read out of the rendered picture and measured against
    something that is not the plugin's code: the GPA's tables for the fill
    (not the fits the plugin uses), a ray trace written here in long double
    with angles and Snell's law in scalar form (not the shader's vector form
    or the CPU's Newton), closed forms for a fall.

        sgtest --out /tmp/s.png        the defaults over the harness's card and its music, 12 s in
        sgtest --list                  every parameter and its default
        sgtest --pipe [--wav F.wav]    raw RGBA frames in on stdin, out on stdout
        sgtest --film N [--wav F.wav]  N frames, raw RGBA on stdout
        sgtest --offline               the checks that need no GL context

    SGTEST_RENDERER=software asks for Apple's software renderer by id.
*/
#include "Audio.h"
#include "Fill.h"
#include "Globe.h"
#include "Optics.h"
#include "Controls.h"
#include "Shaders.h"
#include "Snowglobe.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <crt_externs.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <complex>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace snowglobe;

namespace
{
using Floats  = std::vector< float >;
using Doubles = std::vector< double >;
using Bytes   = std::vector< unsigned char >;

std::string g_argv0;

//---------------------------------------------------------------------------
// Reporting.
//---------------------------------------------------------------------------
int g_failures = 0;
int g_checks   = 0;

std::string fmt( const char* format, ... )
{
	char buffer[ 4096 ];
	va_list args;
	va_start( args, format );
	std::vsnprintf( buffer, sizeof( buffer ), format, args );
	va_end( args );
	return buffer;
}

void Check( bool condition, const std::string& message )
{
	++g_checks;
	std::printf( "  %s  %s\n", condition ? "ok  " : "FAIL", message.c_str() );
	if( !condition )
		++g_failures;
}

void Info( const std::string& message )
{
	std::printf( "  info  %s\n", message.c_str() );
}

int Verdict()
{
	std::printf( "\n  %s\n", g_failures == 0 ? "PASS" : "FAIL" );
	return g_failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// PNG. zlib ships with the OS.
//---------------------------------------------------------------------------
void putU32( Bytes& out, uint32_t value )
{
	out.push_back( static_cast< unsigned char >( value >> 24 ) );
	out.push_back( static_cast< unsigned char >( value >> 16 ) );
	out.push_back( static_cast< unsigned char >( value >> 8 ) );
	out.push_back( static_cast< unsigned char >( value ) );
}

void putChunk( Bytes& out, const char* type, const Bytes& data )
{
	putU32( out, static_cast< uint32_t >( data.size() ) );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, static_cast< uInt >( 4 + data.size() ) );
	putU32( out, static_cast< uint32_t >( crc ) );
}

/// `rgba` is floats, row 0 at the BOTTOM (GL's order); the file is written
/// top row first, which is the only place anything here flips. Alpha is kept.
bool writePng( const std::string& path, int width, int height, const Floats& rgba )
{
	Bytes raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = height - 1; y >= 0; --y )
	{
		raw.push_back( 0 );
		for( int x = 0; x < width; ++x )
			for( int c = 0; c < 4; ++c )
			{
				const float v = rgba[ ( static_cast< size_t >( y ) * width + x ) * 4 + c ];
				raw.push_back( static_cast< unsigned char >( std::lround( std::clamp( v, 0.0f, 1.0f ) * 255.0f ) ) );
			}
	}
	uLongf compressedSize = compressBound( static_cast< uLong >( raw.size() ) );
	Bytes compressed( compressedSize );
	if( compress2( compressed.data(), &compressedSize, raw.data(), static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	compressed.resize( compressedSize );
	Bytes png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	Bytes ihdr;
	putU32( ihdr, static_cast< uint32_t >( width ) );
	putU32( ihdr, static_cast< uint32_t >( height ) );
	ihdr.insert( ihdr.end(), { 8, 6, 0, 0, 0 } );
	putChunk( png, "IHDR", ihdr );
	putChunk( png, "IDAT", compressed );
	putChunk( png, "IEND", {} );
	FILE* file = std::fopen( path.c_str(), "wb" );
	if( !file )
		return false;
	const size_t written = std::fwrite( png.data(), 1, png.size(), file );
	std::fclose( file );
	return written == png.size();
}

//---------------------------------------------------------------------------
// The context.
//---------------------------------------------------------------------------
bool g_software = false;

CGLContextObj createContext()
{
	const CGLPixelFormatAttribute accelerated[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAAccelerated,
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute fallback[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute generic[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFARendererID, static_cast< CGLPixelFormatAttribute >( kCGLRendererGenericFloatID ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	CGLPixelFormatObj format = nullptr;
	GLint formatCount        = 0;
	const char* renderer     = std::getenv( "SGTEST_RENDERER" );
	if( renderer != nullptr && std::strcmp( renderer, "software" ) == 0 )
	{
		if( CGLChoosePixelFormat( generic, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
		g_software = true;
		std::fprintf( stderr, "sgtest: SGTEST_RENDERER=software, Apple's software renderer\n" );
	}
	else if( CGLChoosePixelFormat( accelerated, &format, &formatCount ) != kCGLNoError || format == nullptr )
	{
		if( CGLChoosePixelFormat( fallback, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
	}
	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;
	CGLSetCurrentContext( context );
	return context;
}

const char* kindName( unsigned int type )
{
	switch( type )
	{
	case FF_TYPE_BOOLEAN: return "bool";
	case FF_TYPE_EVENT: return "event";
	case FF_TYPE_OPTION: return "option";
	case FF_TYPE_STANDARD: return "standard";
	case FF_TYPE_TEXT: return "text";
	case FF_TYPE_BUFFER: return "buffer";
	default: return "other";
	}
}

bool stepsBetweenCues( unsigned int type )
{
	return type == FF_TYPE_OPTION || type == FF_TYPE_BOOLEAN || type == FF_TYPE_EVENT;
}

//---------------------------------------------------------------------------
// The cue sheet.
//---------------------------------------------------------------------------
using Track = std::vector< std::pair< int, float > >;

float valueAt( const Track& track, int frame, bool ramp )
{
	if( track.empty() )
		return 0.0f;
	if( frame <= track.front().first )
		return track.front().second;
	if( frame >= track.back().first )
		return track.back().second;
	for( size_t i = 0; i + 1 < track.size(); ++i )
	{
		const auto& a = track[ i ];
		const auto& b = track[ i + 1 ];
		if( frame >= a.first && frame < b.first )
		{
			if( !ramp )
				return a.second;
			const float t = static_cast< float >( frame - a.first ) / static_cast< float >( b.first - a.first );
			return a.second + ( b.second - a.second ) * t;
		}
	}
	return track.back().second;
}

//---------------------------------------------------------------------------
// Pictures. Every one is rows BOTTOM first (GL's order), as uploaded.
//---------------------------------------------------------------------------
/// The look card: colour everywhere: hue sweeping across, value bands down,
/// a chequer of saturated squares and a white bar, so the glass has edges to
/// bend all the way to its rim.
Floats lookCard( int width, int height )
{
	Floats card( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const double u = ( x + 0.5 ) / width, v = ( y + 0.5 ) / height;
			const double h = std::fmod( u * 1.5 + v * 0.35, 1.0 ) * 6.0;
			const int sector = static_cast< int >( h ) % 6;
			const double f   = h - std::floor( h );
			double r = 0, g = 0, b = 0;
			switch( sector )
			{
			case 0: r = 1, g = f, b = 0; break;
			case 1: r = 1 - f, g = 1, b = 0; break;
			case 2: r = 0, g = 1, b = f; break;
			case 3: r = 0, g = 1 - f, b = 1; break;
			case 4: r = f, g = 0, b = 1; break;
			default: r = 1, g = 0, b = 1 - f; break;
			}
			const double band = 0.55 + 0.45 * ( ( static_cast< int >( v * 9.0 ) % 2 ) ? 1.0 : 0.6 );
			const bool chequer = ( ( static_cast< int >( u * 12.0 ) + static_cast< int >( v * 7.0 ) ) % 5 ) == 0;
			const bool bar     = v > 0.46 && v < 0.54;
			float* p           = &card[ ( static_cast< size_t >( y ) * width + x ) * 4 ];
			p[ 0 ] = static_cast< float >( bar ? 1.0 : chequer ? 0.05 : r * band );
			p[ 1 ] = static_cast< float >( bar ? 1.0 : chequer ? 0.05 : g * band );
			p[ 2 ] = static_cast< float >( bar ? 1.0 : chequer ? 0.20 : b * band );
			p[ 3 ] = 1.0f;
		}
	return card;
}

/// Each texel its own position: the output is the backward map.
Floats uvCard( int width, int height )
{
	Floats card( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			float* p = &card[ ( static_cast< size_t >( y ) * width + x ) * 4 ];
			p[ 0 ]   = static_cast< float >( ( x + 0.5 ) / width );
			p[ 1 ]   = static_cast< float >( ( y + 0.5 ) / height );
			p[ 2 ]   = 0.0f;
			p[ 3 ]   = 1.0f;
		}
	return card;
}

Floats flatCard( int width, int height, float r, float g, float b, float a = 1.0f )
{
	Floats card( static_cast< size_t >( width ) * height * 4 );
	for( size_t i = 0; i < card.size(); i += 4 )
	{
		card[ i ]     = r;
		card[ i + 1 ] = g;
		card[ i + 2 ] = b;
		card[ i + 3 ] = a;
	}
	return card;
}

GLuint makeTexture( int width, int height, const float* pixels, GLint format = GL_RGBA32F )
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, GL_FLOAT, pixels );
	//Linear, as a host's clip texture is (the plugin reads texels by hand).
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

//---------------------------------------------------------------------------
// The harness's music: a kick every half second and a hat between, fed
// through earshot's feed() at 48 kHz stereo, as a device would deliver it.
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
// The rig.
//---------------------------------------------------------------------------
struct Rig
{
	SnowglobePlugin plugin;
	int width = 0, height = 0;
	GLuint sourceTexture = 0, outputTexture = 0, outputFBO = 0;
	int frame  = 0;
	double fps = 60.0;
	double timeOffsetMs = 0.0;           ///< SetTime gets ms + this when msClock
	bool msClock        = false;         ///< SetTime in milliseconds (Resolume's habit)
	double beatBpm      = 0.0;           ///< > 0: SetBeatInfo every frame, phase from the clock
	std::function< double( double ) > audio;///< fed through earshot each frame when set
	int audioRate = kRate;                  ///< the rate `audio` is sampled and fed at
	std::function< void( int ) > beforeFrame;
	double fedUntil = 0.0;

	ProcessOpenGLStruct process    = {};
	FFGLTextureStruct inputStruct  = {};
	FFGLTextureStruct* inputs[ 1 ] = { nullptr };

	~Rig()
	{
		plugin.DeInitGL();
		release();
	}

	void release()
	{
		if( outputFBO )
			glDeleteFramebuffers( 1, &outputFBO );
		if( outputTexture )
			glDeleteTextures( 1, &outputTexture );
		if( sourceTexture )
			glDeleteTextures( 1, &sourceTexture );
		outputFBO = outputTexture = sourceTexture = 0;
	}

	bool attach( int w, int h, const Floats* picture )
	{
		width         = w;
		height        = h;
		outputTexture = makeTexture( width, height, nullptr );
		glGenFramebuffers( 1, &outputFBO );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTexture, 0 );
		if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
			return false;
		process.HostFBO   = outputFBO;
		const Floats card = picture ? *picture : lookCard( width, height );
		sourceTexture     = makeTexture( width, height, card.data() );
		inputStruct.Width = inputStruct.HardwareWidth = static_cast< FFUInt32 >( width );
		inputStruct.Height = inputStruct.HardwareHeight = static_cast< FFUInt32 >( height );
		inputStruct.Handle                              = sourceTexture;
		inputs[ 0 ]                                     = &inputStruct;
		process.numInputTextures                        = 1;
		process.inputTextures                           = inputs;
		return true;
	}

	bool Init( int w, int h, const Floats* picture = nullptr )
	{
		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( w );
		viewport.height             = static_cast< FFUInt32 >( h );
		if( plugin.InitGL( &viewport ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "InitGL failed -- see ~/Library/Logs/snowglobe for which shader\n" );
			return false;
		}
		plugin.SetClockScaleForTest( 1.0 );
		return attach( w, h, picture );
	}

	/// The host's raster changes under a running instance: no InitGL.
	bool Resize( int w, int h, const Floats* picture = nullptr )
	{
		release();
		return attach( w, h, picture );
	}

	void Upload( const Floats& picture )
	{
		glBindTexture( GL_TEXTURE_2D, sourceTexture );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, picture.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	void Set( unsigned int id, float value )
	{
		plugin.SetById( id, value );
	}

	double Seconds( int f ) const
	{
		return static_cast< double >( f ) / fps;
	}

	bool Render( int frames = 1 )
	{
		for( int i = 0; i < frames; ++i )
		{
			if( beforeFrame )
				beforeFrame( frame );
			const double t = Seconds( frame );
			if( audio )
			{
				//Every sample from the last frame's time to this one, once.
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
			}
			if( beatBpm > 0.0 )
				plugin.SetBeatInfo( static_cast< float >( beatBpm ), static_cast< float >( std::fmod( t * beatBpm / 240.0, 1.0 ) ) );
			plugin.SetTime( msClock ? t * 1000.0 + timeOffsetMs : t );
			++frame;
			glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
			glViewport( 0, 0, width, height );
			glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
			glClear( GL_COLOR_BUFFER_BIT );
			if( plugin.ProcessOpenGL( &process ) != FF_SUCCESS )
			{
				std::fprintf( stderr, "ProcessOpenGL failed\n" );
				return false;
			}
		}
		return true;
	}

	/// Press an event on the next frame and render it; released after.
	bool Press( unsigned int id )
	{
		Set( id, 1.0f );
		const bool ok = Render( 1 );
		Set( id, 0.0f );
		return ok;
	}

	Floats Output() const
	{
		Floats pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data() );
		return pixels;
	}
};

//---------------------------------------------------------------------------
// Parameters by display name.
//---------------------------------------------------------------------------
struct NamedParameter
{
	std::string name;
	unsigned int index;
	float value;
	unsigned int type;
};

std::vector< NamedParameter > listParameters( SnowglobePlugin& plugin )
{
	std::vector< NamedParameter > list;
	for( unsigned int i = 0; i < plugin.GetNumParams(); ++i )
	{
		const char* const name = plugin.GetParamName( i );
		list.push_back( NamedParameter { name ? name : "?", i, plugin.GetFloatParameter( i ), plugin.GetParamType( i ) } );
	}
	return list;
}

/// An option element's stored value (the device menu's are earshot keys).
float elementValue( SnowglobePlugin& plugin, unsigned int index, unsigned int element )
{
	const FFMixed mixed = plugin.GetParamElementDefault( index, element );
	float value         = 0.0f;
	std::memcpy( &value, &mixed.UIntValue, sizeof( value ) );
	return value;
}

/// An option's element by its name: its stored value for Audio Input (a
/// device key), its index for every other option.
bool optionValue( SnowglobePlugin& plugin, unsigned int index, const std::string& value, float& out )
{
	for( unsigned int e = 0; e < plugin.GetNumParamElements( index ); ++e )
		if( value == plugin.GetParamElementName( index, e ) )
		{
			out = index == static_cast< unsigned int >( plugin.HostIndexOf( PT_INPUT ) ) ? elementValue( plugin, index, e ) : static_cast< float >( e );
			return true;
		}
	return false;
}

/// `Name=Value`. The value is a number, or for an option the name of one of
/// its elements; anything else is refused, never read as 0 (polyhedral's trap).
bool applySetting( SnowglobePlugin& plugin, const std::string& assignment, std::string& error )
{
	const size_t equals = assignment.find( '=' );
	if( equals == std::string::npos )
	{
		error = "expected Name=Value";
		return false;
	}
	const std::string name  = assignment.substr( 0, equals );
	const std::string value = assignment.substr( equals + 1 );
	for( const NamedParameter& parameter : listParameters( plugin ) )
		if( parameter.name == name )
		{
			if( parameter.type == FF_TYPE_TEXT || parameter.type == FF_TYPE_BUFFER )
			{
				error = name + " cannot be set from here";
				return false;
			}
			float v = 0.0f;
			if( parameter.type == FF_TYPE_OPTION && optionValue( plugin, parameter.index, value, v ) )
			{
				plugin.SetFloatParameter( parameter.index, v );
				return true;
			}
			char* end       = nullptr;
			const float got = std::strtof( value.c_str(), &end );
			if( value.empty() || end == value.c_str() || *end != '\0' || !std::isfinite( got ) )
			{
				error = "'" + value + "' is not a number" + ( parameter.type == FF_TYPE_OPTION ? " or an option of " + name : "" );
				return false;
			}
			if( parameter.type != FF_TYPE_OPTION && ( got < 0.0f || got > 1.0f ) )
			{
				error = "'" + value + "' is outside the host's 0..1";
				return false;
			}
			plugin.SetFloatParameter( parameter.index, got );
			return true;
		}
	error = "no parameter called '" + name + "'";
	return false;
}

struct Cue
{
	Track track;
	bool ramp;
};

/// A cue line's name is the longest run of words that is a parameter, and the
/// rest is its value: a number, or an option's name (honeydew's rule).
bool bindScript( SnowglobePlugin& plugin, std::istream& in, const std::string& path, std::map< unsigned int, Cue >& out, std::string& error )
{
	const std::vector< NamedParameter > known = listParameters( plugin );
	std::string line;
	int lineNumber = 0;
	while( std::getline( in, line ) )
	{
		++lineNumber;
		const size_t hash = line.find( '#' );
		if( hash != std::string::npos )
			line.erase( hash );
		std::istringstream words( line );
		int frame = 0;
		if( !( words >> frame ) )
			continue;
		std::vector< std::string > parts;
		std::string word;
		while( words >> word )
			parts.push_back( word );
		const NamedParameter* found = nullptr;
		size_t used                 = 0;
		for( size_t n = parts.size() > 0 ? parts.size() - 1 : 0; n >= 1 && found == nullptr; --n )
		{
			std::string name = parts[ 0 ];
			for( size_t i = 1; i < n; ++i )
				name += " " + parts[ i ];
			for( const NamedParameter& p : known )
				if( p.name == name )
				{
					found = &p;
					used  = n;
				}
		}
		if( found == nullptr )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": names no parameter (try --list)";
			return false;
		}
		std::string value = parts[ used ];
		for( size_t i = used + 1; i < parts.size(); ++i )
			value += " " + parts[ i ];
		float v      = 0.0f;
		bool matched = found->type == FF_TYPE_OPTION && optionValue( plugin, found->index, value, v );
		if( !matched )
		{
			char* end = nullptr;
			v         = std::strtof( value.c_str(), &end );
			if( value.empty() || end == value.c_str() || *end != '\0' )
			{
				error = path + ":" + std::to_string( lineNumber ) + ": '" + value + "' is not a number";
				return false;
			}
		}
		Cue& cue = out[ found->index ];
		cue.ramp = !stepsBetweenCues( found->type );
		cue.track.emplace_back( frame, v );
	}
	for( auto& entry : out )
		std::stable_sort( entry.second.track.begin(), entry.second.track.end(),
		                  []( const std::pair< int, float >& a, const std::pair< int, float >& b ) { return a.first < b.first; } );
	return true;
}

enum class AudioSource
{
	Music,  ///< the harness's kicks and hats, through earshot's feed()
	Silent, ///< nothing fed: Resolume's FFT path with an empty buffer
	Wav,    ///< --wav: a file's samples, through earshot's feed()
};

//---------------------------------------------------------------------------
// --wav: a PCM or float WAV (16, 24 or 32 bit), mixed to mono and fed through
// earshot's feed() at the file's own rate, as a device would deliver it:
// frame k gets samples [round(k R / fps), round((k + 1) R / fps)). After the
// file ends, silence. For the video and the flash survey: Resolume's own
// demo stems drive the shakes, and the same file is the soundtrack. (The
// reader is woofer's wftest's.)
//---------------------------------------------------------------------------
struct Wav
{
	int rate     = 0;
	int channels = 0;
	Floats mono;
};

bool readWav( const std::string& path, Wav& wav, std::string& error )
{
	std::ifstream file( path, std::ios::binary );
	if( !file )
	{
		error = "cannot open " + path;
		return false;
	}
	Bytes data( ( std::istreambuf_iterator< char >( file ) ), std::istreambuf_iterator< char >() );
	auto u32 = [ & ]( size_t at ) { return static_cast< uint32_t >( data[ at ] | ( data[ at + 1 ] << 8 ) | ( data[ at + 2 ] << 16 ) | ( data[ at + 3 ] << 24 ) ); };
	auto u16 = [ & ]( size_t at ) { return static_cast< uint32_t >( data[ at ] | ( data[ at + 1 ] << 8 ) ); };
	if( data.size() < 12 || std::memcmp( data.data(), "RIFF", 4 ) != 0 || std::memcmp( data.data() + 8, "WAVE", 4 ) != 0 )
	{
		error = path + " is not a RIFF WAVE file";
		return false;
	}
	int format = 0, bits = 0;
	size_t at  = 12;
	while( at + 8 <= data.size() )
	{
		const uint32_t size = u32( at + 4 );
		if( std::memcmp( data.data() + at, "fmt ", 4 ) == 0 && at + 8 + 16 <= data.size() )
		{
			format       = static_cast< int >( u16( at + 8 ) );
			wav.channels = static_cast< int >( u16( at + 10 ) );
			wav.rate     = static_cast< int >( u32( at + 12 ) );
			bits         = static_cast< int >( u16( at + 22 ) );
			if( format == 0xFFFE && size >= 40 )
				format = static_cast< int >( u16( at + 8 + 24 ) );
		}
		else if( std::memcmp( data.data() + at, "data", 4 ) == 0 )
		{
			if( wav.channels <= 0 || wav.rate <= 0 || ( format != 1 && format != 3 ) )
			{
				error = path + ": only PCM or float WAV";
				return false;
			}
			//A header claiming 0 or 8 bits would make a zero-sized frame and an
			//endless loop below: refuse anything but the three depths read here.
			if( bits != 16 && bits != 24 && bits != 32 )
			{
				error = path + ": only 16, 24 or 32-bit WAV";
				return false;
			}
			const int bytes        = bits / 8;
			const size_t frameSize = static_cast< size_t >( bytes ) * wav.channels;
			const size_t end       = std::min< size_t >( data.size(), at + 8 + size );
			const int used         = std::min( 2, wav.channels );
			for( size_t p = at + 8; p + frameSize <= end; p += frameSize )
			{
				float sum = 0.0f;
				for( int c = 0; c < used; ++c )
				{
					const size_t q = p + static_cast< size_t >( c ) * bytes;
					float v        = 0.0f;
					if( format == 3 && bits == 32 )
						std::memcpy( &v, &data[ q ], 4 );
					else if( bits == 16 )
						v = static_cast< int16_t >( u16( q ) ) / 32768.0f;
					else if( bits == 24 )
						v = static_cast< float >( static_cast< int32_t >( ( data[ q ] << 8 ) | ( data[ q + 1 ] << 16 ) | ( data[ q + 2 ] << 24 ) ) / 2147483648.0 );
					else if( bits == 32 )
						v = static_cast< float >( static_cast< int32_t >( u32( q ) ) / 2147483648.0 );
					sum += v;
				}
				wav.mono.push_back( sum / static_cast< float >( used ) );
			}
			if( wav.mono.empty() )
			{
				error = path + ": no samples";
				return false;
			}
			return true;
		}
		at += 8 + size + ( size & 1 );
	}
	error = path + ": no data chunk";
	return false;
}

Wav g_wav;

void attachAudio( Rig& rig, AudioSource source, double beatBpm )
{
	if( source == AudioSource::Music )
		rig.audio = musicAt;
	else if( source == AudioSource::Wav )
	{
		rig.audioRate = g_wav.rate;
		rig.audio     = []( double t ) {
            const double k = std::round( t * g_wav.rate );
            return k >= 0.0 && k < static_cast< double >( g_wav.mono.size() ) ? static_cast< double >( g_wav.mono[ static_cast< size_t >( k ) ] ) : 0.0;
		};
	}
	rig.beatBpm = beatBpm;
}

/// --pipe and --film: the fleet's raw RGBA frame format, top row first.
int runPipe( int width, int height, double fps, const std::string& scriptPath, int count, bool readStdin, const std::vector< std::string >& settings,
             AudioSource source, double beatBpm )
{
	Rig rig;
	rig.fps = fps;
	if( !rig.Init( width, height ) )
		return 1;
	attachAudio( rig, source, beatBpm );
	for( const std::string& setting : settings )
	{
		std::string error;
		if( !applySetting( rig.plugin, setting, error ) )
		{
			std::fprintf( stderr, "--set %s: %s\n", setting.c_str(), error.c_str() );
			return 2;
		}
	}
	std::map< unsigned int, Cue > automation;
	if( !scriptPath.empty() )
	{
		std::ifstream file( scriptPath );
		std::string error;
		if( !file )
			error = "cannot open " + scriptPath;
		else
			bindScript( rig.plugin, file, scriptPath, automation, error );
		if( !error.empty() )
		{
			std::fprintf( stderr, "%s\n", error.c_str() );
			return 2;
		}
	}

	Bytes in( static_cast< size_t >( width ) * height * 4 );
	Floats picture( in.size() );
	for( int index = 0; readStdin || index < count; ++index )
	{
		if( readStdin )
		{
			size_t filled = 0;
			while( filled < in.size() )
			{
				const ssize_t got = read( STDIN_FILENO, in.data() + filled, in.size() - filled );
				if( got <= 0 )
					break;
				filled += static_cast< size_t >( got );
			}
			//A partial frame is the end of the stream, never a frame.
			if( filled < in.size() )
			{
				if( filled > 0 )
					std::fprintf( stderr, "partial frame at the end (%zu of %zu bytes): dropped\n", filled, in.size() );
				break;
			}
			for( int y = 0; y < height; ++y )
				for( int x = 0; x < width * 4; ++x )
					picture[ static_cast< size_t >( height - 1 - y ) * width * 4 + x ] = in[ static_cast< size_t >( y ) * width * 4 + x ] / 255.0f;
			rig.Upload( picture );
		}
		//A track does nothing before its first key: a press cued at frame 30
		//must not be held from frame 0 (honeydew's trap).
		for( const auto& cue : automation )
			if( index >= cue.second.track.front().first || cue.second.ramp )
				rig.plugin.SetFloatParameter( cue.first, valueAt( cue.second.track, index, cue.second.ramp ) );
		if( !rig.Render( 1 ) )
			return 1;

		const Floats out = rig.Output();
		Bytes bytes( in.size() );
		for( int y = 0; y < height; ++y )
			for( int x = 0; x < width * 4; ++x )
				bytes[ static_cast< size_t >( y ) * width * 4 + x ] = static_cast< unsigned char >(
					std::lround( std::clamp( out[ static_cast< size_t >( height - 1 - y ) * width * 4 + x ], 0.0f, 1.0f ) * 255.0f ) );
		size_t written = 0;
		while( written < bytes.size() )
		{
			const ssize_t put = write( STDOUT_FILENO, bytes.data() + written, bytes.size() - written );
			//The reader has gone. SIGPIPE is ignored in main(), so this is
			//EPIPE and not a silent 141: say so and stop.
			if( put <= 0 )
			{
				std::fprintf( stderr, "stdout closed at frame %d\n", index );
				return 1;
			}
			written += static_cast< size_t >( put );
		}
	}
	return 0;
}

//===========================================================================
// The checks are below main's helpers; see checks().
//===========================================================================
struct Perturb
{
	int hooks    = 0;///< shaders::Hook bits
	int cpuHooks = 0;///< CpuHook bits
	int physics  = 0;///< PhysicsHook bits
	int model    = 0;///< --model's own wrong formula
};

using CheckFn = int ( * )( const Perturb& );

struct Raster
{
	int w, h;
};
std::vector< Raster > kRasters = { { 1280, 720 }, { 320, 180 } };

#include "checks.inc"

} // namespace

//---------------------------------------------------------------------------
int main( int argc, char** argv )
{
	g_argv0 = argv[ 0 ];
	//Nothing here asks the OS for its audio devices, unless --coreaudio's
	//child is told otherwise (it is the check that the gate holds).
	const bool coreaudioChild = argc >= 2 && std::strcmp( argv[ 1 ], "--coreaudio-child" ) == 0;
	if( !coreaudioChild )
		setenv( "SNOWGLOBE_NO_DEVICES", "1", 1 );
	std::string outPath = "/tmp/snowglobe.png";
	std::vector< std::string > settings;
	int width = 1280, height = 720, frames = 720;
	double fps = 60.0, beatBpm = 0.0;
	std::string mode, scriptPath, childMode;
	int filmFrames = -1;
	bool sizeGiven = false;
	AudioSource source = AudioSource::Music;
	std::map< int, std::vector< std::string > > later;//--at FRAME "Name=V"
	std::string clipPath;
	int debugHooks = 0;

	for( int i = 1; i < argc; ++i )
	{
		const std::string argument = argv[ i ];
		const bool hasNext         = i + 1 < argc;
		if( argument == "--help" || argument == "-h" )
		{
			std::printf( "sgtest -- render Snowglobe offline and measure what its flakes and its glass do\n\n"
			             "  --out PATH        render and write a PNG (default /tmp/snowglobe.png)\n"
			             "  --clip PATH       the clip: raw RGBA, top row first, at --size\n"
			             "  --size WxH        render size (default 1280x720)\n"
			             "  --frames N        frames before reading back (default 720: 12 s)\n"
			             "  --hooks N         shaders::Hook bits on the plain render\n"
			             "  --fps N           the synthetic clock's rate (default 60)\n"
			             "  --set \"Name=V\"    set a parameter by its display name (an option by its name). Repeatable.\n"
			             "  --at N \"Name=V\"  (with --out) set it at frame N instead; an event is released the frame after\n"
			             "  --silent          feed no music (default: the harness's kicks and hats through earshot's feed())\n"
			             "  --wav F.wav       (with --pipe/--film) feed this file instead, through earshot's feed() at its own rate\n"
			             "  --beat BPM        the host's beat: SetBeatInfo every frame (Trigger On = Beat to use it)\n"
			             "  --list            every parameter and its default\n"
			             "  --pipe            raw RGBA frames in on stdin, out on stdout\n"
			             "  --film N          N frames, raw RGBA on stdout\n"
			             "  --script PATH     cues for --pipe/--film: 'frame Name value'\n"
			             "  --negative        every negative control (each must make its check FAIL)\n"
			             "  --bench           GPU, CPU and wall time per frame at 720p and 1080p, each frame alone\n"
			             "  --offline         the checks and negative controls that need no GL context\n"
			             "  --all             every check\n\n"
			             "  checks, one flag each:" );
			for( const auto& check : checks() )
				std::printf( " --%s", check.flag );
			std::printf( "\n" );
			return 0;
		}
		else if( argument == "--coreaudio-child" && hasNext )
		{
			mode      = "coreaudio-child";
			childMode = argv[ ++i ];
		}
		else if( argument == "--out" && hasNext )
			outPath = argv[ ++i ];
		else if( argument == "--hooks" && hasNext )
			debugHooks = std::atoi( argv[ ++i ] );
		else if( argument == "--clip" && hasNext )
			clipPath = argv[ ++i ];
		else if( argument == "--set" && hasNext )
			settings.push_back( argv[ ++i ] );
		else if( argument == "--frames" && hasNext )
			frames = std::atoi( argv[ ++i ] );
		else if( argument == "--fps" && hasNext )
			fps = std::strtod( argv[ ++i ], nullptr );
		else if( argument == "--beat" && hasNext )
			beatBpm = std::strtod( argv[ ++i ], nullptr );
		else if( argument == "--silent" )
			source = AudioSource::Silent;
		else if( argument == "--wav" && hasNext )
		{
			const std::string path = argv[ ++i ];
			std::string error;
			if( !readWav( path, g_wav, error ) )
			{
				std::fprintf( stderr, "--wav: %s\n", error.c_str() );
				return 2;
			}
			source = AudioSource::Wav;
		}
		else if( argument == "--at" && i + 2 < argc )
		{
			const int at = std::atoi( argv[ ++i ] );
			later[ at ].push_back( argv[ ++i ] );
		}
		else if( argument == "--pipe" )
			mode = "pipe";
		else if( argument == "--film" && hasNext )
		{
			mode       = "film";
			filmFrames = std::max( 1, std::atoi( argv[ ++i ] ) );
		}
		else if( argument == "--script" && hasNext )
			scriptPath = argv[ ++i ];
		else if( argument == "--list" )
			mode = "list";
		else if( argument == "--size" && hasNext )
		{
			const std::string value = argv[ ++i ];
			const size_t cross      = value.find( 'x' );
			if( cross != std::string::npos )
			{
				width  = std::atoi( value.substr( 0, cross ).c_str() );
				height = std::atoi( value.substr( cross + 1 ).c_str() );
			}
			sizeGiven = true;
		}
		else if( argument.rfind( "--", 0 ) == 0 )
			mode = argument.substr( 2 );
		else
		{
			std::fprintf( stderr, "unknown argument '%s' (try --help)\n", argument.c_str() );
			return 2;
		}
	}
	if( width <= 0 || height <= 0 || !( fps > 0.0 ) || beatBpm < 0.0 )
	{
		std::fprintf( stderr, "--size and --fps must be positive\n" );
		return 2;
	}
	//A check given --size runs at that raster alone (the software pass).
	if( sizeGiven )
		kRasters = { { width, height } };

	if( mode == "list" )
	{
		SnowglobePlugin plugin;
		std::printf( "%-3s %-16s %-9s %s\n", "id", "name", "kind", "default" );
		for( const NamedParameter& parameter : listParameters( plugin ) )
			std::printf( "%-3u %-16s %-9s %.4f\n", parameter.index, parameter.name.c_str(), kindName( parameter.type ), parameter.value );
		return 0;
	}

	//A reader that hangs up must end --pipe/--film with exit 1 and a message,
	//not SIGPIPE's silent 141: ignored here, the write fails with EPIPE.
	std::signal( SIGPIPE, SIG_IGN );

	if( mode == "offline" )
		return runOffline();
	for( const CheckEntry& check : checks() )
		if( mode == check.flag && check.offline )
			return check.run( Perturb {} );

	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::fprintf( stderr, "could not create an OpenGL 4.1 core context\n" );
		return 1;
	}

	int result = 0;
	bool ran   = false;
	for( const CheckEntry& check : checks() )
		if( mode == check.flag )
		{
			result = check.run( Perturb {} );
			ran    = true;
		}

	if( ran )
		;
	else if( mode == "coreaudio-child" )
		result = runCoreaudioChild( childMode );
	else if( mode == "pipe" )
		result = runPipe( width, height, fps, scriptPath, 0, true, settings, source, beatBpm );
	else if( mode == "film" )
		result = runPipe( width, height, fps, scriptPath, filmFrames, false, settings, source, beatBpm );
	else if( mode == "negative" )
		result = runNegative();
	else if( mode.rfind( "negative-of=", 0 ) == 0 )
		result = runNegative( false, mode.substr( 12 ) );
	else if( mode == "bench" )
		result = runBench();
	else if( mode == "all" )
		result = runAll();
	else if( !mode.empty() )
	{
		std::fprintf( stderr, "unknown mode --%s (try --help)\n", mode.c_str() );
		result = 2;
	}
	else
	{
		Rig rig;
		rig.fps = fps;
		Floats clip;
		if( !clipPath.empty() )
		{
			std::ifstream file( clipPath, std::ios::binary );
			Bytes raw( static_cast< size_t >( width ) * height * 4 );
			if( !file.read( reinterpret_cast< char* >( raw.data() ), static_cast< std::streamsize >( raw.size() ) ) )
			{
				std::fprintf( stderr, "--clip %s: not %d x %d raw RGBA\n", clipPath.c_str(), width, height );
				return 2;
			}
			clip.resize( raw.size() );
			for( int y = 0; y < height; ++y )
				for( int x = 0; x < width * 4; ++x )
					clip[ static_cast< size_t >( height - 1 - y ) * width * 4 + x ] = raw[ static_cast< size_t >( y ) * width * 4 + x ] / 255.0f;
		}
		if( !rig.Init( width, height, clip.empty() ? nullptr : &clip ) )
			result = 1;
		else
		{
			rig.plugin.SetHooksForTest( debugHooks, 0, 0 );
			attachAudio( rig, source, beatBpm );
			for( const std::string& setting : settings )
			{
				std::string error;
				if( !applySetting( rig.plugin, setting, error ) )
				{
					std::fprintf( stderr, "--set %s: %s\n", setting.c_str(), error.c_str() );
					return 2;
				}
			}
			bool rendered = true;
			std::vector< std::pair< int, std::string > > release;
			for( int f = 0; f < std::max( frames, 1 ) && rendered; ++f )
			{
				for( const auto& r : release )
					if( r.first == f )
					{
						std::string ignored;
						applySetting( rig.plugin, r.second + "=0", ignored );
					}
				const auto due = later.find( f );
				if( due != later.end() )
					for( const std::string& setting : due->second )
					{
						std::string error;
						if( !applySetting( rig.plugin, setting, error ) )
						{
							std::fprintf( stderr, "--at %d %s: %s\n", f, setting.c_str(), error.c_str() );
							return 2;
						}
						const std::string name = setting.substr( 0, setting.find( '=' ) );
						for( const NamedParameter& parameter : listParameters( rig.plugin ) )
							if( parameter.name == name && parameter.type == FF_TYPE_EVENT )
								release.emplace_back( f + 1, name );
					}
				rendered = rig.Render( 1 );
			}
			if( !rendered )
				result = 1;
			else if( writePng( outPath, width, height, rig.Output() ) )
			{
				const FrameData& fd = rig.plugin.LastFrame();
				std::printf( "wrote %s -- %dx%d, %d frames at %g fps (%.2f s); %zu shakes; %d flakes drawn (%d behind the card), %d suspended\n",
				             outPath.c_str(), width, height, frames, fps, frames / fps, rig.plugin.ShakeLog().size(), fd.drawn, fd.behind,
				             fd.suspended );
			}
			else
				result = 1;
		}
	}

	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( context );
	return result;
}
