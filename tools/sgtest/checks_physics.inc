//===========================================================================
// The shared rig set-up, the harness's own physics and optics (written here,
// not taken from the plugin), and the flakes' checks: --fall, --settle,
// --swirl, --still.
//===========================================================================

bool startRig( Rig& rig, int w, int h, const Perturb& perturb, const Floats* picture = nullptr )
{
	if( !rig.Init( w, h, picture ) )
		return false;
	rig.plugin.SetHooksForTest( perturb.hooks, perturb.cpuHooks, perturb.physics );
	//Every control a check depends on, set (honeydew's lesson): no kicks, the
	//button only; full Mix; the defaults' globe.
	rig.Set( PT_TRIGGER_ON, static_cast< float >( TriggerOn::Button ) );
	rig.Set( PT_MIX, 1.0f );
	rig.Set( PT_GLOBE, ParamFromGlobe( 0.9 ) );
	rig.Set( PT_SHAKE, 0.3f );
	rig.Set( PT_FILL, 0.0f );
	rig.Set( PT_SNOW, static_cast< float >( Shape::Flakes ) );
	rig.Set( PT_SIZE, ParamFromSize( 1.0e-3, Shape::Flakes ) );
	rig.Set( PT_RANGE, ParamFromRange( 0.5 ) );
	rig.Set( PT_COUNT_FLAKES, ParamFromCount( 600 ) );
	return true;
}

//---------------------------------------------------------------------------
// The GPA's tables (1963), as printed: what the measured speeds are held to.
// Table 4: density at 20 C, g/ml. Table 17 (Segur and Oberstar 1951): cP at
// 20 C. Table 23 (Hoyt 1934): n_D at 20.0 C, per 1 %, 60-80 %.
//---------------------------------------------------------------------------
struct TableRow
{
	double w, rho, mu;
};
const TableRow kGpaRows[] = {
	{ 0.60, 1153.80, 10.8e-3 },
	{ 0.65, 1167.25, 15.2e-3 },
	{ 0.70, 1181.25, 22.5e-3 },
	{ 0.75, 1194.95, 35.5e-3 },
	{ 0.80, 1208.50, 60.1e-3 },
};
const double kGpaIndex[ 21 ] = { 1.41299, 1.41448, 1.41597, 1.41746, 1.41895, 1.42044, 1.42193, 1.42342, 1.42491, 1.42640, 1.42789,
	                             1.42938, 1.43087, 1.43236, 1.43385, 1.43534, 1.43683, 1.43832, 1.43982, 1.44135, 1.44290 };
/// Cheng's own count of his formula against Segur and Oberstar: 3.5 % at most.
constexpr double kChengError = 0.035;
/// Volk's density model against Bosart and Snoddy, 15-30 C: 0.07 % at most.
constexpr double kVolkError = 0.0007;

const TableRow* gpaRow( double w )
{
	for( const TableRow& r : kGpaRows )
		if( std::fabs( r.w - w ) < 1e-9 )
			return &r;
	return nullptr;
}
double gpaIndex( double w )
{
	const int k = static_cast< int >( std::lround( ( w - 0.60 ) * 100.0 ) );
	return kGpaIndex[ std::clamp( k, 0, 20 ) ];
}

/// The terminal velocity the harness expects, from the tables and Katz's /
/// Stokes's laws, for a flake of radius r (m) and unit normal n.
V3 expectedVelocity( Shape shape, double r, V3 n, double rhoFluid, double mu )
{
	const double drho = 1380.0 - rhoFluid;
	const double g    = 9.80665;
	if( shape == Shape::Beads )
		return { 0.0, -2.0 * drho * g * r * r / ( 9.0 * mu ), 0.0 };
	const double weight = drho * g * kPi * r * r * 48e-6;
	//Broadside 16 mu a, edgewise (32/3) mu a: velocity = F n (n.F)/(16 mu a) + F_inplane / ((32/3) mu a).
	const double fn = -weight * n.y;
	const V3 along  = fn * n;
	const V3 inPl   = V3{ 0.0, -weight, 0.0 } - along;
	return ( 1.0 / ( 16.0 * mu * r ) ) * along + ( 3.0 / ( 32.0 * mu * r ) ) * inPl;
}

//---------------------------------------------------------------------------
// The harness's ray trace: the meridional plane, angles, Snell's law as
// sin r = sin i / n, long double; nothing shared with the shader's vector
// form or Optics.cpp's Newton. The pinhole at distance D = 10 R on the axis.
// A ray at angle alpha to the axis meets the unit sphere at incidence i with
// sin i = D sin alpha, at polar angle phi = i - alpha; refracted, it leaves
// the entry point at angle r - phi to the axis, so at depth z it is at
// rho = sin phi + tan(r - phi)(cos phi - z).
//---------------------------------------------------------------------------
using LD = long double;
constexpr LD kD = 10.0L;

LD silhouetteTangentLD()
{
	return std::tan( std::asin( 1.0L / kD ) );
}

bool rhoAt( LD alpha, LD z, LD n, LD& rho )
{
	const LD si = kD * std::sin( alpha );
	if( si > 1.0L )
		return false;
	const LD i   = std::asin( si );
	const LD phi = i - alpha;
	const LD r   = std::asin( si / n );
	const LD cz  = std::cos( phi );
	if( z > cz )
		return false;//in front of the entry point: not on this ray
	rho = std::sin( phi ) + std::tan( r - phi ) * ( cz - z );
	return true;
}

/// Screen tangent radius of the point (rho, z) inside, by bisection on alpha;
/// false if no ray from the pinhole reaches it.
bool harnessXi( LD rho, LD z, LD n, LD& xi )
{
	const LD aMax = std::asin( 1.0L / kD ) * ( 1.0L - 1e-15L );
	LD hi         = aMax, lo = 0.0L, top = 0.0L, bottom = 0.0L;
	if( !rhoAt( hi, z, n, top ) || !rhoAt( lo, z, n, bottom ) || rho < bottom || rho > top )
		return false;
	for( int k = 0; k < 200; ++k )
	{
		const LD mid = 0.5L * ( lo + hi );
		LD v         = 0.0L;
		if( !rhoAt( mid, z, n, v ) )
			return false;
		( v < rho ? lo : hi ) = mid;
	}
	xi = std::tan( 0.5L * ( lo + hi ) );
	return true;
}

/// Screen position (centred px, y up) of a point in globe units of R.
bool harnessScreen( double x, double y, double z, double n, double globePx, double& sx, double& sy )
{
	const LD rho = std::sqrt( static_cast< LD >( x ) * x + static_cast< LD >( y ) * y );
	LD xi        = 0.0L;
	if( !harnessXi( rho, z, n, xi ) )
		return false;
	const LD scale = static_cast< LD >( globePx ) / silhouetteTangentLD();
	if( rho < 1e-15L )
	{
		sx = sy = 0.0;
		return true;
	}
	sx = static_cast< double >( xi * scale * x / rho );
	sy = static_cast< double >( xi * scale * y / rho );
	return true;
}

/// The inverse at a known depth: screen (centred px) to the point's (x, y).
bool harnessUnproject( double sx, double sy, double z, double n, double globePx, double& x, double& y )
{
	const LD scale = static_cast< LD >( globePx ) / silhouetteTangentLD();
	const LD r     = std::sqrt( static_cast< LD >( sx ) * sx + static_cast< LD >( sy ) * sy );
	const LD xi    = r / scale;
	const LD alpha = std::atan( xi );
	LD rho         = 0.0L;
	if( !rhoAt( alpha, z, n, rho ) )
		return false;
	if( r < 1e-15L )
	{
		x = y = 0.0;
		return true;
	}
	x = static_cast< double >( rho * sx / r );
	y = static_cast< double >( rho * sy / r );
	return true;
}

//---------------------------------------------------------------------------
// Pictures read back.
//---------------------------------------------------------------------------
/// The coverage-weighted centroid of the output's alpha (a transparent card:
/// alpha is exactly the flakes' coverage) in a window, centred px, y up.
bool alphaCentroid( const Floats& o, int w, int h, double cx, double cy, double half, double& mx, double& my, double* mass = nullptr )
{
	double s = 0.0, sx = 0.0, sy = 0.0;
	const int x0 = std::max( 0, static_cast< int >( std::floor( cx + 0.5 * w - half ) ) );
	const int x1 = std::min( w - 1, static_cast< int >( std::ceil( cx + 0.5 * w + half ) ) );
	const int y0 = std::max( 0, static_cast< int >( std::floor( cy + 0.5 * h - half ) ) );
	const int y1 = std::min( h - 1, static_cast< int >( std::ceil( cy + 0.5 * h + half ) ) );
	for( int y = y0; y <= y1; ++y )
		for( int x = x0; x <= x1; ++x )
		{
			const double a = o[ ( static_cast< size_t >( y ) * w + x ) * 4 + 3 ];
			s += a;
			sx += a * ( x + 0.5 - 0.5 * w );
			sy += a * ( y + 0.5 - 0.5 * h );
		}
	if( mass )
		*mass = s;
	if( !( s > 1e-6 ) )
		return false;
	mx = sx / s;
	my = sy / s;
	return true;
}

Floats clearCard( int w, int h )
{
	return flatCard( w, h, 0.0f, 0.0f, 0.0f, 0.0f );
}

/// Least squares y = a + b t: b, and the rms residual.
double fitSlope( const std::vector< double >& t, const std::vector< double >& y, double* rms = nullptr )
{
	const double n = static_cast< double >( t.size() );
	double st = 0, sy = 0, stt = 0, sty = 0;
	for( size_t k = 0; k < t.size(); ++k )
	{
		st += t[ k ];
		sy += y[ k ];
		stt += t[ k ] * t[ k ];
		sty += t[ k ] * y[ k ];
	}
	const double b = ( n * sty - st * sy ) / ( n * stt - st * st );
	const double a = ( sy - b * st ) / n;
	if( rms )
	{
		double e = 0.0;
		for( size_t k = 0; k < t.size(); ++k )
			e += ( y[ k ] - a - b * t[ k ] ) * ( y[ k ] - a - b * t[ k ] );
		*rms = std::sqrt( e / n );
	}
	return b;
}

/// The worse of two errors, NaN winning (std::max drops it).
double worse( double a, double b )
{
	if( !( a == a ) || !( b == b ) )
		return std::numeric_limits< double >::quiet_NaN();
	return std::max( a, b );
}

//===========================================================================
// --fall: single flakes of known size and orientation, no swirl, a clear
// card; each centroid tracked off the pixels, turned into a height by the
// harness's trace, fitted to a speed, held to the tables and the laws.
//===========================================================================
int runFall( const Perturb& perturb )
{
	std::printf( "\n=== fall: single flakes dropped with no swirl; each centroid off the pixels every 0.1 s, turned into a height by the harness's "
	             "own trace, fitted to a speed; held to Katz's disc mobility and Stokes's law with the GPA's Tables 4 and 17 (not Cheng's fit): "
	             "tolerance Cheng's 3.5 %% + Volk's 0.07 %% + the fit's own error\n" );
	struct Drop
	{
		Shape shape;
		double r;  ///< m
		V3 n;      ///< normal (discs)
		const char* what;
	};
	const double s45 = std::sqrt( 0.5 );
	const Drop drops[] = {
		{ Shape::Flakes, 0.45e-3, { 0, 1, 0 }, "disc a 0.45 mm broadside" },
		{ Shape::Flakes, 0.80e-3, { 0, 1, 0 }, "disc a 0.80 mm broadside" },
		{ Shape::Flakes, 1.25e-3, { 0, 1, 0 }, "disc a 1.25 mm broadside" },
		{ Shape::Flakes, 0.80e-3, { 1, 0, 0 }, "disc a 0.80 mm edgewise" },
		{ Shape::Flakes, 0.80e-3, { s45, s45, 0 }, "disc a 0.80 mm at 45 deg" },
		{ Shape::Beads, 0.10e-3, { 0, 1, 0 }, "bead r 0.10 mm" },
		{ Shape::Beads, 0.20e-3, { 0, 1, 0 }, "bead r 0.20 mm" },
		{ Shape::Beads, 0.30e-3, { 0, 1, 0 }, "bead r 0.30 mm" },
	};
	const double fills[] = { 0.60, 0.70 };
	for( const Raster& raster : kRasters )
	{
		double worstRel = 0.0, worstSlopeErr = 0.0;
		int measured = 0;
		for( double w : fills )
		{
			const TableRow* row = gpaRow( w );
			const double nTab   = gpaIndex( w );
			std::vector< double > discLogR, discLogU, beadLogR, beadLogU;
			for( Shape shape : { Shape::Flakes, Shape::Beads } )
			{
				Rig rig;
				const Floats card = clearCard( raster.w, raster.h );
				if( !startRig( rig, raster.w, raster.h, perturb, &card ) )
					return 1;
				const double aMax = shape == Shape::Beads ? 0.30e-3 : 1.25e-3;
				rig.Set( PT_FILL, ParamFromFill( w ) );
				rig.Set( PT_SNOW, static_cast< float >( shape ) );
				rig.Set( PT_SIZE, ParamFromSize( aMax, shape ) );
				rig.Set( PT_RANGE, ParamFromRange( 0.25 ) );
				if( !rig.Render( 1 ) )
					return 1;
				const Physics ph = rig.plugin.CurrentPhysics();
				//The flakes of this shape, spread across, starting high.
				std::vector< Drop > mine;
				for( const Drop& d : drops )
					if( d.shape == shape )
						mine.push_back( d );
				rig.plugin.SetCountForTest( static_cast< int >( mine.size() ) );
				const double z0 = 0.15;//units of R, in front of the card
				for( size_t k = 0; k < mine.size(); ++k )
				{
					const double x = -0.45 + 0.9 * static_cast< double >( k ) / std::max< size_t >( mine.size() - 1, 1 );
					const double u = ( mine[ k ].r - ph.aMin ) / ( ph.aMax - ph.aMin );
					rig.plugin.GlobeForTest().PlaceForTest( static_cast< int >( k ), { x * kRadius, 0.45 * kRadius, z0 * kRadius }, mine[ k ].n, false, u );
				}
				const double globePx = rig.plugin.LastFrame().globePx;
				std::vector< std::vector< double > > ts( mine.size() ), ys( mine.size() ), xs( mine.size() );
				std::vector< std::pair< double, double > > last( mine.size(), { 1e9, 1e9 } );
				const int frames = 1200;
				for( int f = 0; f <= frames; ++f )
				{
					if( !rig.Render( 1 ) )
						return 1;
					if( f % 6 != 0 )
						continue;
					const Floats o = rig.Output();
					const double t = static_cast< double >( rig.plugin.LastFrame().clockUs ) * 1e-6;
					for( size_t k = 0; k < mine.size(); ++k )
					{
						const Flake& fl = rig.plugin.GlobeForTest().Flakes()[ k ];
						if( fl.resting )
							continue;
						//Where the harness's trace says it should be, then the
						//centroid there, then back to a height.
						double px = 0, py = 0;
						if( last[ k ].first > 1e8 )
						{
							if( !harnessScreen( fl.x.x / kRadius, fl.x.y / kRadius, z0, nTab, globePx, px, py ) )
								continue;
						}
						else
						{
							px = last[ k ].first;
							py = last[ k ].second;
						}
						double mx = 0, my = 0;
						const double half = std::max( 4.0, 3.0 * globePx / 324.0 * 4.0 );
						if( !alphaCentroid( o, raster.w, raster.h, px, py, half, mx, my ) )
							continue;
						last[ k ] = { mx, my };
						double X = 0, Y = 0;
						if( !harnessUnproject( mx, my, z0, nTab, globePx, X, Y ) )
							continue;
						ts[ k ].push_back( t );
						ys[ k ].push_back( Y * kRadius );
						xs[ k ].push_back( X * kRadius );
					}
				}
				for( size_t k = 0; k < mine.size(); ++k )
				{
					if( ts[ k ].size() < 20 )
					{
						Check( false, fmt( "%dx%d w %.2f %s: only %zu samples", raster.w, raster.h, w, mine[ k ].what, ts[ k ].size() ) );
						continue;
					}
					double rmsY = 0, rmsX = 0;
					const double vy = fitSlope( ts[ k ], ys[ k ], &rmsY );
					const double vx = fitSlope( ts[ k ], xs[ k ], &rmsX );
					const V3 e      = expectedVelocity( shape, mine[ k ].r, mine[ k ].n, row->rho, row->mu );
					const double span = std::fabs( ys[ k ].back() - ys[ k ].front() );
					//The fit's own error: the rms residual over the height fallen.
					const double fitErr = 2.0 * rmsY / std::max( span, 1e-12 );
					const double tol    = kChengError + kVolkError + fitErr;
					const double rel    = std::fabs( vy / e.y - 1.0 );
					worstRel            = worse( worstRel, rel );
					++measured;
					std::string drift;
					bool driftOk = true;
					if( std::fabs( e.x ) > 1e-9 )
					{
						const double ratioM = vx / vy, ratioE = e.x / e.y;
						driftOk             = std::fabs( ratioM - ratioE ) <= 0.02 * std::fabs( ratioE ) + 2.0 * rmsX / std::max( span, 1e-12 );
						drift               = fmt( "; glide/fall %.4f (law %.4f)", ratioM, ratioE );
					}
					Check( rel <= tol && driftOk,
					       fmt( "%dx%d w %.2f %s: fell %.4f mm/s (law %.4f; %+.2f%%, tolerance %.2f%%) over %.2f mm, %zu samples%s", raster.w, raster.h, w,
					            mine[ k ].what, -vy * 1e3, -e.y * 1e3, 100.0 * ( vy / e.y - 1.0 ), 100.0 * tol, span * 1e3, ts[ k ].size(),
					            drift.c_str() ) );
					if( mine[ k ].n.y == 1.0 )
					{
						auto& lr = shape == Shape::Beads ? beadLogR : discLogR;
						auto& lu = shape == Shape::Beads ? beadLogU : discLogU;
						lr.push_back( std::log( mine[ k ].r ) );
						lu.push_back( std::log( -vy ) );
					}
				}
			}
			//The exponents: the viscosity's error is common to every size and
			//cancels; what is left is the fits'.
			if( discLogR.size() >= 3 && beadLogR.size() >= 3 )
			{
				const double pd = fitSlope( discLogR, discLogU ), pb = fitSlope( beadLogR, beadLogU );
				worstSlopeErr = worse( worstSlopeErr, worse( std::fabs( pd - 1.0 ), std::fabs( pb - 2.0 ) ) );
				Check( std::fabs( pd - 1.0 ) <= 0.02 && std::fabs( pb - 2.0 ) <= 0.02,
				       fmt( "%dx%d w %.2f: speed against size, log-log slope %.4f for discs of one film (law 1) and %.4f for beads (law 2), tolerance 0.02",
				            raster.w, raster.h, w, pd, pb ) );
			}
			else
				Check( false, fmt( "%dx%d w %.2f: too few sizes measured for the exponents", raster.w, raster.h, w ) );
		}
		Check( measured == 2 * static_cast< int >( sizeof( drops ) / sizeof( drops[ 0 ] ) ),
		       fmt( "%dx%d: %d of %d falls measured; worst speed error %.2f%%, worst exponent error %.4f", raster.w, raster.h, measured,
		            2 * static_cast< int >( sizeof( drops ) / sizeof( drops[ 0 ] ) ), 100.0 * worstRel, worstSlopeErr ) );
	}
	return Verdict();
}

//===========================================================================
// --settle: one shake that only lifts (swirl zero) from flakes resting on the
// glass's front; the last frame whose pixels change, against the time the
// model derives: the latest of each flake's straight-line time to the glass.
//===========================================================================
/// The time for a flake at x (m) moving at v to reach |x| = L.
double timeToGlass( V3 x, V3 v, double L )
{
	const double a = dot( v, v ), b = 2.0 * dot( x, v ), c = dot( x, x ) - L * L;
	if( !( a > 0.0 ) )
		return std::numeric_limits< double >::infinity();
	const double disc = b * b - 4.0 * a * c;
	return ( -b + std::sqrt( std::max( disc, 0.0 ) ) ) / ( 2.0 * a );
}

int runSettle( const Perturb& perturb )
{
	std::printf( "\n=== settle: 40 discs resting on the front of the glass, one lift-only shake (S 0.6, swirl off); the last frame whose pixels "
	             "change, against the derived time to settle (each flake's closed-form time to the glass, the latest; viscosity from the GPA's "
	             "Table 17): tolerance Cheng's 3.5 %% + two frames\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		const Floats card = clearCard( raster.w, raster.h );
		if( !startRig( rig, raster.w, raster.h, perturb, &card ) )
			return 1;
		const double w = 0.60;
		rig.Set( PT_FILL, ParamFromFill( w ) );
		rig.Set( PT_SIZE, ParamFromSize( 1.25e-3, Shape::Flakes ) );
		rig.Set( PT_RANGE, ParamFromRange( 0.6 ) );
		const int count = 40;
		rig.plugin.SetCountForTest( count );
		rig.plugin.GlobeForTest().SetSwirlGainForTest( 0.0 );
		if( !rig.Render( 1 ) )
			return 1;
		const Physics ph = rig.plugin.CurrentPhysics();
		//Resting on the glass's lower front, where every landing is seen.
		for( int i = 0; i < count; ++i )
		{
			const double u   = Hash01( 99, static_cast< uint64_t >( i ), 0 );
			const double az  = -0.6 + 1.2 * Hash01( 99, static_cast< uint64_t >( i ), 1 );
			const double el  = -0.95 + 0.35 * Hash01( 99, static_cast< uint64_t >( i ), 2 );
			const double r   = ph.aMin + ( ph.aMax - ph.aMin ) * u;
			const double L   = kRadius - r;
			const double cel = std::sqrt( 1.0 - el * el );
			const V3 x{ L * cel * std::sin( az ), L * el, L * cel * std::cos( az ) };
			rig.plugin.GlobeForTest().PlaceForTest( i, x, ( 1.0 / L ) * x, true, u );
		}
		if( !rig.Render( 30 ) )
			return 1;
		rig.plugin.QueueShakeForTest( rig.plugin.LastFrame().clockUs + 4321, 0.6 );
		if( !rig.Render( 1 ) )
			return 1;
		//The derived time, from the lifted state: the stored state is at the
		//last grid point after the shake, and until a flake lands it moves in
		//a straight line, so its time to the glass from there plus the time
		//since the shake.
		const TableRow* row = gpaRow( w );
		const double tShake = static_cast< double >( rig.plugin.ShakeLog().back().us ) * 1e-6;
		const double since  = static_cast< double >( rig.plugin.GlobeForTest().StateUs() ) * 1e-6 - tShake;
		double derived = 0.0, derivedCheng = 0.0;
		bool allSeen = true, allLifted = since >= 0.0;
		for( int i = 0; i < count; ++i )
		{
			const Flake& f   = rig.plugin.GlobeForTest().Flakes()[ static_cast< size_t >( i ) ];
			const double r   = rig.plugin.GlobeForTest().RadiusOf( i, ph );
			const V3 vTab    = expectedVelocity( Shape::Flakes, r, f.n, row->rho, row->mu );
			const V3 vCheng  = expectedVelocity( Shape::Flakes, r, f.n, ph.fill.rho, ph.fill.mu );
			const double t1  = since + timeToGlass( f.x, vTab, kRadius - r );
			const double t2  = since + timeToGlass( f.x, vCheng, kRadius - r );
			allLifted        = allLifted && !f.resting;
			derived          = worse( derived, t1 );
			derivedCheng     = worse( derivedCheng, t2 );
			const V3 land    = f.x + ( t2 - since ) * vCheng;
			double sx = 0, sy = 0;
			allSeen = allSeen && harnessScreen( land.x / kRadius, land.y / kRadius, land.z / kRadius, ph.fill.n, rig.plugin.LastFrame().globePx, sx, sy );
		}
		//Frames until nothing changes for two simulated seconds.
		Floats prev  = rig.Output();
		double lastChange = tShake;
		int still = 0, frames = 0;
		while( still < 120 && frames < 60 * 120 )
		{
			if( !rig.Render( 1 ) )
				return 1;
			++frames;
			const Floats o = rig.Output();
			if( o != prev )
			{
				lastChange = static_cast< double >( rig.plugin.LastFrame().clockUs ) * 1e-6;
				still      = 0;
			}
			else
				++still;
			prev = o;
		}
		const double measured = lastChange - tShake;
		const double frame    = 1.0 / rig.fps;
		const double tol      = kChengError * derived + 2.0 * frame;
		//The model's own time (its Cheng viscosity): the landing logic alone.
		//A flake is caught at the end of the 8 ms step it crosses the glass
		//in, and the first frame after that is the last that changes.
		const double stepS = static_cast< double >( kStepUs ) * 1e-6;
		const bool exact   = measured >= derivedCheng - 1e-9 && measured <= derivedCheng + stepS + frame + 1e-9;
		Check( allLifted && allSeen && std::fabs( measured - derived ) <= tol && exact && still >= 120,
		       fmt( "%dx%d: settled %.3f s after the shake (derived %.3f s from the tables, tolerance %.3f s; %.3f s from the model's own viscosity, "
		            "measured within a step and a frame after it: %s); every flake lifted: %s, every landing seen: %s",
		            raster.w, raster.h, measured, derived, tol, derivedCheng, exact ? "yes" : "NO", allLifted ? "yes" : "NO", allSeen ? "yes" : "NO" ) );
	}
	return Verdict();
}

//===========================================================================
// --swirl: neutral tracers in a swirl about the view axis; their angular
// rate off the pixels decays as exp(-nu k^2 t), k = x1 / R, nu from the
// tables; the ratio of two radii's rates is 3 j1(kr)/(kr)'s.
//===========================================================================
double besselProfile( double z )
{
	if( z < 1e-3 )
		return 1.0 - z * z / 10.0;
	return 3.0 * ( std::sin( z ) - z * std::cos( z ) ) / ( z * z * z );
}

double rootTanX()
{
	double lo = 4.4, hi = 4.6;
	for( int k = 0; k < 200; ++k )
	{
		const double mid = 0.5 * ( lo + hi );
		( std::tan( mid ) - mid > 0.0 ? hi : lo ) = mid;
	}
	return 0.5 * ( lo + hi );
}

int runSwirl( const Perturb& perturb )
{
	std::printf( "\n=== swirl: neutral tracers at 0.15 R and 0.6 R from the centre, in a swirl about the view axis (2 rad/s at the centre); each "
	             "one's angle off the pixels; its rate's decay against nu (x1/R)^2 with nu from the GPA's tables (tolerance Cheng's 3.5 %% + the "
	             "fit's), the rates' ratio against 3 j1(kr)/(kr) (tolerance 1 %%)\n" );
	const double x1 = rootTanX();
	for( const Raster& raster : kRasters )
	{
		for( double w : { 0.60, 0.70 } )
		{
			Rig rig;
			const Floats card = clearCard( raster.w, raster.h );
			if( !startRig( rig, raster.w, raster.h, perturb, &card ) )
				return 1;
			rig.Set( PT_FILL, ParamFromFill( w ) );
			rig.Set( PT_SNOW, static_cast< float >( Shape::Beads ) );
			rig.Set( PT_SIZE, ParamFromSize( 0.3e-3, Shape::Beads ) );
			rig.Set( PT_RANGE, ParamFromRange( 1.0 ) );
			rig.plugin.SetCountForTest( 2 );
			rig.plugin.GlobeForTest().SetNeutralForTest( true );
			if( !rig.Render( 1 ) )
				return 1;
			const double radii[ 2 ] = { 0.15, 0.6 };
			for( int k = 0; k < 2; ++k )
				rig.plugin.GlobeForTest().PlaceForTest( k, { radii[ k ] * kRadius, 0.0, 0.05 * kRadius }, { 0, 1, 0 }, false, 0.0 );
			rig.plugin.GlobeForTest().SetSwirlForTest( { 0.0, 0.0, 2.0 } );
			const TableRow* row  = gpaRow( w );
			const double nTab    = gpaIndex( w );
			const double lawRate = row->mu / row->rho * ( x1 / kRadius ) * ( x1 / kRadius );
			const double globePx = rig.plugin.LastFrame().globePx;
			std::vector< double > ts, th[ 2 ];
			double prevAngle[ 2 ] = { 0, 0 }, unwrap[ 2 ] = { 0, 0 };
			double lastXY[ 2 ][ 2 ];
			for( int k = 0; k < 2; ++k )
			{
				double sx = 0, sy = 0;
				harnessScreen( radii[ k ], 0.0, 0.05, nTab, globePx, sx, sy );
				lastXY[ k ][ 0 ] = sx;
				lastXY[ k ][ 1 ] = sy;
			}
			const int frames = static_cast< int >( 2.0 * 60.0 / ( lawRate ) );//two time constants
			bool lost        = false;
			for( int f = 0; f < frames && !lost; ++f )
			{
				if( !rig.Render( 1 ) )
					return 1;
				if( f % 3 != 0 )
					continue;
				const Floats o = rig.Output();
				ts.push_back( static_cast< double >( rig.plugin.LastFrame().clockUs ) * 1e-6 );
				for( int k = 0; k < 2; ++k )
				{
					double mx = 0, my = 0;
					const double half = std::max( 4.0, 0.04 * globePx );
					if( !alphaCentroid( o, raster.w, raster.h, lastXY[ k ][ 0 ], lastXY[ k ][ 1 ], half, mx, my ) )
					{
						lost = true;
						break;
					}
					lastXY[ k ][ 0 ] = mx;
					lastXY[ k ][ 1 ] = my;
					const double a   = std::atan2( my, mx );
					double d         = a - prevAngle[ k ];
					while( d > kPi )
						d -= 2.0 * kPi;
					while( d < -kPi )
						d += 2.0 * kPi;
					unwrap[ k ] += th[ k ].empty() ? a : d;
					prevAngle[ k ] = a;
					th[ k ].push_back( unwrap[ k ] );
				}
			}
			if( lost || ts.size() < 40 )
			{
				Check( false, fmt( "%dx%d w %.2f: a tracer was lost (%zu samples)", raster.w, raster.h, w, ts.size() ) );
				continue;
			}
			//Rates over windows of 0.25 s, then ln(rate) against time.
			double rate[ 2 ] = { 0, 0 }, rms[ 2 ] = { 0, 0 }, ratioSum = 0.0;
			int ratioN = 0;
			std::vector< double > wt[ 2 ], wl[ 2 ];
			const size_t span = 5;
			for( size_t i = 0; i + span < ts.size(); i += span )
			{
				double om[ 2 ];
				for( int k = 0; k < 2; ++k )
				{
					om[ k ] = ( th[ k ][ i + span ] - th[ k ][ i ] ) / ( ts[ i + span ] - ts[ i ] );
					wt[ k ].push_back( 0.5 * ( ts[ i ] + ts[ i + span ] ) );
					wl[ k ].push_back( std::log( std::fabs( om[ k ] ) ) );
				}
				ratioSum += om[ 1 ] / om[ 0 ];
				++ratioN;
			}
			for( int k = 0; k < 2; ++k )
				rate[ k ] = -fitSlope( wt[ k ], wl[ k ], &rms[ k ] );
			const double fitErr  = 2.0 * rms[ 0 ] / std::max( 1e-9, std::fabs( wl[ 0 ].front() - wl[ 0 ].back() ) );
			const double tol     = kChengError + kVolkError + fitErr;
			const double ratio   = ratioSum / ratioN;
			const double ratioLaw = besselProfile( x1 * radii[ 1 ] ) / besselProfile( x1 * radii[ 0 ] );
			Check( std::fabs( rate[ 0 ] / lawRate - 1.0 ) <= tol && std::fabs( rate[ 1 ] / lawRate - 1.0 ) <= tol && std::fabs( ratio / ratioLaw - 1.0 ) <= 0.01,
			       fmt( "%dx%d w %.2f: decay %.4f and %.4f /s at 0.15 R and 0.6 R (law %.4f /s, tau %.2f s; %+.2f%%, %+.2f%%, tolerance %.2f%%); rate "
			            "ratio %.4f (law %.4f)",
			            raster.w, raster.h, w, rate[ 0 ], rate[ 1 ], lawRate, 1.0 / lawRate, 100.0 * ( rate[ 0 ] / lawRate - 1.0 ),
			            100.0 * ( rate[ 1 ] / lawRate - 1.0 ), 100.0 * tol, ratio, ratioLaw ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --still: kicks, then music with no kick in it until every flake is at rest,
// then 300 frames: every one bit-identical, the state too.
//===========================================================================
double hatsOnly( double t )
{
	const double off = std::fmod( t + 0.25, 0.5 );
	const uint32_t n = static_cast< uint32_t >( t * kRate );
	return 0.25 * std::exp( -off / 0.01 ) * ( Hash01( n, 7u, 0u ) - Hash01( n - 1u, 7u, 0u ) );
}

int runStill( const Perturb& perturb )
{
	std::printf( "\n=== still: three seconds of kicks (Trigger On = Kick, the harness's music), then hats with no kick until every flake is at rest, "
	             "then 300 frames of the same: every output float and the CPU state bit-identical\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		rig.Set( PT_TRIGGER_ON, static_cast< float >( TriggerOn::Kick ) );
		rig.Set( PT_SHAKE, 0.25f );
		rig.Set( PT_SIZE, ParamFromSize( 1.25e-3, Shape::Flakes ) );
		rig.Set( PT_RANGE, ParamFromRange( 0.7 ) );
		rig.Set( PT_COUNT_FLAKES, ParamFromCount( 300 ) );
		rig.audio = musicAt;
		if( !rig.Render( 180 ) )
			return 1;
		const size_t shakes = rig.plugin.ShakeLog().size();
		rig.audio           = hatsOnly;
		int frames          = 0;
		while( rig.plugin.LastFrame().suspended > 0 && frames < 60 * 600 )
		{
			if( !rig.Render( 60 ) )
				return 1;
			frames += 60;
		}
		const double settledAt = static_cast< double >( rig.plugin.LastFrame().clockUs ) * 1e-6;
		if( !rig.Render( 2 ) )
			return 1;
		const Floats first     = rig.Output();
		const uint64_t hash    = rig.plugin.LastFrame().stateHash;
		const size_t shakesNow = rig.plugin.ShakeLog().size();
		int differing = 0, hashChanges = 0;
		for( int f = 0; f < 300; ++f )
		{
			if( !rig.Render( 1 ) )
				return 1;
			differing += rig.Output() != first;
			hashChanges += rig.plugin.LastFrame().stateHash != hash;
		}
		const bool noShakes = rig.plugin.ShakeLog().size() == shakesNow;
		Check( shakes > 0 && rig.plugin.LastFrame().suspended == 0 && differing == 0 && hashChanges == 0 && noShakes && rig.plugin.LastFrame().drawn > 0,
		       fmt( "%dx%d: %zu shakes from the kicks, all at rest by %.1f s; then 300 frames of hats: %d differ, state changed %d times, %zu shakes; %d "
		            "flakes drawn",
		            raster.w, raster.h, shakes, settledAt, differing, hashChanges, rig.plugin.ShakeLog().size() - shakesNow,
		            rig.plugin.LastFrame().drawn ) );
	}
	return Verdict();
}
