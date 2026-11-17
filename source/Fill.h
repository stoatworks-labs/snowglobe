#pragma once

/**
    The fill: water and glycerol at 20 C (SPEC-snowglobe.md, "The fill").

    - viscosity: Cheng (2008), Ind. Eng. Chem. Res. 47(9) 3285, the mixing
      rule mu = mu_w^alpha mu_g^(1 - alpha); within 3.5 % of Segur and
      Oberstar's measurements by Cheng's own count;
    - density: Volk and Kahler (2018), Exp. Fluids 59:75 (the authors' MATLAB),
      within 0.07 % for 15-30 C;
    - refractive index n_D: Hoyt's fits (Ind. Eng. Chem. 26, 329 (1934)) as
      printed in the GPA's "Physical Properties of Glycerine and its
      Solutions" (1963), Table 23: linear for 45-79 %, the cubic for 80-100 %,
      within 0.00011 of the observed.
*/
namespace snowglobe
{
struct FillProps
{
	double w   = 0.6;///< glycerol mass fraction
	double rho = 0.0;///< kg/m^3
	double mu  = 0.0;///< Pa s
	double nu  = 0.0;///< m^2/s
	double n   = 1.0;///< refractive index, sodium D
};

constexpr double kFillCelsius = 20.0;

double CelsiusViscosityWater( double t );   ///< Pa s, Cheng's eq. for water
double CelsiusViscosityGlycerol( double t );///< Pa s, Cheng's eq. for glycerol
double ChengViscosity( double w, double t );///< Pa s
double VolkDensity( double w, double t );   ///< kg/m^3
double HoytIndex( double w );               ///< n_D at 20 C, 0.45 <= w <= 1

FillProps FillAt( double w );

} // namespace snowglobe
