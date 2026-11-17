#include "Fill.h"

#include <cmath>

namespace snowglobe
{
double CelsiusViscosityWater( double t )
{
	return 1.790e-3 * std::exp( ( -1230.0 - t ) * t / ( 36100.0 + 360.0 * t ) );
}

double CelsiusViscosityGlycerol( double t )
{
	return 12100.0e-3 * std::exp( ( -1233.0 + t ) * t / ( 9900.0 + 70.0 * t ) );
}

double ChengViscosity( double w, double t )
{
	const double a     = 0.705 - 0.0017 * t;
	const double b     = ( 4.9 + 0.036 * t ) * std::pow( a, 2.5 );
	const double alpha = 1.0 - w + ( a * b * w * ( 1.0 - w ) ) / ( a * w + b * ( 1.0 - w ) );
	const double muW = CelsiusViscosityWater( t ), muG = CelsiusViscosityGlycerol( t );
	return muG * std::exp( alpha * std::log( muW / muG ) );
}

double VolkDensity( double w, double t )
{
	const double c           = 1.78e-6 * t * t - 1.82e-4 * t + 1.41e-2;
	const double contraction = w > 0.0 ? 1.0 + c * std::pow( std::sin( std::pow( w, 1.31 ) * 3.14159265358979323846 ), 0.81 ) : 1.0;
	const double rhoW        = 1000.0 * ( 1.0 - std::pow( std::fabs( ( t - 3.98 ) / 615.0 ), 1.71 ) );
	const double rhoG        = 1273.0 - 0.612 * t;
	if( w <= 0.0 )
		return rhoW;
	return contraction * ( rhoW + ( rhoG - rhoW ) / ( 1.0 + rhoG / rhoW * ( 1.0 / w - 1.0 ) ) );
}

double HoytIndex( double w )
{
	const double x = 100.0 * w;
	if( x < 80.0 )
		return 0.00149 * x + 1.32359;
	return 0.90799 + 0.0154 * x - 0.000155 * x * x + 0.000000576 * x * x * x;
}

FillProps FillAt( double w )
{
	FillProps f;
	f.w   = w;
	f.rho = VolkDensity( w, kFillCelsius );
	f.mu  = ChengViscosity( w, kFillCelsius );
	f.nu  = f.mu / f.rho;
	f.n   = HoytIndex( w );
	return f;
}

} // namespace snowglobe
