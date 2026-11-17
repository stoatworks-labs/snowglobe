#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace snowglobe
{
namespace
{
double clamp01( float v )
{
	if( !( v == v ) )
		return 0.0;
	return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
}
double geometric( float v, double low, double high )
{
	return low * std::pow( high / low, clamp01( v ) );
}
float inverseGeometric( double value, double low, double high )
{
	const double lo = std::min( low, high ), hi = std::max( low, high );
	return static_cast< float >( std::log( std::clamp( value, lo, hi ) / low ) / std::log( high / low ) );
}
float inverseLinear( double value, double low, double high )
{
	return static_cast< float >( std::clamp( ( value - low ) / ( high - low ), 0.0, 1.0 ) );
}

const char* const kTriggerNames[] = { "Kick", "Beat", "Button" };
const char* const kShapeNames[]   = { "Flakes", "Beads" };

ControlInfo standard( unsigned int id, const char* name, const char* group, float value )
{
	return { id, name, group, Kind::Standard, value, nullptr, 0 };
}
ControlInfo option( unsigned int id, const char* name, const char* group, float value, const char* const* names, int count )
{
	return { id, name, group, Kind::Option, value, names, count };
}
ControlInfo event( unsigned int id, const char* name, const char* group )
{
	return { id, name, group, Kind::Event, 0.0f, nullptr, 0 };
}

//---------------------------------------------------------------------------
// The defaults: chosen (AGENTS.md, "Defaults", says what clips.py measured).
//---------------------------------------------------------------------------
const ControlInfo kTable[] = {
	standard( PT_SHAKE, "Shake", "Shake", 0.3f ),
	option( PT_TRIGGER_ON, "Trigger On", "Shake", static_cast< float >( TriggerOn::Kick ), kTriggerNames, static_cast< int >( TriggerOn::Count ) ),
	{ PT_INPUT, "Audio Input", "Shake", Kind::Devices, 0.0f, nullptr, 0 },
	event( PT_SHAKE_NOW, "Shake Now", "Shake" ),

	standard( PT_FILL, "Fill", "Globe", 0.0f ),
	option( PT_SNOW, "Snow", "Flakes", static_cast< float >( Shape::Flakes ), kShapeNames, static_cast< int >( Shape::Count ) ),
	standard( PT_SIZE, "Size", "Flakes", ParamFromSize( 1.0e-3, Shape::Flakes ) ),
	standard( PT_RANGE, "Range", "Flakes", ParamFromRange( 0.5 ) ),
	standard( PT_COUNT_FLAKES, "Count", "Flakes", ParamFromCount( 600 ) ),
	standard( PT_GLOBE, "Globe", "Globe", ParamFromGlobe( 0.9 ) ),
	standard( PT_MIX, "Mix", "Globe", 1.0f ),
	{ PT_AUDIO, "Audio", "Shake", Kind::Buffer, 0.0f, nullptr, 0 },
};
static_assert( sizeof( kTable ) / sizeof( kTable[ 0 ] ) == PT_ABOUT_TEXT, "every control needs a row in the table" );
} // namespace

const ControlInfo& InfoOf( unsigned int id )
{
	return kTable[ std::min< unsigned int >( id, PT_ABOUT_TEXT - 1 ) ];
}

const std::vector< unsigned int >& HostOrder()
{
	static const std::vector< unsigned int > order = [] {
		std::vector< unsigned int > o;
		for( unsigned int id = 0; id < PT_COUNT; ++id )
			o.push_back( id );
		return o;
	}();
	return order;
}

int OptionIndex( float value, int count )
{
	if( !( value == value ) )
		return 0;
	const long i = std::lround( value );
	return static_cast< int >( std::clamp< long >( i, 0, count - 1 ) );
}

double FillFromParam( float value )
{
	return kFillLow + ( kFillHigh - kFillLow ) * clamp01( value );
}
float ParamFromFill( double w )
{
	return inverseLinear( w, kFillLow, kFillHigh );
}

double SizeFromParam( float value, Shape shape )
{
	return shape == Shape::Beads ? geometric( value, kBeadLow, kBeadHigh ) : geometric( value, kDiscLow, kDiscHigh );
}
float ParamFromSize( double radius, Shape shape )
{
	return shape == Shape::Beads ? inverseGeometric( radius, kBeadLow, kBeadHigh ) : inverseGeometric( radius, kDiscLow, kDiscHigh );
}

double RangeFromParam( float value )
{
	return kRangeLow + ( 1.0 - kRangeLow ) * clamp01( value );
}
float ParamFromRange( double ratio )
{
	return inverseLinear( ratio, kRangeLow, 1.0 );
}

int CountFromParam( float value )
{
	return static_cast< int >( std::lround( geometric( value, kCountLow, kCountHigh ) ) );
}
float ParamFromCount( int count )
{
	return inverseGeometric( static_cast< double >( count ), kCountLow, kCountHigh );
}

double GlobeFromParam( float value )
{
	return kGlobeLow + ( kGlobeHigh - kGlobeLow ) * clamp01( value );
}
float ParamFromGlobe( double fraction )
{
	return inverseLinear( fraction, kGlobeLow, kGlobeHigh );
}

double ShakeFromParam( float value )
{
	return clamp01( value );
}

} // namespace snowglobe
