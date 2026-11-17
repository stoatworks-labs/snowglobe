#pragma once

#include "Globe.h"

/**
    The flakes through the globe (SPEC-snowglobe.md, "Flakes through the same
    refraction"). Units of R; the pinhole at (0, 0, D) looking down -z; the
    screen coordinate of a ray is its tangent, xi = (dx, dy) / -dz. The
    fragment shader traces the other way (pixel to card) with the same
    surface; this solves, for a point inside, which ray reaches it.
*/
namespace snowglobe
{
struct Projection
{
	double xi[ 2 ] = { 0.0, 0.0 };///< screen tangent
	double J[ 2 ][ 3 ] = {};       ///< d xi / d point (units of R)
	bool seen = false;            ///< a ray from the pinhole reaches the point
};

/// tan of the silhouette's half-angle: 1 / sqrt(D^2 - 1).
double SilhouetteTangent();

/// The point P (units of R, inside the sphere) seen through a fill of index
/// n. Newton on the meridional plane's one unknown, from the paraxial image,
/// a fixed eight iterations: the result does not depend on any earlier frame.
/// `refract` false: straight lines (the harness's negative control).
Projection ProjectPoint( const double P[ 3 ], double n, bool refract = true );

/// Where the ray through screen tangent `xi` meets the plane z = zPlane
/// (units of R), as the shader computes it but in double. False if it misses.
bool TraceToPlane( double xiX, double xiY, double n, double zPlane, double out[ 2 ] );

} // namespace snowglobe
