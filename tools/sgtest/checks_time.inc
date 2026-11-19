//===========================================================================
// Time and the host: --rate, --clock, --resize, --alpha, --triggers, --hostfft.
//===========================================================================

/// 97 BPM: a quarter every 0.618557 s, on no frame boundary at 24, 60 or
/// 144 fps and no 8 ms step boundary within the runs here (marbling found
/// 120 BPM lands on frames at 24 and 60 and hides timing faults).
constexpr double kOffGridBpm = 97.0;

struct RateRun
{
	std::vector< Floats > frames;             ///< at the common instants
	std::vector< std::vector< V3 > > places;  ///< flakes' centres there
	std::vector< uint64_t > hashes;           ///< the stored state there
	std::vector< int64_t > shakes;            ///< the placed shakes' instants
	std::vector< int64_t > beats;             ///< the beat run's shake instants
};

/// Three shakes placed by the harness at instants on no frame at 24, 60 or
/// 144 fps and on no 8 ms step: the stored state and the picture at every
/// common half second must be the same bits at every rate (the state is a
/// function of the events' instants alone, Globe.h). Then the host's beat at
/// 97 BPM for 6 s: its shakes' instants within 2 us.
bool rateRun( int w, int h, double fps, const Perturb& perturb, RateRun& out )
{
	{
		Rig rig;
		rig.fps = fps;
		if( !startRig( rig, w, h, perturb ) )
			return false;
		rig.plugin.QueueShakeForTest( 317'100, 1.0 );
		rig.plugin.QueueShakeForTest( 1'234'500, 0.6 );
		rig.plugin.QueueShakeForTest( 2'000'700, 0.8 );
		const int every  = static_cast< int >( std::lround( fps / 2.0 ) );
		const int frames = static_cast< int >( std::lround( fps * 4.0 ) );
		for( int f = 1; f <= frames; ++f )
		{
			if( !rig.Render( 1 ) )
				return false;
			//Frame f is at (f - 1) / fps: the common instants are k / 2 s.
			if( ( f - 1 ) % every != 0 || f == 1 )
				continue;
			out.frames.push_back( rig.Output() );
			std::vector< V3 > p;
			rig.plugin.GlobeForTest().Positions( rig.plugin.LastFrame().clockUs, rig.plugin.CurrentPhysics(), p );
			out.places.push_back( p );
			out.hashes.push_back( rig.plugin.LastFrame().stateHash );
		}
		for( const ShakeRecord& s : rig.plugin.ShakeLog() )
			out.shakes.push_back( s.us );
	}
	{
		Rig rig;
		rig.fps = fps;
		if( !startRig( rig, w, h, perturb ) )
			return false;
		rig.Set( PT_TRIGGER_ON, static_cast< float >( TriggerOn::Beat ) );
		rig.Set( PT_SHAKE, 0.5f );
		rig.beatBpm = kOffGridBpm;
		if( !rig.Render( static_cast< int >( std::lround( fps * 6.0 ) ) ) )
			return false;
		for( const ShakeRecord& s : rig.plugin.ShakeLog() )
			out.beats.push_back( s.us );
	}
	return true;
}

int runRate( const Perturb& perturb )
{
	std::printf( "\n=== rate: three shakes placed at 0.3171, 1.2345 and 2.0007 s (on no frame and no 8 ms step), at 24, 60 and 144 fps: at every "
	             "common half second to 4 s the state, every flake's centre and every output float the same bits; the host's beat at 97 BPM "
	             "for 6 s: the same shakes, within 2 us (the float bar phase, 2^-24 of a bar = 0.15 us here, and the us rounding)\n" );
	for( const Raster& raster : kRasters )
	{
		RateRun runs[ 3 ];
		const double rates[ 3 ] = { 60.0, 24.0, 144.0 };
		for( int k = 0; k < 3; ++k )
			if( !rateRun( raster.w, raster.h, rates[ k ], perturb, runs[ k ] ) )
				return 1;
		for( int k = 1; k < 3; ++k )
		{
			const RateRun &a = runs[ 0 ], &b = runs[ k ];
			const bool sameCounts = a.frames.size() == b.frames.size() && a.shakes.size() == b.shakes.size() && a.beats.size() == b.beats.size();
			size_t floats = 0, places = 0, hashes = 0;
			int64_t placed = 0, beat = 0;
			for( size_t f = 0; sameCounts && f < a.frames.size(); ++f )
			{
				for( size_t i = 0; i < a.frames[ f ].size(); ++i )
					floats += a.frames[ f ][ i ] != b.frames[ f ][ i ];
				for( size_t i = 0; i < a.places[ f ].size(); ++i )
					places += a.places[ f ][ i ].x != b.places[ f ][ i ].x || a.places[ f ][ i ].y != b.places[ f ][ i ].y
					          || a.places[ f ][ i ].z != b.places[ f ][ i ].z;
				hashes += a.hashes[ f ] != b.hashes[ f ];
			}
			for( size_t i = 0; sameCounts && i < a.shakes.size(); ++i )
				placed = std::max< int64_t >( placed, std::llabs( a.shakes[ i ] - b.shakes[ i ] ) );
			for( size_t i = 0; sameCounts && i < a.beats.size(); ++i )
				beat = std::max< int64_t >( beat, std::llabs( a.beats[ i ] - b.beats[ i ] ) );
			const bool enough = a.frames.size() == 7 && a.shakes.size() == 3 && a.beats.size() >= 9;
			Check( sameCounts && enough && floats == 0 && places == 0 && hashes == 0 && placed == 0,
			       fmt( "%dx%d, %g fps against 60: %zu common frames; placed shakes %lld us apart; state differs at %zu, flake centres at %zu, "
			            "output floats at %zu (want 0, 0, 0, 0)",
			            raster.w, raster.h, rates[ k ], a.frames.size(), static_cast< long long >( placed ), hashes, places, floats ) );
			Check( sameCounts && enough && beat <= 2,
			       fmt( "%dx%d, %g fps against 60: %zu and %zu beat shakes, worst %lld us apart (tolerance 2)", raster.w, raster.h, rates[ k ],
			            a.beats.size(), b.beats.size(), static_cast< long long >( beat ) ) );
		}
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
		const Floats o = rig.Output();
		//The spec's silhouette at the default Globe: diameter 0.9 of the
		//frame's height. Not the plugin's own figure, which a wrong one
		//would agree with.
		const double globePx = 0.5 * 0.9 * raster.h;
		int insideNotOpaque = 0, outsideNotClear = 0, inside = 0, outside = 0;
		double sumAlpha = 0.0;
		for( int y = 0; y < raster.h; ++y )
			for( int x = 0; x < raster.w; ++x )
			{
				const double r = std::hypot( x + 0.5 - 0.5 * raster.w, y + 0.5 - 0.5 * raster.h );
				const float a  = o[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 + 3 ];
				sumAlpha += a;
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
		//The silhouette's radius off the alpha alone: sqrt(sum / pi). With
		//alpha 1 inside r - 1 and 0 outside r + 1, the sum lies between the
		//two discs' areas, so the radius is within 1 px (the lattice's own
		//miscount, ~sqrt(r) pixels, is under 0.03 px of radius here).
		const double measured = std::sqrt( sumAlpha / kPi );
		Check( passThrough && leaks == 0 && insideNotOpaque == 0 && outsideNotClear == 0 && inside > 0 && outside > 0 &&
		           std::fabs( measured - globePx ) <= 1.0,
		       fmt( "%dx%d: Mix 0 the clip bit for bit: %s; floats changed by white under clear texels at Mix 1 and 0.5: %d; opaque card: %d of %d "
		            "pixels inside not opaque, %d of %d outside not clear; the silhouette's radius off the alpha %.3f px (spec 0.45 H = %.3f; "
		            "tolerance 1 px)",
		            raster.w, raster.h, passThrough ? "yes" : "NO", leaks, insideNotOpaque, inside, outsideNotClear, outside, measured, globePx ) );
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
	             "instant, the plugin's own beat when the host's bar phase stands still; the harness's music gives one shake per kick, 0-26.7 ms "
	             "after it, and its hats alone none\n" );
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
	//A host that never moves its bar phase: the plugin's own beat at its BPM.
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		rig.Set( PT_TRIGGER_ON, static_cast< float >( TriggerOn::Beat ) );
		rig.beforeFrame = [ &rig ]( int ) { rig.plugin.SetBeatInfo( static_cast< float >( kOffGridBpm ), 0.0f ); };
		if( !rig.Render( 300 ) )
			return 1;
		const auto& log  = rig.plugin.ShakeLog();
		const double end = static_cast< double >( rig.plugin.LastFrame().clockUs ) * 1e-6;
		const int expect = static_cast< int >( std::floor( end * kOffGridBpm / 60.0 ) );
		int64_t worst    = 0;
		for( size_t k = 0; k < log.size(); ++k )
			worst = std::max< int64_t >( worst, std::llabs( log[ k ].us - std::llround( static_cast< double >( k + 1 ) * 60.0 / kOffGridBpm * 1e6 ) ) );
		Check( static_cast< int >( log.size() ) == expect && worst <= 2,
		       fmt( "a bar phase stuck at 0: %zu shakes in %.2f s (%d quarters of the plugin's own beat), worst %lld us off (tolerance 2)", log.size(),
		            end, expect, static_cast< long long >( worst ) ) );
	}
	//Kicks, then hats alone. earshot stamps an onset at the end of the first
	//1024-sample window (21.3 ms) whose flux crosses and analyses whole
	//256-sample hops (5.3 ms): a shake lands 0 .. 26.7 ms after its kick
	//(marbling's bound, from the same snapshot).
	const double late = ( 1024.0 + 256.0 ) / kRate + 1e-6;
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
			Check( log.empty(), fmt( "hats alone (no kick), 5 s: %zu shakes (want none)", log.size() ) );
			continue;
		}
		//The first second warms earshot's thresholds up (marbling's rule).
		int unmatched = 0, missed = 0, doubled = 0, n = 0;
		double worst = 0.0;
		std::vector< int > hits( 16, 0 );
		for( const ShakeRecord& sr : log )
		{
			const double t = static_cast< double >( sr.us ) * 1e-6;
			if( t < 1.0 )
				continue;
			++n;
			const int kick   = static_cast< int >( std::floor( t / 0.5 + 1e-9 ) );
			const double lag = t - kick * 0.5;
			if( lag < -1e-6 || lag > late )
				++unmatched;
			else if( kick < 16 )
				++hits[ static_cast< size_t >( kick ) ];
			worst = std::max( worst, lag );
		}
		for( int k = 2; k < 10; ++k )
		{
			missed += hits[ static_cast< size_t >( k ) ] == 0;
			doubled += hits[ static_cast< size_t >( k ) ] > 1;
		}
		Check( unmatched == 0 && missed == 0 && doubled == 0 && n > 0,
		       fmt( "the music, 1..5 s: %d shakes; %d outside 0..%.1f ms of a kick, %d of 8 kicks missed, %d doubled; latest %.1f ms after its kick",
		            n, unmatched, late * 1e3, missed, doubled, worst * 1e3 ) );
	}
	return Verdict();
}

//===========================================================================
// --hostfft: Kick on Resolume's own FFT, earshot's host path alone. A drum
// loop becomes the 64 bins Arena 7.29.0 was measured to send (earshot's
// AGENTS.md, "Resolume's 64 bins, measured") and is handed to the plugin as
// its FF_USAGE_FFT buffer, element by element, as a host does; nothing goes
// through feed(). Under earshot 8124191 (each bin whole to the band holding
// its centre) band 0 got no bin and this never shook.
//===========================================================================

/// Arena's FFT buffer as measured: every 2048 samples of the engine's rate, a
/// 2048-point Hann-windowed DFT of the newest 2048 samples (the mono sum
/// (L + R) / 2: the loop is the same on both sides), and bin k the sum of
/// |X_i| / 2N over the 16 fine bins 16k .. 16k + 15. That is linear, fs / 128
/// a bin (375 Hz at 48 kHz) to fs / 2, a magnitude, a sine of amplitude A
/// reading A / 4 in its bin and 0.75 of that split 1 : 3 on a boundary, as
/// measured. Held between updates, as Arena holds it.
struct HostSpectrum
{
	static constexpr int kN = 2048;
	int rate;
	std::function< double( double ) > signal;
	std::vector< float > bins = std::vector< float >( 64, 0.0f );
	int64_t updated           = -1;

	HostSpectrum( int r, std::function< double( double ) > s ) : rate( r ), signal( std::move( s ) ) {}

	const std::vector< float >& at( double t )
	{
		const int64_t u = static_cast< int64_t >( std::floor( t * rate / kN + 1e-9 ) );
		if( u == updated )
			return bins;
		updated = u;
		std::vector< std::complex< double > > x( kN );
		for( int n = 0; n < kN; ++n )
		{
			const int64_t sample = u * kN - kN + n;
			const double hann    = 0.5 - 0.5 * std::cos( 2.0 * kPi * n / kN );
			x[ static_cast< size_t >( n ) ] = hann * signal( static_cast< double >( sample ) / rate );
		}
		//Radix-2, in place.
		for( int i = 1, j = 0; i < kN; ++i )
		{
			int bit = kN >> 1;
			for( ; j & bit; bit >>= 1 )
				j ^= bit;
			j ^= bit;
			if( i < j )
				std::swap( x[ static_cast< size_t >( i ) ], x[ static_cast< size_t >( j ) ] );
		}
		for( int len = 2; len <= kN; len <<= 1 )
		{
			const std::complex< double > w = std::polar( 1.0, -2.0 * kPi / len );
			for( int i = 0; i < kN; i += len )
			{
				std::complex< double > wk( 1.0, 0.0 );
				for( int k = 0; k < len / 2; ++k )
				{
					const auto a = x[ static_cast< size_t >( i + k ) ], b = x[ static_cast< size_t >( i + k + len / 2 ) ] * wk;
					x[ static_cast< size_t >( i + k ) ]           = a + b;
					x[ static_cast< size_t >( i + k + len / 2 ) ] = a - b;
					wk *= w;
				}
			}
		}
		for( int k = 0; k < 64; ++k )
		{
			double sum = 0.0;
			for( int i = 16 * k; i < 16 * k + 16; ++i )
				sum += std::abs( x[ static_cast< size_t >( i ) ] );
			bins[ static_cast< size_t >( k ) ] = static_cast< float >( sum / ( 2.0 * kN ) );
		}
		return bins;
	}
};

/// 128 BPM (Arena's Beat 001), so the kicks fall at every phase of the
/// 2048-sample updates. The parts: the harness's kick on every beat; a clap on
/// two and four and hats on the eighths between, as noise twice differenced
/// (-52 dB at 375 Hz), so they put nothing in bin 0; a 49 Hz bass held
/// throughout, in bin 0 with the kick.
constexpr double kLoopBeat = 60.0 / 128.0;
enum LoopPart : int
{
	kLoopKick = 1,
	kLoopTop  = 2,///< claps and hats
	kLoopBass = 4
};

double drumLoopAt( double t, int parts, int rate )
{
	if( t < 0.0 )
		return 0.0;
	//The sample's own index, rounded: truncating t * rate repeats and skips
	//samples, and a differenced noise that does is no longer high-passed (it
	//put a click's worth into bin 0 and shook the globe once in 6 s).
	const uint32_t n  = static_cast< uint32_t >( std::llround( t * rate ) );
	const double beat = std::fmod( t, kLoopBeat );
	double s          = 0.0;
	if( parts & kLoopBass )
		s += 0.2 * std::sin( 2.0 * kPi * 49.0 * t );
	if( parts & kLoopKick )
		s += 0.6 * std::exp( -beat / 0.06 ) * std::sin( 2.0 * kPi * ( 55.0 + 60.0 * std::exp( -beat / 0.02 ) ) * beat );
	if( parts & kLoopTop )
	{
		auto hiss = [ n ]( uint32_t seed ) {
			return Hash01( n, seed, 0u ) - 2.0 * Hash01( n - 1u, seed, 0u ) + Hash01( n - 2u, seed, 0u );
		};
		const double clap = std::fmod( t, 2.0 * kLoopBeat ) - kLoopBeat;
		if( clap >= 0.0 )
			s += 0.3 * std::exp( -clap / 0.03 ) * hiss( 11u );
		const double hat = std::fmod( t + kLoopBeat / 2.0, kLoopBeat );
		s += 0.2 * std::exp( -hat / 0.01 ) * hiss( 7u );
	}
	return s;
}

int runHostFft( const Perturb& perturb )
{
	std::printf( "\n=== hostfft: Kick on Resolume's FFT alone (the bins as Arena 7.29.0 sends them, nothing fed): a drum loop at 128 BPM gives "
	             "one shake per kick, at 48 and at 44.1 kHz, within two of the host's updates and a frame; claps and hats above bin 0 none; over a "
	             "bass held in bin 0 no kick is missed (the extra shakes the bass's beating gives are counted); earshot reads the bins at the "
	             "host's rate\n" );
	const Raster raster = kRasters.back();
	struct Run
	{
		int rate;
		int parts;
		const char* what;
	};
	const Run runs[] = {
		{ 48000, kLoopKick | kLoopTop, "the drum loop" },
		{ 44100, kLoopKick | kLoopTop, "the drum loop" },
		{ 48000, kLoopTop, "claps and hats alone" },
		{ 48000, kLoopKick | kLoopTop | kLoopBass, "the drum loop over a held 49 Hz bass" },
	};
	for( const Run& run : runs )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		rig.plugin.SetSampleRate( static_cast< unsigned int >( run.rate ) );
		rig.Set( PT_TRIGGER_ON, static_cast< float >( TriggerOn::Kick ) );
		rig.Set( PT_INPUT, 0.0f );//Resolume FFT
		const int parts = run.parts, rate = run.rate;
		HostSpectrum host( run.rate, [ parts, rate ]( double t ) { return drumLoopAt( t, parts, rate ); } );
		rig.beforeFrame = [ &rig, &host ]( int f ) {
			const std::vector< float >& b = host.at( rig.Seconds( f ) );
			for( unsigned int j = 0; j < 64; ++j )
				rig.plugin.SetParamElementValue( PT_AUDIO, j, b[ j ] );
		};
		if( !rig.Render( 360 ) )
			return 1;
		const double hostRate = rig.plugin.AudioForTest().HostRateForTest();
		const auto& log       = rig.plugin.ShakeLog();
		if( !( parts & kLoopKick ) )
		{
			std::string at;
			for( const ShakeRecord& sr : log )
				at += fmt( " %.3f s", static_cast< double >( sr.us ) * 1e-6 );
			Check( log.empty(), fmt( "%s, %d Hz, 6 s: %zu shakes (want none)%s", run.what, run.rate, log.size(), at.empty() ? "" : ( ", at" + at ).c_str() ) );
			continue;
		}
		//Seen by the second update after the kick at the latest (the first may
		//hold only its window's Hann-weighted tail), then read at the next frame.
		const double late = 2.0 * HostSpectrum::kN / run.rate + 1.0 / rig.fps + 1e-6;
		int unmatched = 0, missed = 0, doubled = 0, n = 0, expected = 0;
		double worst = 0.0;
		std::vector< int > hits( 16, 0 );
		for( const ShakeRecord& sr : log )
		{
			const double t = static_cast< double >( sr.us ) * 1e-6;
			if( t < 1.0 )
				continue;//the first second primes earshot's floors
			++n;
			const int k      = static_cast< int >( std::floor( t / kLoopBeat + 1e-9 ) );
			const double lag = t - k * kLoopBeat;
			if( lag > late || k >= 16 )
				++unmatched;
			else
			{
				++hits[ static_cast< size_t >( k ) ];
				worst = std::max( worst, lag );
			}
		}
		for( int k = 0; k < 16; ++k )
		{
			const double at = k * kLoopBeat;
			if( at < 1.0 || at + late > 6.0 - 1.0 / rig.fps )
				continue;
			++expected;
			missed += hits[ static_cast< size_t >( k ) ] == 0;
			doubled += hits[ static_cast< size_t >( k ) ] > 1;
		}
		if( run.rate != 48000 || !( parts & kLoopBass ) )
			Check( std::fabs( hostRate - run.rate ) < 1e-9, fmt( "the host says %d Hz: earshot reads its bins at %.0f Hz", run.rate, hostRate ) );
		const std::string line = fmt( "%s, %d Hz, 1..6 s: %d shakes for %d kicks; %d missed, %d doubled, latest %.1f ms after its kick "
		                              "(limit %.1f); %d not within that of a kick",
		                              run.what, run.rate, n, expected, missed, doubled, worst * 1e3, late * 1e3, unmatched );
		if( parts & kLoopBass )
			Check( n > 0 && missed == 0, line + " (counted, not refused: a held bass in bin 0 beats against the kick's tail)" );
		else
			Check( n > 0 && unmatched == 0 && missed == 0 && doubled == 0, line );
	}
	//A rate no engine runs at is not one: earshot keeps its 48 kHz.
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		rig.plugin.SetSampleRate( 0u );
		rig.Set( PT_TRIGGER_ON, static_cast< float >( TriggerOn::Kick ) );
		if( !rig.Render( 2 ) )
			return 1;
		const double hostRate = rig.plugin.AudioForTest().HostRateForTest();
		Check( std::fabs( hostRate - 48000.0 ) < 1e-9, fmt( "a host rate of 0: earshot keeps %.0f Hz (want 48000)", hostRate ) );
	}
	return Verdict();
}
