//===========================================================================
// Time and the host: --rate, --clock, --resize, --alpha, --triggers.
//===========================================================================

/// 97 BPM: a quarter every 0.618557 s, on no frame boundary at 24, 60 or
/// 144 fps and no 8 ms step boundary within the runs here (marbling found
/// 120 BPM lands on frames at 24 and 60 and hides timing faults).
constexpr double kOffGridBpm = 97.0;

struct RateRun
{
	std::vector< Floats > frames;             ///< at the common instants
	std::vector< std::vector< V3 > > places;  ///< flakes' centres there
	std::vector< int64_t > shakes;
	double fastest = 0.0;                     ///< m/s, the quickest flake seen
};

bool rateRun( int w, int h, double fps, const Perturb& perturb, RateRun& out )
{
	Rig rig;
	rig.fps = fps;
	if( !startRig( rig, w, h, perturb ) )
		return false;
	rig.Set( PT_TRIGGER_ON, static_cast< float >( TriggerOn::Beat ) );
	rig.Set( PT_SHAKE, 0.5f );
	rig.beatBpm      = kOffGridBpm;
	const int every  = static_cast< int >( std::lround( fps / 2.0 ) );//each 0.5 s
	const int frames = static_cast< int >( std::lround( fps * 6.0 ) );
	for( int f = 1; f <= frames; ++f )
	{
		if( !rig.Render( 1 ) )
			return false;
		if( f % every != 0 )
			continue;
		out.frames.push_back( rig.Output() );
		const Physics ph = rig.plugin.CurrentPhysics();
		std::vector< V3 > p;
		rig.plugin.GlobeForTest().Positions( rig.plugin.LastFrame().clockUs, ph, p );
		out.places.push_back( p );
		const V3 W = rig.plugin.GlobeForTest().Swirl();
		out.fastest = std::max( out.fastest, std::sqrt( dot( W, W ) ) * kRadius + 0.01 );
	}
	for( const ShakeRecord& s : rig.plugin.ShakeLog() )
		out.shakes.push_back( s.us );
	return true;
}

int runRate( const Perturb& perturb )
{
	std::printf( "\n=== rate: the same run (the host's beat at 97 BPM, Shake 0.5, 600 flakes, 6 s) at 24, 60 and 144 fps; at every common half "
	             "second the shakes' instants agree within 2 us (the float bar phase's resolution, rounded), the flakes' centres within the fastest "
	             "flake's speed times that, the pixels within half an 8-bit code\n" );
	for( const Raster& raster : kRasters )
	{
		RateRun runs[ 3 ];
		const double rates[ 3 ] = { 24.0, 60.0, 144.0 };
		for( int k = 0; k < 3; ++k )
			if( !rateRun( raster.w, raster.h, rates[ k ], perturb, runs[ k ] ) )
				return 1;
		int64_t worstShake = 0;
		bool sameCount     = true;
		double worstPlace = 0.0, worstPixel = 0.0, fastest = 0.0;
		int identical = 0, compared = 0;
		for( int k = 1; k < 3; ++k )
		{
			sameCount = sameCount && runs[ k ].shakes.size() == runs[ 0 ].shakes.size() && runs[ k ].frames.size() == runs[ 0 ].frames.size();
			if( !sameCount )
				break;
			for( size_t s = 0; s < runs[ 0 ].shakes.size(); ++s )
				worstShake = std::max< int64_t >( worstShake, std::llabs( runs[ k ].shakes[ s ] - runs[ 0 ].shakes[ s ] ) );
			for( size_t f = 0; f < runs[ 0 ].frames.size(); ++f )
			{
				double px = 0.0;
				for( size_t i = 0; i < runs[ 0 ].frames[ f ].size(); ++i )
					px = worse( px, std::fabs( static_cast< double >( runs[ k ].frames[ f ][ i ] ) - runs[ 0 ].frames[ f ][ i ] ) );
				worstPixel = worse( worstPixel, px );
				identical += px == 0.0;
				++compared;
				for( size_t i = 0; i < runs[ 0 ].places[ f ].size(); ++i )
				{
					const V3 d = runs[ k ].places[ f ][ i ] - runs[ 0 ].places[ f ][ i ];
					worstPlace = worse( worstPlace, std::sqrt( dot( d, d ) ) );
				}
			}
			fastest = std::max( { fastest, runs[ k ].fastest, runs[ 0 ].fastest } );
		}
		const double placeTol = fastest * 2e-6 + 1e-12;
		Check( sameCount && !runs[ 0 ].shakes.empty() && worstShake <= 2 && worstPlace <= placeTol && worstPixel <= 0.5 / 255.0,
		       fmt( "%dx%d: %zu shakes each; instants agree within %lld us (tolerance 2); centres within %.3g m (tolerance %.3g); pixels within %.3g "
		            "(tolerance %.3g); %d of %d common frames bit-identical",
		            raster.w, raster.h, runs[ 0 ].shakes.size(), static_cast< long long >( worstShake ), worstPlace, placeTol, worstPixel, 0.5 / 255.0,
		            identical, compared ) );
	}
	return Verdict();
}

//===========================================================================
// --clock: Resolume's clock at ~499 million ms gives the run a seconds clock
// from zero gives.
//===========================================================================
int runClock( const Perturb& perturb )
{
	std::printf( "\n=== clock: the same beat-driven run (97 BPM, Shake 0.5, 4 s) with SetTime in seconds from 0 and in milliseconds from 499,000,000: "
	             "every output float and the state identical\n" );
	for( const Raster& raster : kRasters )
	{
		Floats out[ 2 ];
		uint64_t hash[ 2 ] = { 0, 0 };
		size_t shakes[ 2 ] = { 0, 0 };
		for( int k = 0; k < 2; ++k )
		{
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			if( k == 1 )
			{
				rig.msClock      = true;
				rig.timeOffsetMs = 4.99e8;
				rig.plugin.SetClockScaleForTest( 0.001 );
			}
			rig.Set( PT_TRIGGER_ON, static_cast< float >( TriggerOn::Beat ) );
			rig.Set( PT_SHAKE, 0.5f );
			rig.beatBpm = kOffGridBpm;
			if( !rig.Render( 240 ) )
				return 1;
			out[ k ]    = rig.Output();
			hash[ k ]   = rig.plugin.LastFrame().stateHash;
			shakes[ k ] = rig.plugin.ShakeLog().size();
		}
		Check( out[ 0 ] == out[ 1 ] && hash[ 0 ] == hash[ 1 ] && shakes[ 0 ] > 0 && shakes[ 0 ] == shakes[ 1 ],
		       fmt( "%dx%d: %zu and %zu shakes; output %s, state %s", raster.w, raster.h, shakes[ 0 ], shakes[ 1 ], out[ 0 ] == out[ 1 ] ? "identical" : "DIFFERS",
		            hash[ 0 ] == hash[ 1 ] ? "identical" : "DIFFERS" ) );
	}
	return Verdict();
}

//===========================================================================
// --resize: a resize changes only the drawing.
//===========================================================================
int runResize( const Perturb& perturb )
{
	std::printf( "\n=== resize: shaken at frames 10, 60 and 130, resized from WxH to W/2xH/2 at frame 100; against a run at W/2xH/2 throughout: the "
	             "state and the last frame identical\n" );
	for( const Raster& raster : kRasters )
	{
		const int w2 = raster.w / 2, h2 = raster.h / 2;
		Floats out[ 2 ];
		uint64_t hash[ 2 ] = { 0, 0 };
		for( int k = 0; k < 2; ++k )
		{
			Rig rig;
			if( !startRig( rig, k == 0 ? raster.w : w2, k == 0 ? raster.h : h2, perturb ) )
				return 1;
			for( int f = 0; f < 200; ++f )
			{
				if( k == 0 && f == 100 && !rig.Resize( w2, h2 ) )
					return 1;
				const bool press = f == 10 || f == 60 || f == 130;
				if( !( press ? rig.Press( PT_SHAKE_NOW ) : rig.Render( 1 ) ) )
					return 1;
			}
			out[ k ]  = rig.Output();
			hash[ k ] = rig.plugin.LastFrame().stateHash;
		}
		Check( out[ 0 ] == out[ 1 ] && hash[ 0 ] == hash[ 1 ],
		       fmt( "%dx%d -> %dx%d: state %s, last frame %s", raster.w, raster.h, w2, h2, hash[ 0 ] == hash[ 1 ] ? "identical" : "DIFFERS",
		            out[ 0 ] == out[ 1 ] ? "identical" : "DIFFERS" ) );
	}
	return Verdict();
}

//===========================================================================
// --alpha: Mix 0 hands the clip through; what is under a transparent texel
// never shows; an opaque card stays opaque inside the globe; outside it the
// output is transparent.
//===========================================================================
Floats alphaCard( int w, int h, bool whiteUnderClear )
{
	Floats card = lookCard( w, h );
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
		{
			float* p          = &card[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
			const int band    = ( x * 7 / w + y * 5 / h ) % 3;
			const float alpha = band == 0 ? 0.0f : band == 1 ? 0.5f : 1.0f;
			p[ 3 ]            = alpha;
			if( alpha == 0.0f )
				p[ 0 ] = p[ 1 ] = p[ 2 ] = whiteUnderClear ? 1.0f : 0.0f;
		}
	return card;
}

int runAlpha( const Perturb& perturb )
{
	std::printf( "\n=== alpha: Mix 0 is the clip bit for bit; white or black under the clip's transparent texels changes nothing at Mix 1 or 0.5; an "
	             "opaque card gives alpha 1 inside the globe and 0 outside it at Mix 1\n" );
	for( const Raster& raster : kRasters )
	{
		bool passThrough = false;
		int leaks        = 0;
		for( float mix : { 0.0f, 1.0f, 0.5f } )
		{
			Floats out[ 2 ];
			Floats cards[ 2 ];
			for( int k = 0; k < 2; ++k )
			{
				Rig rig;
				cards[ k ] = alphaCard( raster.w, raster.h, k == 1 );
				if( !startRig( rig, raster.w, raster.h, perturb, &cards[ k ] ) )
					return 1;
				rig.Set( PT_MIX, mix );
				if( !rig.Press( PT_SHAKE_NOW ) || !rig.Render( 30 ) )
					return 1;
				out[ k ] = rig.Output();
			}
			if( mix == 0.0f )
				passThrough = out[ 0 ] == cards[ 0 ] && out[ 1 ] == cards[ 1 ];
			else
				for( size_t i = 0; i < out[ 0 ].size(); ++i )
					leaks += out[ 0 ][ i ] != out[ 1 ][ i ];
		}
		//The opaque card.
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		if( !rig.Press( PT_SHAKE_NOW ) || !rig.Render( 30 ) )
			return 1;
		const Floats o       = rig.Output();
		const double globePx = rig.plugin.LastFrame().globePx;
		int insideNotOpaque = 0, outsideNotClear = 0, inside = 0, outside = 0;
		for( int y = 0; y < raster.h; ++y )
			for( int x = 0; x < raster.w; ++x )
			{
				const double r = std::hypot( x + 0.5 - 0.5 * raster.w, y + 0.5 - 0.5 * raster.h );
				const float a  = o[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 + 3 ];
				if( r < globePx - 1.0 )
				{
					++inside;
					insideNotOpaque += a != 1.0f;
				}
				else if( r > globePx + 1.0 )
				{
					++outside;
					outsideNotClear += a != 0.0f;
				}
			}
		Check( passThrough && leaks == 0 && insideNotOpaque == 0 && outsideNotClear == 0 && inside > 0 && outside > 0,
		       fmt( "%dx%d: Mix 0 the clip bit for bit: %s; floats changed by white under clear texels at Mix 1 and 0.5: %d; opaque card: %d of %d "
		            "pixels inside not opaque, %d of %d outside not clear",
		            raster.w, raster.h, passThrough ? "yes" : "NO", leaks, insideNotOpaque, inside, outsideNotClear, outside ) );
	}
	return Verdict();
}

//===========================================================================
// --triggers: the button, the host's beat inside the frame, kicks from the
// lowest band only.
//===========================================================================
int runTriggers( const Perturb& perturb )
{
	std::printf( "\n=== triggers: three presses are three full shakes at their frames; at 97 BPM every quarter beat a shake within 2 us of its "
	             "instant; the harness's music gives one shake per kick, each 0-40 ms after it (earshot's detection; marbling measured 0-26.7 ms), "
	             "and its hats alone none\n" );
	const Raster raster = kRasters.back();
	//The button.
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		std::vector< int64_t > at;
		for( int f = 0; f < 60; ++f )
		{
			const bool press = f == 5 || f == 17 || f == 40;
			if( !( press ? rig.Press( PT_SHAKE_NOW ) : rig.Render( 1 ) ) )
				return 1;
			if( press )
				at.push_back( rig.plugin.LastFrame().clockUs );
		}
		const auto& log = rig.plugin.ShakeLog();
		bool ok         = log.size() == 3;
		for( size_t k = 0; ok && k < 3; ++k )
			ok = log[ k ].us == at[ k ] && log[ k ].strength == 1.0;
		Check( ok, fmt( "the button: 3 presses, %zu shakes, at their frames at full strength: %s", log.size(), ok ? "yes" : "NO" ) );
	}
	//The beat.
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		rig.Set( PT_TRIGGER_ON, static_cast< float >( TriggerOn::Beat ) );
		rig.Set( PT_SHAKE, 0.4f );
		rig.beatBpm = kOffGridBpm;
		if( !rig.Render( 300 ) )
			return 1;
		const auto& log  = rig.plugin.ShakeLog();
		const double end = static_cast< double >( rig.plugin.LastFrame().clockUs ) * 1e-6;
		const int expect = static_cast< int >( std::floor( end * kOffGridBpm / 60.0 ) );
		int64_t worst    = 0;
		bool strengths   = true;
		for( size_t k = 0; k < log.size(); ++k )
		{
			const int64_t exact = std::llround( static_cast< double >( k + 1 ) * 60.0 / kOffGridBpm * 1e6 );
			worst               = std::max< int64_t >( worst, std::llabs( log[ k ].us - exact ) );
			strengths           = strengths && std::fabs( log[ k ].strength - 0.4f ) < 1e-6;
		}
		Check( static_cast< int >( log.size() ) == expect && worst <= 2 && strengths,
		       fmt( "the beat: %zu shakes in %.2f s (%d quarters), worst %lld us from the beat's instant (tolerance 2), at the Shake gain: %s", log.size(),
		            end, expect, static_cast< long long >( worst ), strengths ? "yes" : "NO" ) );
	}
	//Kicks, then hats alone.
	for( int hats = 0; hats < 2; ++hats )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		rig.Set( PT_TRIGGER_ON, static_cast< float >( TriggerOn::Kick ) );
		rig.audio = hats ? hatsOnly : musicAt;
		if( !rig.Render( 300 ) )
			return 1;
		const auto& log = rig.plugin.ShakeLog();
		if( hats )
		{
			Check( log.empty(), fmt( "hats alone (no kick): %zu shakes (want none)", log.size() ) );
			continue;
		}
		const double end = static_cast< double >( rig.plugin.LastFrame().clockUs ) * 1e-6;
		int matched = 0, stray = 0;
		double worstLag = 0.0;
		for( const ShakeRecord& s : log )
		{
			const double t    = static_cast< double >( s.us ) * 1e-6;
			const double kick = std::floor( t / 0.5 + 1e-9 ) * 0.5;
			const double lag  = t - kick;
			if( lag >= 0.0 && lag <= 0.040 )
			{
				++matched;
				worstLag = std::max( worstLag, lag );
			}
			else
				++stray;
		}
		//Kicks at 0, 0.5, ...: the first frame primes, so the kick at 0 may go.
		const int kicks = static_cast< int >( std::floor( end / 0.5 ) );
		Check( stray == 0 && matched >= kicks - 1 && matched <= kicks + 1,
		       fmt( "the music: %zu shakes in %.2f s, %d of them 0-40 ms after a kick (worst %.1f ms), %d stray; %d kicks after the first", log.size(), end,
		            matched, 1e3 * worstLag, stray, kicks ) );
	}
	return Verdict();
}
