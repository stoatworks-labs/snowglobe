#include "Optics.h"

#include <algorithm>
#include <cmath>

namespace snowglobe
{
namespace
{
/// Forward-mode derivative: value and d/dxi.
struct Dual
{
	double v, d;
};
Dual operator+( Dual a, Dual b )
{
	return { a.v + b.v, a.d + b.d };
}
Dual operator-( Dual a, Dual b )
{
	return { a.v - b.v, a.d - b.d };
}
Dual operator*( Dual a, Dual b )
{
	return { a.v * b.v, a.d * b.v + a.v * b.d };
}
Dual operator/( Dual a, Dual b )
{
	return { a.v / b.v, ( a.d * b.v - a.v * b.d ) / ( b.v * b.v ) };
}
Dual operator*( double s, Dual a )
{
	return { s * a.v, s * a.d };
}
Dual dsqrt( Dual a )
{
	const double s = std::sqrt( std::max( a.v, 0.0 ) );
	return { s, s > 0.0 ? 0.5 * a.d / s : 0.0 };
}
Dual constant( double v )
{
	return { v, 0.0 };
}

/// The meridional plane (rho, z): the ray at screen tangent xi, refracted at
/// the sphere, evaluated at depth zp. Returns rho there and the ray's slope
/// d rho / d z inside.
struct Inside
{
	Dual rho;
	double slope;
	bool ok;
};
Inside meridional( double xi, double zp, double n, bool refract )
{
	const double D = kViewDistance;
	const Dual x   = { xi, 1.0 };
	const Dual L   = dsqrt( constant( 1.0 ) + x * x );
	const Dual dr  = x / L, dz = constant( -1.0 ) / L;
	//|E + s d|^2 = 1, E = (0, D): s^2 + 2 s D dz + D^2 - 1 = 0
	const Dual b    = D * dz;
	const Dual disc = b * b - constant( D * D - 1.0 );
	if( disc.v < 0.0 )
		return { constant( 0.0 ), 0.0, false };
	const Dual s  = constant( 0.0 ) - b - dsqrt( disc );
	const Dual Sr = s * dr, Sz = constant( D ) + s * dz;
	Dual tr = dr, tz = dz;
	if( refract )
	{
		const double eta = 1.0 / n;
		const Dual c1    = constant( 0.0 ) - ( dr * Sr + dz * Sz );
		const Dual k     = constant( 1.0 ) - eta * eta * ( constant( 1.0 ) - c1 * c1 );
		const Dual m     = eta * c1 - dsqrt( k );
		tr               = eta * dr + m * Sr;
		tz               = eta * dz + m * Sz;
	}
	if( tz.v >= 0.0 )
		return { constant( 0.0 ), 0.0, false };
	const Dual rho = Sr + tr * ( constant( zp ) - Sz ) / tz;
	return { rho, tr.v / tz.v, true };
}
} // namespace

double SilhouetteTangent()
{
	return 1.0 / std::sqrt( kViewDistance * kViewDistance - 1.0 );
}

Projection ProjectPoint( const double P[ 3 ], double n, bool refract )
{
	Projection out;
	const double D   = kViewDistance;
	const double rho = std::sqrt( P[ 0 ] * P[ 0 ] + P[ 1 ] * P[ 1 ] );
	const double zp  = P[ 2 ];
	const double nn  = refract ? n : 1.0;
	//The paraxial image (OpenStax 2.11 at the front vertex): s from the
	//vertex in the fill, 1/s' = (n - 1)/R - n/s, m = -n s'/s.
	const double sObj = 1.0 - zp;
	double xi         = 0.0;
	{
		const double inv = ( nn - 1.0 ) - nn / sObj;
		const double sImg = std::fabs( inv ) > 1e-12 ? 1.0 / inv : -1e12;
		const double m    = -nn * sImg / sObj;
		const double zImg = 1.0 + sImg;
		xi                = m * rho / std::max( D - zImg, 1e-6 );
	}
	const double xiMax = SilhouetteTangent() * ( 1.0 - 1e-12 );
	xi                 = std::clamp( xi, 0.0, xiMax );
	double slope       = 0.0;
	Dual g{ 0.0, 0.0 };
	bool ok = false;
	for( int it = 0; it < 8; ++it )
	{
		const Inside in = meridional( xi, zp, nn, refract );
		if( !in.ok )
		{
			xi *= 0.5;
			continue;
		}
		g     = in.rho - constant( rho );
		slope = in.slope;
		ok    = true;
		if( g.d == 0.0 )
			break;
		xi = std::clamp( xi - g.v / g.d, 0.0, xiMax );
	}
	if( !ok )
		return out;
	const Inside fin = meridional( xi, zp, nn, refract );
	if( !fin.ok || std::fabs( fin.rho.v - rho ) > 1e-9 || fin.rho.d == 0.0 )
		return out;
	//Implicit derivatives of G(xi; rho, z) = rho_at(z; xi) - rho = 0:
	//dxi/drho = 1 / G_xi, dxi/dz = -slope / G_xi.
	const double dXiDRho = 1.0 / fin.rho.d;
	const double dXiDz   = -fin.slope / fin.rho.d;
	(void)slope;
	if( rho < 1e-12 )
	{
		out.xi[ 0 ] = out.xi[ 1 ] = 0.0;
		//On the axis: the transverse scale is dxi/drho in every direction.
		out.J[ 0 ][ 0 ] = out.J[ 1 ][ 1 ] = dXiDRho;
		out.seen                          = true;
		return out;
	}
	const double ex = P[ 0 ] / rho, ey = P[ 1 ] / rho;
	out.xi[ 0 ] = xi * ex;
	out.xi[ 1 ] = xi * ey;
	//xi_vec = xi(rho, z) e: radial part dXiDRho e e^T, tangential xi/rho (I - e e^T).
	const double t  = xi / rho;
	out.J[ 0 ][ 0 ] = dXiDRho * ex * ex + t * ( 1.0 - ex * ex );
	out.J[ 0 ][ 1 ] = dXiDRho * ex * ey - t * ex * ey;
	out.J[ 1 ][ 0 ] = out.J[ 0 ][ 1 ];
	out.J[ 1 ][ 1 ] = dXiDRho * ey * ey + t * ( 1.0 - ey * ey );
	out.J[ 0 ][ 2 ] = dXiDz * ex;
	out.J[ 1 ][ 2 ] = dXiDz * ey;
	out.seen        = true;
	return out;
}

bool TraceToPlane( double xiX, double xiY, double n, double zPlane, double out[ 2 ] )
{
	const double D = kViewDistance;
	double d[ 3 ]  = { xiX, xiY, -1.0 };
	const double L = std::sqrt( d[ 0 ] * d[ 0 ] + d[ 1 ] * d[ 1 ] + 1.0 );
	for( double& c : d )
		c /= L;
	const double b    = D * d[ 2 ];
	const double disc = b * b - ( D * D - 1.0 );
	if( disc < 0.0 )
		return false;
	const double s    = -b - std::sqrt( disc );
	const double S[3] = { s * d[ 0 ], s * d[ 1 ], D + s * d[ 2 ] };
	const double eta  = 1.0 / n;
	const double c1   = -( d[ 0 ] * S[ 0 ] + d[ 1 ] * S[ 1 ] + d[ 2 ] * S[ 2 ] );
	const double k    = 1.0 - eta * eta * ( 1.0 - c1 * c1 );
	const double m    = eta * c1 - std::sqrt( std::max( k, 0.0 ) );
	double t[ 3 ];
	for( int i = 0; i < 3; ++i )
		t[ i ] = eta * d[ i ] + m * S[ i ];
	if( t[ 2 ] >= 0.0 )
		return false;
	const double q = ( zPlane - S[ 2 ] ) / t[ 2 ];
	out[ 0 ]       = S[ 0 ] + q * t[ 0 ];
	out[ 1 ]       = S[ 1 ] + q * t[ 1 ];
	return true;
}

} // namespace snowglobe
