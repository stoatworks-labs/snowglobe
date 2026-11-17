#include "Globe.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace snowglobe
{
namespace
{
constexpr uint64_t kStartStream = 0xffffffffffffffffull;
constexpr double kTwoPi         = 2.0 * kPi;

uint64_t splitmix( uint64_t z )
{
	z += 0x9e3779b97f4a7c15ull;
	z = ( z ^ ( z >> 30 ) ) * 0xbf58476d1ce4e5b9ull;
	z = ( z ^ ( z >> 27 ) ) * 0x94d049bb133111ebull;
	return z ^ ( z >> 31 );
}

double length( V3 a )
{
	return std::sqrt( dot( a, a ) );
}

/// 3 j1(z) / z: 1 at the centre, 0 at the glass (z = x1).
double profile( double z )
{
	if( z < 1e-2 )
	{
		const double z2 = z * z;
		return 1.0 - z2 / 10.0 + z2 * z2 / 280.0;
	}
	return 3.0 * ( std::sin( z ) - z * std::cos( z ) ) / ( z * z * z );
}

/// A uniform unit vector from two uniforms.
V3 sphereDirection( double u, double v )
{
	const double y = 2.0 * u - 1.0;
	const double s = std::sqrt( std::max( 0.0, 1.0 - y * y ) );
	const double p = kTwoPi * v;
	return { s * std::cos( p ), y, s * std::sin( p ) };
}
} // namespace

double Hash01( uint64_t a, uint64_t b, uint64_t c )
{
	const uint64_t h = splitmix( splitmix( splitmix( a ) ^ b ) ^ c );
	return static_cast< double >( h >> 11 ) * ( 1.0 / 9007199254740992.0 );
}

double Globe::RadiusOf( int i, const Physics& p ) const
{
	return p.aMin + ( p.aMax - p.aMin ) * flakes[ static_cast< size_t >( i ) ].u;
}

double Globe::DecayRate( const Physics& p ) const
{
	const double k = ( hooks & kPhysWrongMode ) ? kPi / kRadius : kX1 / kRadius;
	return p.fill.nu * k * k;
}

V3 Globe::SettlingVelocity( int i, const Physics& p ) const
{
	const Flake& f    = flakes[ static_cast< size_t >( i ) ];
	const double r    = RadiusOf( i, p );
	const double mu   = ( hooks & kPhysWaterSettling ) ? CelsiusViscosityWater( kFillCelsius ) : p.fill.mu;
	const double drho = neutral ? 0.0 : kFlakeDensity - p.fill.rho;
	if( p.shape == Shape::Beads || ( hooks & kPhysSphereDiscs ) )
	{
		//Stokes: U = 2 drho g r^2 / (9 mu). The hook gives a disc its
		//equal-volume sphere (the wrong model --fall must catch).
		const double rs = p.shape == Shape::Beads ? r : std::cbrt( 0.75 * r * r * kFilmThickness );
		return { 0.0, -2.0 * drho * kGravity * rs * rs / ( 9.0 * mu ), 0.0 };
	}
	//A thin disc (Katz, citing Happel and Brenner, which was not read): mobility 1/(16 mu a) along the
	//normal, 3/(32 mu a) in its plane.
	const double weight = drho * kGravity * kPi * r * r * kFilmThickness;
	const double c      = weight / ( mu * r );
	const V3 g{ 0.0, -1.0, 0.0 };
	const double gn  = dot( f.n, g );
	const V3 along   = gn * f.n;
	const V3 inPlane = g - along;
	return ( c / 16.0 ) * along + ( 3.0 * c / 32.0 ) * inPlane;
}

V3 Globe::FluidVelocity( V3 x, V3 w, const Physics& ) const
{
	const double r = length( x );
	return profile( kX1 * r / kRadius ) * cross( w, x );
}

V3 Globe::velocity( int, V3 x, V3 w, const Physics& p, V3 vs ) const
{
	return FluidVelocity( x, w, p ) + vs;
}

bool Globe::land( Flake& f, double radius ) const
{
	const double limit = kRadius - radius;
	const double r     = length( f.x );
	if( r < limit )
		return false;
	const V3 out = ( 1.0 / r ) * f.x;
	f.x          = limit * out;
	f.n          = out;//lying on the glass
	f.resting    = true;
	return true;
}

void Globe::Start( const Physics& p )
{
	flakes.assign( kMaxFlakes, Flake{} );
	for( int i = 0; i < kMaxFlakes; ++i )
	{
		Flake& f       = flakes[ static_cast< size_t >( i ) ];
		const auto idx = static_cast< uint64_t >( i );
		f.u            = Hash01( kStartStream, idx, 0 );
		const double r = RadiusOf( i, p );
		const double L = kRadius - r;
		//A seeded point in the ball (rejection, deterministic), dropped
		//straight down onto the glass.
		double x = 0.0, z = 0.0;
		for( uint64_t k = 0; k < 64; ++k )
		{
			x = L * ( 2.0 * Hash01( kStartStream, idx, 1 + 2 * k ) - 1.0 );
			z = L * ( 2.0 * Hash01( kStartStream, idx, 2 + 2 * k ) - 1.0 );
			if( x * x + z * z < L * L )
				break;
			x = z = 0.0;
		}
		f.x       = { x, -std::sqrt( std::max( 0.0, L * L - x * x - z * z ) ), z };
		f.n       = ( 1.0 / std::max( length( f.x ), 1e-300 ) ) * f.x;
		f.resting = true;
	}
	swirl      = {};
	stateUs    = 0;
	shakeCount = 0;
	started    = true;
}

void Globe::PlaceForTest( int i, V3 x, V3 n, bool resting, double u )
{
	Flake& f  = flakes[ static_cast< size_t >( i ) ];
	f.x       = x;
	f.n       = n;
	f.resting = resting;
	f.u       = u;
}

void Globe::shake( double strength, const Physics& p )
{
	const uint64_t seq = shakeCount++;
	if( !( strength > 0.0 ) )
		return;
	const double S   = std::min( strength, 1.0 );
	const double phi = kTwoPi * Hash01( seq, kStartStream, 7 );
	const double amp = S * kOmegaMax * swirlGain;
	swirl            = swirl + V3{ amp * std::cos( phi ), 0.0, amp * std::sin( phi ) };
	const int count  = std::min( p.count, kMaxFlakes );
	for( int i = 0; i < count; ++i )
	{
		Flake& f = flakes[ static_cast< size_t >( i ) ];
		if( !f.resting )
			continue;
		const auto idx  = static_cast< uint64_t >( i );
		const double r  = RadiusOf( i, p );
		const double L  = kRadius - r;
		const double up = 2.0 * kRadius * S * liftGain * Hash01( seq, idx, 0 );
		const double top = std::sqrt( std::max( 0.0, L * L - f.x.x * f.x.x - f.x.z * f.x.z ) );
		f.x.y            = std::min( f.x.y + up, top * ( 1.0 - 1e-9 ) );
		f.n              = sphereDirection( Hash01( seq, idx, 1 ), Hash01( seq, idx, 2 ) );
		f.resting        = false;
	}
}

void Globe::step( double dt, const Physics& p )
{
	const double lambda = DecayRate( p );
	const int count     = std::min( p.count, kMaxFlakes );
	const V3 w0         = swirl;
	const V3 wh         = std::exp( -lambda * 0.5 * dt ) * w0;
	const V3 w1         = std::exp( -lambda * dt ) * w0;
	for( int i = 0; i < count; ++i )
	{
		Flake& f = flakes[ static_cast< size_t >( i ) ];
		if( f.resting )
		{
			if( hooks & kPhysSliding )
			{
				//The negative control: a flake creeps down the glass along
				//the tangential part of its settling.
				const V3 vs    = SettlingVelocity( i, p );
				const V3 out   = ( 1.0 / std::max( length( f.x ), 1e-300 ) ) * f.x;
				const V3 slide = vs - dot( vs, out ) * out;
				f.x            = f.x + dt * slide;
				f.x            = ( ( kRadius - RadiusOf( i, p ) ) / length( f.x ) ) * f.x;
			}
			continue;
		}
		const V3 vs = SettlingVelocity( i, p );
		const V3 x  = f.x;
		const V3 k1 = velocity( i, x, w0, p, vs );
		const V3 k2 = velocity( i, x + ( 0.5 * dt ) * k1, wh, p, vs );
		const V3 k3 = velocity( i, x + ( 0.5 * dt ) * k2, wh, p, vs );
		const V3 k4 = velocity( i, x + dt * k3, w1, p, vs );
		f.x         = x + ( dt / 6.0 ) * ( k1 + 2.0 * k2 + 2.0 * k3 + k4 );
		land( f, RadiusOf( i, p ) );
	}
	swirl = w1;
	if( length( swirl ) < kSwirlFloor )
		swirl = {};
}

void Globe::Advance( int64_t toUs, const std::vector< ShakeEvent >& shakes, const Physics& p )
{
	size_t k = 0;
	//Anything at or before the stored instant happens now.
	for( ; k < shakes.size() && shakes[ k ].us <= stateUs; ++k )
		shake( shakes[ k ].strength, p );
	for( int guard = 0; guard < 100000; ++guard )
	{
		const int64_t nextGrid  = ( stateUs / kStepUs + 1 ) * kStepUs;
		const int64_t nextEvent = k < shakes.size() ? shakes[ k ].us : INT64_MAX;
		const int64_t end       = std::min( nextGrid, nextEvent );
		if( end > toUs )
			break;
		step( static_cast< double >( end - stateUs ) * 1e-6, p );
		stateUs = end;
		for( ; k < shakes.size() && shakes[ k ].us <= stateUs; ++k )
			shake( shakes[ k ].strength, p );
	}
}

void Globe::Positions( int64_t atUs, const Physics& p, std::vector< V3 >& out ) const
{
	const int count = std::min( p.count, kMaxFlakes );
	out.resize( static_cast< size_t >( count ) );
	const double dt     = static_cast< double >( std::max< int64_t >( atUs - stateUs, 0 ) ) * 1e-6;
	const double lambda = DecayRate( p );
	const V3 w0         = swirl;
	const V3 wh         = std::exp( -lambda * 0.5 * dt ) * w0;
	const V3 w1         = std::exp( -lambda * dt ) * w0;
	for( int i = 0; i < count; ++i )
	{
		const Flake& f = flakes[ static_cast< size_t >( i ) ];
		if( f.resting || dt == 0.0 )
		{
			out[ static_cast< size_t >( i ) ] = f.x;
			continue;
		}
		const V3 vs = SettlingVelocity( i, p );
		const V3 x  = f.x;
		const V3 k1 = velocity( i, x, w0, p, vs );
		const V3 k2 = velocity( i, x + ( 0.5 * dt ) * k1, wh, p, vs );
		const V3 k3 = velocity( i, x + ( 0.5 * dt ) * k2, wh, p, vs );
		const V3 k4 = velocity( i, x + dt * k3, w1, p, vs );
		Flake moved = f;
		moved.x     = x + ( dt / 6.0 ) * ( k1 + 2.0 * k2 + 2.0 * k3 + k4 );
		land( moved, RadiusOf( i, p ) );
		out[ static_cast< size_t >( i ) ] = moved.x;
	}
}

int Globe::Suspended( int count ) const
{
	int n = 0;
	for( int i = 0; i < std::min( count, static_cast< int >( flakes.size() ) ); ++i )
		n += flakes[ static_cast< size_t >( i ) ].resting ? 0 : 1;
	return n;
}

uint64_t Globe::Hash( int count ) const
{
	uint64_t h = 1469598103934665603ull;
	auto eat   = [ &h ]( const void* data, size_t n ) {
        const auto* b = static_cast< const unsigned char* >( data );
        for( size_t i = 0; i < n; ++i )
        {
            h ^= b[ i ];
            h *= 1099511628211ull;
        }
	};
	for( int i = 0; i < std::min( count, static_cast< int >( flakes.size() ) ); ++i )
	{
		const Flake& f = flakes[ static_cast< size_t >( i ) ];
		eat( &f.x, sizeof f.x );
		eat( &f.n, sizeof f.n );
		eat( &f.u, sizeof f.u );
		const unsigned char rest = f.resting ? 1 : 0;
		eat( &rest, 1 );
	}
	eat( &swirl, sizeof swirl );
	return h;
}

} // namespace snowglobe
