#pragma once

#include "StoatworksAboutLinks.h"

#include <vector>

/**
    The host's parameters, and what they mean in globe units.

    Every ranged parameter the host sees is 0..1, because `SetParamInfo`
    clamps an `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange`
    could widen it. The conversions live in Controls.cpp, one function per
    control, each with its inverse where a default or the harness needs it.
    Option parameters hold the element index (an option reads back 0..1
    whatever its count: map by index), except Audio Input, whose elements'
    values are earshot device keys (a device is remembered by name). Shake Now
    is a press; Audio is Resolume's 64-bin FFT buffer, the kick detector's
    input when no device is chosen.

    Units: metres and seconds (SPEC-snowglobe.md, "The model").
*/
namespace snowglobe
{
enum ParamId : unsigned int
{
	PT_SHAKE = 0,
	PT_TRIGGER_ON,
	PT_INPUT,///< the device menu
	PT_SHAKE_NOW,

	PT_FILL,
	PT_SNOW,
	PT_SIZE,
	PT_RANGE,
	PT_COUNT_FLAKES,
	PT_GLOBE,
	PT_MIX,
	PT_AUDIO,

	PT_ABOUT_TEXT,
	PT_COUNT = PT_ABOUT_TEXT + 1 + stoatworks::about::kButtonCount
};

enum class Kind
{
	Standard,
	Option,
	Event,
	Buffer,
	Devices
};

struct ControlInfo
{
	unsigned int id;
	const char* name;///< at most 16 characters: FFGL's name field is not terminated
	const char* group;
	Kind kind;
	float defaultValue;///< the host's 0..1, or the element index
	const char* const* options;
	int optionCount;
};

const ControlInfo& InfoOf( unsigned int id );

/// Host index -> ParamId (one plugin, so the table's order).
const std::vector< unsigned int >& HostOrder();

/// An option's element index from whatever the host sent (index, rounded).
int OptionIndex( float value, int count );

/// The device menu: element 0 Resolume's FFT, element 1 the system default,
/// then devices by name, then spares, kInputSlots in all (marbling's shape).
/// Nothing is listed until Trigger On is Kick (SPEC-snowglobe.md, Triggers).
constexpr int kInputSlots           = 16;
constexpr const char* kHostFftLabel = "Resolume FFT";
constexpr const char* kDefaultLabel = "System default";
constexpr const char* kEmptySlot    = "(none)";

enum class TriggerOn
{
	Kick = 0,
	Beat,
	Button,
	Count
};

enum class Shape
{
	Flakes = 0,///< thin PET discs, 48 um
	Beads,     ///< PET spheres
	Count
};

/// The ranges (SPEC-snowglobe.md, "The fill", "The flakes").
constexpr double kFillLow = 0.60, kFillHigh = 0.80;  ///< glycerol mass fraction
constexpr double kDiscLow = 0.30e-3, kDiscHigh = 1.25e-3;///< largest disc radius, m
constexpr double kBeadLow = 0.08e-3, kBeadHigh = 0.30e-3;///< largest bead radius, m
constexpr double kRangeLow = 0.25;                   ///< smallest / largest
constexpr int kCountLow = 50, kCountHigh = 3000;     ///< flakes (the cap)
constexpr double kGlobeLow = 0.25, kGlobeHigh = 1.0; ///< silhouette diameter / frame height

double FillFromParam( float value );///< w, glycerol mass fraction
float ParamFromFill( double w );

double SizeFromParam( float value, Shape shape );///< the largest radius, m
float ParamFromSize( double radius, Shape shape );

double RangeFromParam( float value );///< smallest / largest radius
float ParamFromRange( double ratio );

int CountFromParam( float value );///< flakes
float ParamFromCount( int count );

double GlobeFromParam( float value );///< silhouette diameter / frame height
float ParamFromGlobe( double fraction );

double ShakeFromParam( float value );///< S per kick or beat, 0..1

constexpr double kPi = 3.14159265358979323846;

} // namespace snowglobe
