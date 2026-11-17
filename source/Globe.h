#pragma once

#include "Controls.h"
#include "Fill.h"

#include <cstdint>
#include <vector>

/**
    The globe's state: the flakes and the swirl, on the CPU in double
    (SPEC-snowglobe.md, "State and determinism").

    Units: metres, seconds; the centre is the origin, y up, z toward the
    viewer. Time is integer microseconds from the plugin's first frame. The
    physics steps on a fixed 8 ms grid; an event inside a step splits it at
    the event's microsecond. What a frame shows is the stored state advanced
    to the frame's instant by one partial step that is not stored, so the
    state at an instant depends on the events' instants and nothing else.
*/
namespace snowglobe
{
constexpr double kRadius        = 0.040;               ///< R, the glass's inside, m (chosen)
constexpr double kViewDistance  = 10.0;                ///< D / R (chosen: reading distance)
constexpr double kFlakeDensity  = 1380.0;              ///< PET, kg/m^3
constexpr double kFilmThickness = 48e-6;               ///< the disc film, m (Geoglit's 48 um foil)
constexpr double kGravity       = 9.80665;             ///< m/s^2
constexpr double kOmegaMax      = 2.0;                 ///< rad/s at the centre per full shake (chosen)
constexpr double kX1            = 4.4934094579090641753;///< the first root of tan x = x
constexpr int64_t kStepUs       = 8000;                ///< the physics grid
constexpr int kMaxFlakes        = kCountHigh;
constexpr double kSwirlFloor    = 1e-9;                ///< |W| below this is flushed to 0

struct V3
{
	double x = 0.0, y = 0.0, z = 0.0;
};
inline V3 operator+( V3 a, V3 b )
{
	return { a.x + b.x, a.y + b.y, a.z + b.z };
}
inline V3 operator-( V3 a, V3 b )
{
	return { a.x - b.x, a.y - b.y, a.z - b.z };
}
inline V3 operator*( double s, V3 a )
{
	return { s * a.x, s * a.y, s * a.z };
}
inline double dot( V3 a, V3 b )
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline V3 cross( V3 a, V3 b )
{
	return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

struct Flake
{
	V3 x;              ///< centre, m
	V3 n{ 0, 1, 0 };   ///< a disc's normal (unit); a bead ignores it
	double u     = 0.0;///< the seeded size fraction, [0, 1)
	bool resting = true;
};

/// What the physics reads from the controls, per frame.
struct Physics
{
	FillProps fill = FillAt( 0.6 );
	Shape shape    = Shape::Flakes;
	double aMax    = 1.0e-3;///< m
	double aMin    = 0.5e-3;///< m
	int count      = 600;
};

struct ShakeEvent
{
	int64_t us;     ///< on the plugin's clock
	double strength;///< S, 0..1
};

/// CPU negative controls the physics honours (the harness's hooks).
enum PhysicsHook : int
{
	kPhysSphereDiscs   = 1 << 0,///< discs fall at their equal-volume sphere's Stokes speed
	kPhysSliding       = 1 << 1,///< resting flakes slide down the glass
	kPhysWaterSettling = 1 << 2,///< settling speeds use water's viscosity
	kPhysWrongMode     = 1 << 3 ///< the swirl decays with k = pi / R
};

class Globe
{
public:
	/// Settled: every flake dropped from a seeded point to the glass.
	void Start( const Physics& physics );
	bool Started() const
	{
		return started;
	}

	/// Advance the stored state to the last grid point or event at or before
	/// `toUs`, applying `shakes` (sorted, none before the stored instant) at
	/// their instants.
	void Advance( int64_t toUs, const std::vector< ShakeEvent >& shakes, const Physics& physics );

	/// The flakes' centres at `atUs` (>= the stored instant), not stored.
	void Positions( int64_t atUs, const Physics& physics, std::vector< V3 >& out ) const;

	double RadiusOf( int i, const Physics& physics ) const;
	V3 SettlingVelocity( int i, const Physics& physics ) const;
	V3 FluidVelocity( V3 x, V3 w, const Physics& physics ) const;

	const std::vector< Flake >& Flakes() const
	{
		return flakes;
	}
	V3 Swirl() const
	{
		return swirl;
	}
	int64_t StateUs() const
	{
		return stateUs;
	}
	uint64_t ShakeCount() const
	{
		return shakeCount;
	}
	int Suspended( int count ) const;
	/// FNV-1a over the state the picture depends on.
	uint64_t Hash( int count ) const;

	//-- Harness --------------------------------------------------------------
	void SetHooks( int bits )
	{
		hooks = bits;
	}
	void SetNeutralForTest( bool on )
	{
		neutral = on;
	}
	void SetSwirlGainForTest( double g )
	{
		swirlGain = g;
	}
	void SetLiftGainForTest( double g )
	{
		liftGain = g;
	}
	void PlaceForTest( int i, V3 x, V3 n, bool resting, double u );
	void SetSwirlForTest( V3 w )
	{
		swirl = w;
	}
	double DecayRate( const Physics& physics ) const;///< 1 / tau

private:
	void step( double dt, const Physics& physics );
	void shake( double strength, const Physics& physics );
	V3 velocity( int i, V3 x, V3 w, const Physics& physics, V3 vs ) const;
	bool land( Flake& f, double radius ) const;

	std::vector< Flake > flakes;
	V3 swirl;
	int64_t stateUs     = 0;
	uint64_t shakeCount = 0;
	bool started        = false;
	int hooks           = 0;
	bool neutral        = false;
	double swirlGain    = 1.0;
	double liftGain     = 1.0;
};

/// The seeded streams (SplitMix64): [0, 1).
double Hash01( uint64_t a, uint64_t b, uint64_t c );

} // namespace snowglobe
