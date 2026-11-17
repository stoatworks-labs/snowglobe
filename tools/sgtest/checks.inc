//===========================================================================
// The checks. Each sets every control it depends on (honeydew's lesson),
// and every picture check runs at each raster in kRasters.
//===========================================================================
#include "checks_physics.inc"
#include "checks_optics.inc"
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
		{ "fall", runFall, false },         { "settle", runSettle, false },     { "swirl", runSwirl, false },   { "still", runStill, false },
		{ "grid", runGrid, false },         { "magnify", runMagnify, false },   { "flakes", runFlakes, false }, { "rate", runRate, false },
		{ "clock", runClock, false },       { "resize", runResize, false },     { "alpha", runAlpha, false },   { "triggers", runTriggers, false },
		{ "coreaudio", runCoreaudio, false }, { "glstate", runGlstate, false }, { "robust", runRobust, false }, { "model", runModel, true },
		{ "names", runNames, true },        { "cues", runCues, true },
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
	auto add = [ & ]( const char* name, const char* what, int hooks, int cpuHooks, int physics, int model ) {
		Perturb p;
		p.hooks    = hooks;
		p.cpuHooks = cpuHooks;
		p.physics  = physics;
		p.model    = model;
		cases.push_back( { name, p, what } );
	};
	using namespace shaders;
	add( "fall", "discs fall at their equal-volume sphere's Stokes speed", 0, 0, kPhysSphereDiscs, 0 );
	add( "fall", "settling speeds from water's viscosity, whatever the fill", 0, 0, kPhysWaterSettling, 0 );
	add( "settle", "settling speeds from water's viscosity, whatever the fill", 0, 0, kPhysWaterSettling, 0 );
	add( "swirl", "the swirl decays with k = pi / R, not x1 / R", 0, 0, kPhysWrongMode, 0 );
	add( "still", "resting flakes slide down the glass", 0, 0, kPhysSliding, 0 );
	add( "still", "a kick from any band: the hats shake it", 0, kCpuAnyBand, 0, 0 );
	add( "grid", "the scene not refracted at the glass", kHookNoRefract, 0, 0, 0 );
	add( "grid", "the scene refracted with water's n, whatever the fill", 0, kCpuWaterIndex, 0, 0 );
	add( "magnify", "the scene refracted with water's n, whatever the fill", 0, kCpuWaterIndex, 0, 0 );
	add( "flakes", "flakes projected without the glass's refraction", 0, kCpuFlakesStraight, 0, 0 );
	add( "flakes", "flakes drawn with hard edges, no coverage", kHookHardEdge, 0, 0, 0 );
	add( "rate", "beats and placed shakes at the frame, not inside it", 0, kCpuEventsAtFrame, 0, 0 );
	add( "clock", "the host's clock read through a float", 0, kCpuFloatClock, 0, 0 );
	add( "resize", "a resize starts the globe again", 0, kCpuResizeRestarts, 0, 0 );
	add( "alpha", "the clip sampled straight, not premultiplied", kHookNoPremultiply, 0, 0, 0 );
	add( "triggers", "beats and kicks placed at the frame, not inside it", 0, kCpuEventsAtFrame, 0, 0 );
	add( "triggers", "a kick from any band, not the lowest", 0, kCpuAnyBand, 0, 0 );
	add( "coreaudio", "the device menu listed and Audio Input applied whatever Trigger On says", 0, kCpuIgnoreGate, 0, 0 );
	add( "glstate", "the composite leaves its program and textures bound", 0, kCpuLeakState, 0, 0 );
	add( "robust", "presses between two frames collapse to one", 0, kCpuDropPresses, 0, 0 );
	add( "model", "the fill's viscosity at 25 C, not 20 C", 0, 0, 0, 1 );
	add( "model", "water's index, whatever the fill", 0, 0, 0, 4 );
	add( "model", "the swirl mode held to k = pi / R", 0, 0, 0, 5 );
	add( "names", "a seventeen-character name added", 0, 0, 0, 2 );
	add( "cues", "every control ramped between keys", 0, 0, 0, 3 );

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
// --bench: each frame timed alone. glFinish before it, so nothing queued
// from the last frame is counted; GL_TIME_ELAPSED around it for the GPU;
// the CPU's own time in ProcessOpenGL (physics, projection, upload) from
// the thread's clock; wall time to a glFinish after it. Every timed frame is
// checked to have rendered: its clock advanced and its flakes were drawn.
//===========================================================================
int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
	};
	const Size sizes[] = { { 1280, 720, "720p" }, { 1920, 1080, "1080p" } };
	struct Case
	{
		const char* what;
		std::function< void( Rig& ) > set;
		int pressEvery;///< frames between full shakes, 0 for none
	};
	const Case cases[] = {
		{ "defaults (600 flakes), kicks from the music",
		  []( Rig& r ) {
			  for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
				  if( InfoOf( id ).kind == Kind::Standard || InfoOf( id ).kind == Kind::Option )
					  r.Set( id, InfoOf( id ).defaultValue );
			  r.audio = musicAt;
		  },
		  0 },
		{ "3000 flakes (the cap), 1.25 mm, Globe 1.0, a full shake each second",
		  []( Rig& r ) {
			  for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
				  if( InfoOf( id ).kind == Kind::Standard || InfoOf( id ).kind == Kind::Option )
					  r.Set( id, InfoOf( id ).defaultValue );
			  r.Set( PT_TRIGGER_ON, static_cast< float >( TriggerOn::Button ) );
			  r.Set( PT_COUNT_FLAKES, 1.0f );
			  r.Set( PT_SIZE, 1.0f );
			  r.Set( PT_RANGE, 1.0f );
			  r.Set( PT_GLOBE, 1.0f );
		  },
		  60 },
	};
	std::printf( "\n=== bench: each frame alone (glFinish before and after), after 240 frames of warm-up: GPU time (GL_TIME_ELAPSED), the CPU's "
	             "time in ProcessOpenGL (thread clock) and wall time, median and worst of 120 (the machine is shared, so these wander)\n" );
	GLuint query = 0;
	glGenQueries( 1, &query );
	int unrendered = 0;
	for( const Case& c : cases )
		for( const Size& size : sizes )
		{
			Rig rig;
			if( !rig.Init( size.w, size.h ) )
				return 1;
			c.set( rig );
			auto frameOf = [ & ]( int f ) {
				if( c.pressEvery > 0 )
					rig.Set( PT_SHAKE_NOW, f % c.pressEvery == 1 ? 1.0f : 0.0f );
			};
			for( int f = 0; f < 240; ++f )
			{
				frameOf( f );
				if( !rig.Render( 1 ) )
					return 1;
			}
			const int kTimed = 120;
			std::vector< double > gpu, cpu, wall;
			int drawnMin = 1 << 30;
			for( int f = 240; f < 240 + kTimed; ++f )
			{
				frameOf( f );
				glFinish();
				const int64_t before = rig.plugin.LastFrame().clockUs;
				timespec c0 {}, c1 {};
				clock_gettime( CLOCK_THREAD_CPUTIME_ID, &c0 );
				const auto start = std::chrono::steady_clock::now();
				glBeginQuery( GL_TIME_ELAPSED, query );
				if( !rig.Render( 1 ) )
					return 1;
				glEndQuery( GL_TIME_ELAPSED );
				clock_gettime( CLOCK_THREAD_CPUTIME_ID, &c1 );
				glFinish();
				wall.push_back( std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count() );
				GLuint64 ns = 0;
				glGetQueryObjectui64v( query, GL_QUERY_RESULT, &ns );
				gpu.push_back( static_cast< double >( ns ) * 1e-6 );
				cpu.push_back( ( c1.tv_sec - c0.tv_sec ) * 1e3 + ( c1.tv_nsec - c0.tv_nsec ) * 1e-6 );
				const FrameData& fd = rig.plugin.LastFrame();
				unrendered += !( fd.clockUs > before ) || fd.drawn == 0 || ns == 0;
				drawnMin = std::min( drawnMin, fd.drawn );
			}
			std::sort( gpu.begin(), gpu.end() );
			std::sort( cpu.begin(), cpu.end() );
			std::sort( wall.begin(), wall.end() );
			std::printf( "  %-66s %-6s GPU median %6.3f ms, worst %6.3f; CPU median %6.3f ms, worst %6.3f; wall median %6.3f ms, worst %6.3f; "
			             "at least %d flakes drawn\n",
			             c.what, size.name, gpu[ kTimed / 2 ], gpu.back(), cpu[ kTimed / 2 ], cpu.back(), wall[ kTimed / 2 ], wall.back(), drawnMin );
		}
	glDeleteQueries( 1, &query );
	std::printf( "  timed frames that did not render (clock still, nothing drawn or no GPU time): %d\n", unrendered );
	return unrendered == 0 ? 0 : 1;
}
