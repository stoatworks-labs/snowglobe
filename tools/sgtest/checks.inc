//===========================================================================
// The checks. Each sets every control it depends on (honeydew's lesson),
// and every picture check runs at each raster in kRasters.
//===========================================================================
#include "checks_geometry.inc"
#include "checks_time.inc"
#include "checks_host.inc"

//===========================================================================
// The registry.
//===========================================================================
struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< needs no GL context
};

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "drop", runDrop, false },         { "rings", runRings, false },       { "jacobian", runJacobian, false }, { "ink", runInk, false },
		{ "undo", runUndo, false },         { "period", runPeriod, false },     { "resample", runResample, false }, { "rate", runRate, false },
		{ "clock", runClock, false },       { "resize", runResize, false },     { "paper", runPaper, false },       { "alpha", runAlpha, false },
		{ "triggers", runTriggers, false }, { "coreaudio", runCoreaudio, false }, { "glstate", runGlstate, false }, { "robust", runRobust, false },
		{ "model", runModel, true },        { "names", runNames, true },        { "cues", runCues, true },
	};
	return list;
}

bool isOffline( const char* name )
{
	for( const CheckEntry& c : checks() )
		if( std::strcmp( c.flag, name ) == 0 )
			return c.offline;
	return false;
}

/// Every check's wrong models. Each must make its check FAIL; a check with
/// none is refused (earshot's rule: a check that cannot fail is not one).
int runNegative( bool offlineOnly = false, const std::string& only = "" )
{
	struct Case
	{
		const char* name;
		Perturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, const char* what, int hooks, int cpuHooks, int model ) {
		Perturb p;
		p.hooks    = hooks;
		p.cpuHooks = cpuHooks;
		p.model    = model;
		cases.push_back( { name, p, what } );
	};
	using namespace shaders;
	add( "drop", "a drop's push given exponent 0.6, not 1/2", kHookPushExponent, 0, 0 );
	add( "rings", "a drop's push given exponent 0.6, not 1/2", kHookPushExponent, 0, 0 );
	add( "jacobian", "every tool perturbed off area-preserving (shear, rotation, push)", kHookCompressible, 0, 0 );
	add( "ink", "the map read bilinear across ink too: inks blended at their edges", kHookBlendInk, 0, 0 );
	add( "undo", "every tool perturbed off area-preserving", kHookCompressible, 0, 0 );
	add( "undo", "the comb's fold taken modulo 1.25 s", kHookFoldSpacing, 0, 0 );
	add( "period", "the comb's fold taken modulo 1.25 s", kHookFoldSpacing, 0, 0 );
	add( "resample", "the baked map read nearest, not bilinear", kHookNearestMap, 0, 0 );
	add( "rate", "beats placed at the frame, not inside it", 0, kCpuEventsAtFrame, 0 );
	add( "clock", "the host's clock read through a float", 0, kCpuFloatClock, 0 );
	add( "resize", "a resize loses the map instead of resampling it", 0, kCpuResizeClears, 0 );
	add( "paper", "Paper does nothing", 0, kCpuIgnorePaper, 0 );
	add( "alpha", "the clip sampled straight, not premultiplied", kHookNoPremultiply, 0, 0 );
	add( "triggers", "beats and onsets placed at the frame, not inside it", 0, kCpuEventsAtFrame, 0 );
	add( "coreaudio", "the device menu listed and Audio Input applied whatever Trigger On says", 0, kCpuIgnoreGate, 0 );
	add( "glstate", "the display pass leaves its program and textures bound", 0, kCpuLeakState, 0 );
	add( "robust", "nothing ever baked: ops pile up past what the shader traces", 0, kCpuNoBake, 0 );
	add( "model", "a drop in progress composed in r, not r^2", 0, 0, 1 );
	add( "names", "a seventeen-character name added", 0, 0, 2 );
	add( "cues", "every control ramped between keys", 0, 0, 3 );

	int unfalsifiable = 0, missing = 0;
	for( const CheckEntry& check : checks() )
	{
		bool has = false;
		for( const Case& c : cases )
			has = has || std::strcmp( c.name, check.flag ) == 0;
		if( !has )
		{
			std::printf( "  FAIL  --%s has no negative control\n", check.flag );
			++missing;
		}
	}
	if( !only.empty() )
		cases.erase( std::remove_if( cases.begin(), cases.end(), [ & ]( const Case& c ) { return only != c.name; } ), cases.end() );
	if( offlineOnly )
		cases.erase( std::remove_if( cases.begin(), cases.end(), []( const Case& c ) { return !isOffline( c.name ); } ), cases.end() );

	for( const Case& c : cases )
	{
		CheckFn fn = nullptr;
		for( const CheckEntry& check : checks() )
			if( std::strcmp( check.flag, c.name ) == 0 )
				fn = check.run;
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		fn( c.perturb );
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed %d check%s, as it must\n", c.name, observed, observed == 1 ? "" : "s" );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected; %d checks without one\n", cases.size(), unfalsifiable, missing );
	const bool ok = unfalsifiable == 0 && missing == 0;
	std::printf( "\n  %s\n", ok ? "PASS" : "FAIL" );
	return ok ? 0 : 1;
}

int runOffline()
{
	int failed = 0;
	for( const CheckEntry& check : checks() )
		if( check.offline )
			failed |= check.run( Perturb {} );
	failed |= runNegative( true );
	std::printf( "\n  offline: the checks that need no GL context. The picture checks were NOT run --\n"
	             "  tools/verify.sh runs them against a real driver, at 1280x720 and 320x180, and again on the software renderer.\n"
	             "\n  %s\n",
	             failed == 0 && g_failures == 0 ? "PASS" : "FAIL" );
	return failed == 0 && g_failures == 0 ? 0 : 1;
}

int runAll()
{
	int failed = 0;
	for( const CheckEntry& check : checks() )
		failed |= check.run( Perturb {} );
	std::printf( "\n  all: %d checks, %d failed\n\n  %s\n", g_checks, g_failures, g_failures == 0 && failed == 0 ? "PASS" : "FAIL" );
	return g_failures == 0 && failed == 0 ? 0 : 1;
}

//===========================================================================
// --bench: GPU time per frame from GL_TIME_ELAPSED, and wall time.
//===========================================================================
int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
	};
	const Size sizes[] = { { 1280, 720, "720p" }, { 1920, 1080, "1080p" }, { 3840, 2160, "4K" } };
	struct Case
	{
		const char* what;
		std::function< void( Rig& ) > set;
		bool at4K;
	};
	const Case cases[] = {
		{ "defaults, host beat 120 BPM",
		  []( Rig& r ) {
			  r.Set( PT_TRIGGER_ON, static_cast< float >( TriggerOn::Beat ) );
			  r.beatBpm = 120.0;
		  },
		  true },
		{ "64 ops traced (the most a frame can)",
		  []( Rig& r ) {
			  r.plugin.SetHooksForTest( 0, kCpuNoBake );
			  for( int k = 0; k < kMaxOps; ++k )
				  r.Inject( k % 4 == 0 ? dropOp( 0.3 * std::sin( k ), 0.2 * std::cos( k ), 0.03 ) : combOp( k % 2 ? 1 : 0, k % 2 ? 0 : 1, 0.01 * k, 0.05, 0.05, 0.01, k % 3 == 0 ? 0.01 : 0.0, 12.0, 0.0 ) );
		  },
		  false },
	};
	std::printf( "\n=== bench: GPU time per frame (GL_TIME_ELAPSED) and wall time per frame, after 240 frames of warm-up, median and worst of 120 "
	             "(4K: 30 and 30, defaults only; the machine is shared, so these wander)\n" );
	{
		Rig warm;
		if( !warm.Init( 1920, 1080 ) || !warm.Render( 300 ) )
			return 1;
	}
	GLuint query = 0;
	glGenQueries( 1, &query );
	for( const Case& c : cases )
		for( const Size& size : sizes )
		{
			const bool fourK = size.w == 3840;
			if( fourK && !c.at4K )
				continue;
			Rig rig;
			if( !rig.Init( size.w, size.h ) )
				return 1;
			rig.Set( PT_MIX, 1.0f );
			c.set( rig );
			if( !rig.Render( fourK ? 30 : 240 ) )
				return 1;
			const int kTimed = fourK ? 30 : 120;
			std::vector< double > gpu, wall;
			for( int f = 0; f < kTimed; ++f )
			{
				const auto start = std::chrono::steady_clock::now();
				glBeginQuery( GL_TIME_ELAPSED, query );
				if( !rig.Render( 1 ) )
					return 1;
				glEndQuery( GL_TIME_ELAPSED );
				GLuint64 ns = 0;
				glGetQueryObjectui64v( query, GL_QUERY_RESULT, &ns );
				gpu.push_back( static_cast< double >( ns ) * 1e-6 );
				wall.push_back( std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count() );
			}
			std::sort( gpu.begin(), gpu.end() );
			std::sort( wall.begin(), wall.end() );
			std::printf( "  %-38s %-6s GPU median %6.2f ms, worst %6.2f (%5.1f%% of a 60 fps frame); wall median %5.2f ms; %d ops traced, %d bakes\n",
			             c.what, size.name, gpu[ kTimed / 2 ], gpu.back(), 100.0 * gpu[ kTimed / 2 ] / ( 1000.0 / 60.0 ), wall[ kTimed / 2 ],
			             rig.plugin.LastFrame().opCount, rig.plugin.LastFrame().bakesTotal );
		}
	glDeleteQueries( 1, &query );
	return 0;
}
