//===========================================================================
// The glass: --grid, --magnify, --flakes. Each held to the harness's own
// trace (checks_physics.inc), with n from the GPA's Table 23, not the fit.
//===========================================================================

/// The fraction of [x, x + 1] that [c - half, c + half] covers.
double lineCover( double x, double c, double half )
{
	return std::max( 0.0, std::min( x + 1.0, c + half ) - std::max( x, c - half ) );
}

/// A card for the glass: R and G ramp across and up (colour over the whole
/// frame, so the rim has something to bend), B = 0; lines white, drawn with
/// exact box coverage. Lines sit at card coordinates (units of R, the card's
/// height spanning 2R as the plugin maps it) given in `xs` and `ys`.
Floats lineCard( int w, int h, const std::vector< double >& xs, const std::vector< double >& ys, double width )
{
	Floats card( static_cast< size_t >( w ) * h * 4 );
	const double unit = 0.5 * h;//px per R on the card
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
		{
			double cv = 0.0, ch = 0.0;
			for( double X : xs )
				cv = std::max( cv, lineCover( x, 0.5 * w + X * unit, 0.5 * width * unit ) );
			for( double Y : ys )
				ch = std::max( ch, lineCover( y, 0.5 * h + Y * unit, 0.5 * width * unit ) );
			const double c = cv + ch - cv * ch;
			float* p       = &card[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
			const double r = 0.2 + 0.6 * ( x + 0.5 ) / w, g = 0.2 + 0.6 * ( y + 0.5 ) / h;
			p[ 0 ] = static_cast< float >( c + ( 1.0 - c ) * r );
			p[ 1 ] = static_cast< float >( c + ( 1.0 - c ) * g );
			p[ 2 ] = static_cast< float >( c );
			p[ 3 ] = 1.0f;
		}
	return card;
}

/// The B-weighted centroid in a window (lines are B = 1, the card B = 0).
bool blueCentroid( const Floats& o, int w, int h, double cx, double cy, double halfX, double halfY, double& mx, double& my )
{
	double s = 0.0, sx = 0.0, sy = 0.0;
	const int x0 = std::max( 0, static_cast< int >( std::ceil( cx + 0.5 * w - halfX - 0.5 ) ) );
	const int x1 = std::min( w - 1, static_cast< int >( std::floor( cx + 0.5 * w + halfX - 0.5 ) ) );
	const int y0 = std::max( 0, static_cast< int >( std::ceil( cy + 0.5 * h - halfY - 0.5 ) ) );
	const int y1 = std::min( h - 1, static_cast< int >( std::floor( cy + 0.5 * h + halfY - 0.5 ) ) );
	for( int y = y0; y <= y1; ++y )
		for( int x = x0; x <= x1; ++x )
		{
			const double b = o[ ( static_cast< size_t >( y ) * w + x ) * 4 + 2 ];
			s += b;
			sx += b * ( x + 0.5 - 0.5 * w );
			sy += b * ( y + 0.5 - 0.5 * h );
		}
	if( !( s > 1e-6 ) )
		return false;
	mx = sx / s;
	my = sy / s;
	return true;
}

//===========================================================================
// --grid: grid crossings through the glass against the harness's trace.
//===========================================================================
/// The card through the glass, traced by the harness pixel by pixel: the
/// shader's four subsamples at +-0.25 px, each taken back to the card by
/// harnessUnproject (angles, scalar Snell, nothing of the shader's vector
/// form), the card read by the same premultiplied bilinear rule (texel
/// centres at +0.5, clamped at the edge, nothing outside the clip), in
/// double. Pixels beyond `within` of the silhouette are left at 0.
Floats tracedCard( const Floats& card, int w, int h, double n, double globePx, double within )
{
	Floats out( card.size(), 0.0f );
	const double aspect = static_cast< double >( w ) / h;
	auto texel = [ & ]( int x, int y, int c ) {
		x = std::clamp( x, 0, w - 1 );
		y = std::clamp( y, 0, h - 1 );
		const float* p = &card[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
		const double a = std::clamp( static_cast< double >( p[ 3 ] ), 0.0, 1.0 );
		return c == 3 ? a : std::clamp( static_cast< double >( p[ c ] ), 0.0, 1.0 ) * a;
	};
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
		{
			const double Px = x + 0.5 - 0.5 * w, Py = y + 0.5 - 0.5 * h;
			if( std::hypot( Px, Py ) > within * globePx )
				continue;
			double sum[ 4 ] = {};
			for( int s = 0; s < 4; ++s )
			{
				const double sx = Px + ( s & 1 ? 0.25 : -0.25 ), sy = Py + ( s & 2 ? 0.25 : -0.25 );
				double X = 0, Y = 0;
				if( !harnessUnproject( sx, sy, 0.0, n, globePx, X, Y ) )
					continue;
				const double tx = 0.5 + X / ( 2.0 * aspect ), ty = 0.5 + 0.5 * Y;
				if( tx < 0.0 || ty < 0.0 || tx > 1.0 || ty > 1.0 )
					continue;
				const double px = tx * w - 0.5, py = ty * h - 0.5;
				const double fx = std::floor( px ), fy = std::floor( py );
				const double ux = px - fx, uy = py - fy;
				const int ix = static_cast< int >( fx ), iy = static_cast< int >( fy );
				for( int c = 0; c < 4; ++c )
				{
					const double lo = texel( ix, iy, c ) * ( 1.0 - ux ) + texel( ix + 1, iy, c ) * ux;
					const double hi = texel( ix, iy + 1, c ) * ( 1.0 - ux ) + texel( ix + 1, iy + 1, c ) * ux;
					sum[ c ] += lo * ( 1.0 - uy ) + hi * uy;
				}
			}
			for( int c = 0; c < 4; ++c )
				out[ ( static_cast< size_t >( y ) * w + x ) * 4 + c ] = static_cast< float >( 0.25 * sum[ c ] );
		}
	return out;
}

/// The crossing finder: an iterated B-weighted centroid in a window a third
/// of the way to the nearest neighbour, started where the trace puts it.
bool findCrossing( const Floats& o, int w, int h, double px, double py, double half, double& mx, double& my )
{
	mx = px;
	my = py;
	bool ok = true;
	for( int it = 0; it < 6 && ok; ++it )
		ok = blueCentroid( o, w, h, mx, my, half, half, mx, my );
	return ok;
}

/// The shader's float trace against the harness's double one: a crossing
/// sits where the card's B ramps cross; float rounding in the trace moves
/// the card coordinate by a few ulp of the card's 1.0 (6e-8 x a few, times
/// the card's px per R, 360 at 720p: 1e-4 px), which the screen sees divided
/// by the card's magnification (>= 1): tolerance 1e-3 px, ten times that.
constexpr double kGridTolerance = 1e-3;

int runGrid( const Perturb& perturb )
{
	std::printf( "\n=== grid: a grid card (lines every R/8) through the globe at w 0.60 and 0.80; each crossing within 0.8 of the silhouette found "
	             "by an iterated B-weighted centroid in the plugin's picture and in the harness's own traced picture (angles, scalar Snell, double, "
	             "n from Table 23): the two within %.0e px (the shader's float trace); and the traced picture's crossings against where the trace "
	             "puts the card's crossing points (the finder's own bias, reported)\n",
	             kGridTolerance );
	const double g = 0.125, lw = 0.02;
	std::vector< double > lines;
	for( int k = -8; k <= 8; ++k )
		lines.push_back( k * g );
	for( const Raster& raster : kRasters )
		for( double w : { 0.60, 0.80 } )
		{
			Rig rig;
			const Floats card = lineCard( raster.w, raster.h, lines, lines, lw );
			if( !startRig( rig, raster.w, raster.h, perturb, &card ) )
				return 1;
			rig.Set( PT_FILL, ParamFromFill( w ) );
			rig.plugin.SetCountForTest( 0 );
			if( !rig.Render( 2 ) )
				return 1;
			const Floats o       = rig.Output();
			const double globePx = rig.plugin.LastFrame().globePx;
			const double n       = gpaIndex( w );
			const Floats traced  = tracedCard( card, raster.w, raster.h, n, globePx, 0.9 );
			auto predict         = [ & ]( int i, int j, double& sx, double& sy ) { return harnessScreen( i * g, j * g, 0.0, n, globePx, sx, sy ); };
			double worst = 0.0, sum2 = 0.0, worstR = 0.0, bias = 0.0;
			int count = 0;
			for( int i = -7; i <= 7; ++i )
				for( int j = -7; j <= 7; ++j )
				{
					double px = 0, py = 0;
					if( !predict( i, j, px, py ) || std::hypot( px, py ) > 0.8 * globePx )
						continue;
					double nearest = 1e9;
					const int di[ 4 ] = { 1, -1, 0, 0 }, dj[ 4 ] = { 0, 0, 1, -1 };
					for( int k = 0; k < 4; ++k )
					{
						double qx = 0, qy = 0;
						if( predict( i + di[ k ], j + dj[ k ], qx, qy ) )
							nearest = std::min( nearest, std::hypot( qx - px, qy - py ) );
					}
					const double half = 0.33 * nearest;
					if( half < 2.5 )
						continue;
					double mx = 0, my = 0, tx = 0, ty = 0;
					if( !findCrossing( o, raster.w, raster.h, px, py, half, mx, my ) || !findCrossing( traced, raster.w, raster.h, px, py, half, tx, ty ) )
					{
						worst = std::numeric_limits< double >::quiet_NaN();
						continue;
					}
					const double e = std::hypot( mx - tx, my - ty );
					if( !( e <= worst ) )
						worstR = std::hypot( px, py ) / globePx;
					worst = worse( worst, e );
					bias  = worse( bias, std::hypot( tx - px, ty - py ) );
					sum2 += e * e;
					++count;
				}
			//Lines every R/8 put 69 crossings inside 0.8 of the silhouette;
			//fewer than 60 found and the check has not looked at the rim.
			const int minimum = 60;
			Check( count >= minimum && worst <= kGridTolerance,
			       fmt( "%dx%d w %.2f (n %.5f): %d crossings, the plugin's against the traced picture's worst %.2e px (at %.2f of the silhouette), "
			            "rms %.2e px; the finder's own bias on the traced picture, worst %.3f px",
			            raster.w, raster.h, w, n, count, worst, worstR, count ? std::sqrt( sum2 / count ) : 0.0, bias ) );
		}
	return Verdict();
}

//===========================================================================
// --magnify: the card's centre magnified by n.
//===========================================================================
int runMagnify( const Perturb& perturb )
{
	std::printf( "\n=== magnify: two lines at +-d either side of the card's centre, each way; their separation on the screen against the card seen "
	             "with no fill (d/D per tangent): the magnification, against the harness's trace at d (which tends to n, Table 23's, as d -> 0) at "
	             "w 0.60 and 0.80; tolerance 0.2 px over the separation\n" );
	for( const Raster& raster : kRasters )
		for( double w : { 0.60, 0.80 } )
		{
			const double n       = gpaIndex( w );
			const double globePx = 0.5 * 0.9 * raster.h;
			const double xiSil   = static_cast< double >( silhouetteTangentLD() );
			//d such that the lines are at least 40 px apart.
			const double d = std::max( 0.05, 40.0 * static_cast< double >( kD ) / ( 2.0 * n * globePx / xiSil ) );
			Rig rig;
			const Floats card = lineCard( raster.w, raster.h, { -d, d }, { -d, d }, 0.01 );
			if( !startRig( rig, raster.w, raster.h, perturb, &card ) )
				return 1;
			rig.Set( PT_FILL, ParamFromFill( w ) );
			rig.plugin.SetCountForTest( 0 );
			if( !rig.Render( 2 ) )
				return 1;
			const Floats o = rig.Output();
			double sx = 0, sy = 0;
			harnessScreen( d, 0.0, 0.0, n, rig.plugin.LastFrame().globePx, sx, sy );
			const double half = 0.3 * sx;
			double xr = 0, xl = 0, yt = 0, yb = 0, dummy = 0;
			const bool ok = blueCentroid( o, raster.w, raster.h, sx, 0.0, half, 2.0, xr, dummy ) && blueCentroid( o, raster.w, raster.h, -sx, 0.0, half, 2.0, xl, dummy )
			                && blueCentroid( o, raster.w, raster.h, 0.0, sx, 2.0, half, dummy, yt ) && blueCentroid( o, raster.w, raster.h, 0.0, -sx, 2.0, half, dummy, yb );
			const double unrefracted = 2.0 * d / static_cast< double >( kD ) / xiSil * rig.plugin.LastFrame().globePx;
			const double mx = ( xr - xl ) / unrefracted, my = ( yt - yb ) / unrefracted;
			const double mTrace = 2.0 * sx / unrefracted;
			const double tol    = 0.2 / ( xr - xl );
			Check( ok && std::fabs( mx / mTrace - 1.0 ) <= tol && std::fabs( my / mTrace - 1.0 ) <= tol,
			       fmt( "%dx%d w %.2f: magnification %.5f across, %.5f up (trace at d = %.3f R: %.5f, %+.3f%% from n = %.5f); %+.3f%%, %+.3f%%, tolerance %.3f%%",
			            raster.w, raster.h, w, mx, my, d, mTrace, 100.0 * ( mTrace / n - 1.0 ), n, 100.0 * ( mx / mTrace - 1.0 ), 100.0 * ( my / mTrace - 1.0 ),
			            100.0 * tol ) );
		}
	return Verdict();
}

//===========================================================================
// --flakes: flakes at known points appear where the harness's trace puts
// them, and cover their true area (the projected disc's, from the trace's
// own Jacobian by differences).
//===========================================================================
/// The flake shader's 4 x 4 box rule over its one-pixel ramp, simulated for
/// 6,000 seeded ellipses of 0.2-6 px at every aspect and lattice offset
/// (tools/coverage.py): area within 0.84 %, centroid within 0.032 px. The
/// tolerances round those up; the trace's own differences are 1e-8 of them.
constexpr double kFlakeAreaTolerance     = 0.01;
constexpr double kFlakePositionTolerance = 0.04;

int runFlakes( const Perturb& perturb )
{
	std::printf( "\n=== flakes: beads held still at known points (in front, at the card, behind it through a clear card, near the rim); each "
	             "alpha centroid against the harness's trace (tolerance 0.04 px), each one's summed coverage against pi r^2 sqrt(det J J^T), J the "
	             "trace's by differences (tolerance 1 %%): both from the shader's box rule (tools/coverage.py)\n" );
	struct Spot
	{
		double x, y, z;
		const char* what;
	};
	const Spot spots[] = {
		{ 0.30, 0.20, 0.50, "in front" },     { -0.40, -0.30, 0.02, "at the card" }, { 0.20, -0.40, -0.50, "behind the card" },
		{ -0.10, 0.55, -0.20, "behind, high" }, { 0.00, -0.80, 0.45, "low, near the rim" }, { 0.72, 0.00, 0.45, "right, near the rim" },
	};
	const int count = static_cast< int >( sizeof( spots ) / sizeof( spots[ 0 ] ) );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		const Floats card = clearCard( raster.w, raster.h );
		if( !startRig( rig, raster.w, raster.h, perturb, &card ) )
			return 1;
		rig.Set( PT_SNOW, static_cast< float >( Shape::Beads ) );
		rig.Set( PT_SIZE, ParamFromSize( 0.3e-3, Shape::Beads ) );
		rig.Set( PT_RANGE, ParamFromRange( 1.0 ) );
		rig.plugin.SetCountForTest( count );
		rig.plugin.GlobeForTest().SetNeutralForTest( true );
		if( !rig.Render( 1 ) )
			return 1;
		for( int k = 0; k < count; ++k )
			rig.plugin.GlobeForTest().PlaceForTest( k, { spots[ k ].x * kRadius, spots[ k ].y * kRadius, spots[ k ].z * kRadius }, { 0, 1, 0 }, false, 0.0 );
		if( !rig.Render( 1 ) )
			return 1;
		const Floats o       = rig.Output();
		const double globePx = rig.plugin.LastFrame().globePx;
		const double n       = gpaIndex( 0.60 );
		const double r       = 0.3e-3 / kRadius;
		double worstPos = 0.0, worstArea = 0.0;
		int seen = 0;
		std::string list;
		for( int k = 0; k < count; ++k )
		{
			double px = 0, py = 0;
			if( !harnessScreen( spots[ k ].x, spots[ k ].y, spots[ k ].z, n, globePx, px, py ) )
			{
				list += fmt( " %s: not reachable", spots[ k ].what );
				continue;
			}
			//The true area: a ball's image under the trace's 2 x 3 Jacobian
			//(central differences in x, y and z) is the ellipse J J^T, area
			//pi r^2 sqrt(det J J^T).
			const double h = 1e-4;
			double J[ 2 ][ 3 ] = {};
			for( int a = 0; a < 3; ++a )
			{
				double p[ 3 ] = { spots[ k ].x, spots[ k ].y, spots[ k ].z }, m[ 3 ] = { spots[ k ].x, spots[ k ].y, spots[ k ].z };
				p[ a ] += h;
				m[ a ] -= h;
				double ux = 0, uy = 0, vx = 0, vy = 0;
				harnessScreen( p[ 0 ], p[ 1 ], p[ 2 ], n, globePx, ux, uy );
				harnessScreen( m[ 0 ], m[ 1 ], m[ 2 ], n, globePx, vx, vy );
				J[ 0 ][ a ] = ( ux - vx ) / ( 2.0 * h );
				J[ 1 ][ a ] = ( uy - vy ) / ( 2.0 * h );
			}
			const double ja = J[ 0 ][ 0 ] * J[ 0 ][ 0 ] + J[ 0 ][ 1 ] * J[ 0 ][ 1 ] + J[ 0 ][ 2 ] * J[ 0 ][ 2 ];
			const double jb = J[ 0 ][ 0 ] * J[ 1 ][ 0 ] + J[ 0 ][ 1 ] * J[ 1 ][ 1 ] + J[ 0 ][ 2 ] * J[ 1 ][ 2 ];
			const double jc = J[ 1 ][ 0 ] * J[ 1 ][ 0 ] + J[ 1 ][ 1 ] * J[ 1 ][ 1 ] + J[ 1 ][ 2 ] * J[ 1 ][ 2 ];
			const double area = kPi * r * r * std::sqrt( std::max( ja * jc - jb * jb, 0.0 ) );
			double mx = 0, my = 0, mass = 0;
			const double half = std::max( 6.0, 4.0 * std::sqrt( area ) );
			if( !alphaCentroid( o, raster.w, raster.h, px, py, half, mx, my, &mass ) )
			{
				list += fmt( " %s: nothing drawn", spots[ k ].what );
				worstPos = std::numeric_limits< double >::quiet_NaN();
				continue;
			}
			++seen;
			const double e  = std::hypot( mx - px, my - py );
			const double ea = std::fabs( mass / area - 1.0 );
			worstPos        = worse( worstPos, e );
			worstArea       = worse( worstArea, ea );
			list += fmt( " %s %.3f px, area %.2f px^2 (%+.1f%%);", spots[ k ].what, e, area, 100.0 * ( mass / area - 1.0 ) );
		}
		Check( seen == count && worstPos <= kFlakePositionTolerance && worstArea <= kFlakeAreaTolerance,
		       fmt( "%dx%d: %d of %d seen; worst position %.4f px, worst area %.2f%%:%s", raster.w, raster.h, seen, count, worstPos, 100.0 * worstArea, list.c_str() ) );
	}
	return Verdict();
}
