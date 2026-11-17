//===========================================================================
// The host and the plugin's own bookkeeping: --coreaudio, --glstate,
// --robust; and the offline ones, --model, --names, --cues.
//===========================================================================

std::string selfPath()
{
	char buffer[ PATH_MAX ];
	uint32_t size = sizeof( buffer );
	if( _NSGetExecutablePath( buffer, &size ) != 0 )
		return g_argv0;
	char resolved[ PATH_MAX ];
	return realpath( buffer, resolved ) ? resolved : buffer;
}

struct SpyCounts
{
	int dlopens = -1, dlsyms = -1, calls = -1;
	bool absent = true;
	int total() const
	{
		return dlopens + dlsyms + calls;
	}
};

/// A child sgtest under the interposer, the gate on or off, one mode.
SpyCounts spawnSpied( const std::string& mode, bool gate, int cpuHooks )
{
	SpyCounts counts;
	const std::string self  = selfPath();
	const std::string dir   = self.substr( 0, self.rfind( '/' ) );
	const std::string spy   = dir + "/libaudiospy.dylib";
	std::vector< std::string > env;
	for( char** e = *_NSGetEnviron(); *e != nullptr; ++e )
	{
		const std::string entry = *e;
		if( entry.rfind( "SNOWGLOBE_NO_DEVICES=", 0 ) == 0 || entry.rfind( "DYLD_INSERT_LIBRARIES=", 0 ) == 0 || entry.rfind( "AUDIOSPY_", 0 ) == 0 )
			continue;
		env.push_back( entry );
	}
	env.push_back( "DYLD_INSERT_LIBRARIES=" + spy );
	//Always refused, so even a failed gate opens nothing: the count reports it.
	env.push_back( "AUDIOSPY_BLOCK=1" );
	if( gate )
		env.push_back( "SNOWGLOBE_NO_DEVICES=1" );
	std::vector< char* > envp;
	for( std::string& s : env )
		envp.push_back( s.data() );
	envp.push_back( nullptr );
	const std::string arg = mode + ":" + std::to_string( cpuHooks );
	std::vector< char* > argv = { const_cast< char* >( self.c_str() ), const_cast< char* >( "--coreaudio-child" ), const_cast< char* >( arg.c_str() ), nullptr };

	int pipeFds[ 2 ];
	if( pipe( pipeFds ) != 0 )
		return counts;
	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init( &actions );
	posix_spawn_file_actions_adddup2( &actions, pipeFds[ 1 ], STDOUT_FILENO );
	posix_spawn_file_actions_addclose( &actions, pipeFds[ 0 ] );
	pid_t pid = 0;
	const int spawned = posix_spawn( &pid, self.c_str(), &actions, nullptr, argv.data(), envp.data() );
	posix_spawn_file_actions_destroy( &actions );
	close( pipeFds[ 1 ] );
	std::string out;
	char buffer[ 512 ];
	ssize_t got = 0;
	while( spawned == 0 && ( got = read( pipeFds[ 0 ], buffer, sizeof( buffer ) ) ) > 0 )
		out.append( buffer, static_cast< size_t >( got ) );
	close( pipeFds[ 0 ] );
	int status = 0;
	if( spawned == 0 )
		waitpid( pid, &status, 0 );
	const size_t at = out.find( "audiospy dlopens=" );
	if( at != std::string::npos
	    && std::sscanf( out.c_str() + at, "audiospy dlopens=%d dlsyms=%d calls=%d", &counts.dlopens, &counts.dlsyms, &counts.calls ) == 3 )
		counts.absent = false;
	return counts;
}

/// The child: the real plugin for 40 frames in one mode, then the counts.
int runCoreaudioChild( const std::string& arg )
{
	const size_t colon   = arg.find( ':' );
	const std::string m  = arg.substr( 0, colon );
	const int cpuHooks   = colon == std::string::npos ? 0 : std::atoi( arg.c_str() + colon + 1 );
	Rig rig;
	Perturb p;
	p.cpuHooks = cpuHooks;
	if( !startRig( rig, 320, 180, p ) )
		return 1;
	const TriggerOn on = m == "kick" ? TriggerOn::Kick : m == "beat" ? TriggerOn::Beat : TriggerOn::Button;
	rig.Set( PT_TRIGGER_ON, static_cast< float >( on ) );
	//A composition saved with a device chosen: the System default.
	rig.Set( PT_INPUT, elementValue( rig.plugin, static_cast< unsigned int >( rig.plugin.HostIndexOf( PT_INPUT ) ), 1 ) );
	rig.beatBpm     = kOffGridBpm;
	rig.beforeFrame = [ &rig ]( int f ) { rig.Set( PT_SHAKE_NOW, f % 10 == 5 ? 1.0f : 0.0f ); };
	if( !rig.Render( 40 ) )
		return 1;
	using CountsFn = void ( * )( int*, int*, int* );
	const auto fn  = reinterpret_cast< CountsFn >( dlsym( RTLD_DEFAULT, "audiospy_counts" ) );
	if( fn == nullptr )
	{
		std::printf( "audiospy absent\n" );
		return 3;
	}
	int a = 0, b = 0, c = 0;
	fn( &a, &b, &c );
	std::printf( "audiospy dlopens=%d dlsyms=%d calls=%d\n", a, b, c );
	return 0;
}

//===========================================================================
// --coreaudio: with the gate on, or audio triggering off, nothing calls
// into CoreAudio or loads it by name -- counted by an interposer.
//===========================================================================
int runCoreaudio( const Perturb& perturb )
{
	std::printf( "\n=== coreaudio: a child sgtest under an interposer (tools/sgtest/audiospy.c) counting audio-framework dlopens, 'Audio*' dlsyms "
	             "and CoreAudio/AudioToolbox calls, Audio Input = System default, 40 frames\n" );
	struct Case
	{
		const char* what;
		const char* mode;
		bool gate;
		bool expectZero;
	};
	const Case cases[] = {
		{ "SNOWGLOBE_NO_DEVICES=1, Trigger On = Kick", "kick", true, true },
		{ "no gate, Trigger On = Beat", "beat", false, true },
		{ "no gate, Trigger On = Button", "button", false, true },
		{ "the instrument's own control: no gate, Kick (refused, so nothing opens)", "kick", false, false },
	};
	for( const Case& c : cases )
	{
		const SpyCounts n = spawnSpied( c.mode, c.gate, perturb.cpuHooks );
		const bool ok     = !n.absent && ( c.expectZero ? n.total() == 0 : n.total() > 0 );
		Check( ok, fmt( "%s: %s (expected %s)", c.what,
		                n.absent ? "the interposer did not report: absent" : fmt( "%d dlopens, %d dlsyms, %d calls", n.dlopens, n.dlsyms, n.calls ).c_str(),
		                c.expectZero ? "none" : "some" ) );
	}
	return Verdict();
}

//===========================================================================
// --glstate: the GL state the host hands over is the state it gets back.
//===========================================================================
int runGlstate( const Perturb& perturb )
{
	std::printf( "\n=== glstate: the GL state the host hands over is the state it gets back, through shakes, Mix 0 and 1, the beads and a resize\n" );
	Rig rig;
	if( !startRig( rig, 320, 180, perturb ) )
		return 1;
	GLuint hostArray = 0, hostBuffer = 0;
	glGenVertexArrays( 1, &hostArray );
	glGenBuffers( 1, &hostBuffer );
	int problems = 0, drawnFrames = 0;
	std::string what;
	for( int frame = 0; frame < 80; ++frame )
	{
		if( frame == 60 && !rig.Resize( 240, 160 ) )
			return 1;
		rig.plugin.SetTime( frame / 60.0 );
		rig.Set( PT_SHAKE_NOW, frame % 20 == 1 ? 1.0f : 0.0f );
		rig.Set( PT_SNOW, static_cast< float >( frame >= 40 ? Shape::Beads : Shape::Flakes ) );
		rig.Set( PT_MIX, frame % 7 == 3 ? 0.0f : 1.0f );
		const int w = rig.width, h = rig.height;
		glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
		glViewport( 0, 0, w, h );
		glBindVertexArray( hostArray );
		glBindBuffer( GL_ARRAY_BUFFER, hostBuffer );
		glEnable( GL_BLEND );
		glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO );
		glClearColor( 0.2f, 0.3f, 0.4f, 0.5f );
		glEnable( GL_SCISSOR_TEST );
		glScissor( 0, 0, w, h );
		glActiveTexture( GL_TEXTURE0 );
		glUseProgram( 0 );
		if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
			return 1;
		drawnFrames += rig.plugin.LastFrame().drawn > 0;
		GLint viewport[ 4 ] = {}, array = 0, buffer = 0, program = 0, unit = 0, fbo = 0, src = 0, dst = 0, unpack = 0;
		GLfloat clear[ 4 ]  = {};
		GLboolean mask[ 4 ] = {};
		glGetIntegerv( GL_VIEWPORT, viewport );
		glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &array );
		glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &buffer );
		glGetIntegerv( GL_CURRENT_PROGRAM, &program );
		glGetIntegerv( GL_ACTIVE_TEXTURE, &unit );
		glGetIntegerv( GL_FRAMEBUFFER_BINDING, &fbo );
		glGetIntegerv( GL_BLEND_SRC_RGB, &src );
		glGetIntegerv( GL_BLEND_DST_RGB, &dst );
		glGetIntegerv( GL_UNPACK_ALIGNMENT, &unpack );
		glGetFloatv( GL_COLOR_CLEAR_VALUE, clear );
		glGetBooleanv( GL_COLOR_WRITEMASK, mask );
		auto expect = [ & ]( bool ok, const char* name ) {
			if( !ok )
			{
				++problems;
				if( what.find( name ) == std::string::npos )
					what += std::string( " " ) + name;
			}
		};
		expect( viewport[ 0 ] == 0 && viewport[ 1 ] == 0 && viewport[ 2 ] == w && viewport[ 3 ] == h, "viewport" );
		expect( array == static_cast< GLint >( hostArray ), "vertex-array" );
		expect( buffer == static_cast< GLint >( hostBuffer ), "array-buffer" );
		expect( program == 0, "program" );
		expect( unit == GL_TEXTURE0, "active-unit" );
		expect( fbo == static_cast< GLint >( rig.outputFBO ), "framebuffer" );
		expect( glIsEnabled( GL_BLEND ) && src == GL_SRC_ALPHA && dst == GL_ONE_MINUS_SRC_ALPHA, "blend" );
		expect( glIsEnabled( GL_SCISSOR_TEST ), "scissor" );
		expect( clear[ 0 ] == 0.2f && clear[ 1 ] == 0.3f && clear[ 2 ] == 0.4f && clear[ 3 ] == 0.5f, "clear-colour" );
		expect( mask[ 0 ] && mask[ 1 ] && mask[ 2 ] && mask[ 3 ], "colour-mask" );
		expect( unpack == 4, "unpack-alignment" );
		for( int u = 0; u < 8; ++u )
		{
			GLint bound = 0;
			glActiveTexture( static_cast< GLenum >( GL_TEXTURE0 + u ) );
			glGetIntegerv( GL_TEXTURE_BINDING_2D, &bound );
			expect( bound == 0, "texture-unit" );
		}
		glActiveTexture( GL_TEXTURE0 );
		glUseProgram( 0 );
		glDisable( GL_SCISSOR_TEST );
		glDisable( GL_BLEND );
	}
	glBindVertexArray( 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glDeleteVertexArrays( 1, &hostArray );
	glDeleteBuffers( 1, &hostBuffer );
	Check( problems == 0 && drawnFrames > 0,
	       fmt( "80 frames, %d with flakes drawn: viewport, vertex array, array buffer, program, active unit, framebuffer, blend, scissor, clear "
	            "colour, colour mask, unpack alignment, eight texture units (%d wrong:%s)",
	            drawnFrames, problems, what.empty() ? " none" : what.c_str() ) );
	return Verdict();
}

//===========================================================================
// --robust: the extremes. Finite output in 0..1; every flake inside the
// glass; every press a shake.
//===========================================================================
int runRobust( const Perturb& perturb )
{
	std::printf( "\n=== robust: both shapes, every ranged control at both ends, a press every other frame and two between some frames: output finite "
	             "and in 0..1, every flake inside the glass, every press a shake\n" );
	const Raster sizes[] = { { 320, 180 }, { 97, 61 } };
	for( const Raster& size : sizes )
		for( int shape = 0; shape < 2; ++shape )
			for( float end : { 0.0f, 1.0f } )
			{
				Rig rig;
				if( !startRig( rig, size.w, size.h, perturb ) )
					return 1;
				rig.Set( PT_SNOW, static_cast< float >( shape ) );
				for( unsigned int id : { PT_SHAKE, PT_FILL, PT_SIZE, PT_RANGE, PT_COUNT_FLAKES, PT_GLOBE, PT_MIX } )
					rig.Set( id, end );
				int presses = 0, nonFinite = 0, outOfRange = 0;
				double outside = 0.0;
				for( int f = 0; f < 240; ++f )
				{
					const bool down = f % 2 == 0;
					if( down && f % 6 == 0 )
					{
						//Two presses between this frame and the last.
						rig.Set( PT_SHAKE_NOW, 1.0f );
						rig.Set( PT_SHAKE_NOW, 0.0f );
						++presses;
					}
					presses += down;
					rig.Set( PT_SHAKE_NOW, down ? 1.0f : 0.0f );
					if( !rig.Render( 1 ) )
						return 1;
					if( f % 16 == 0 || f == 239 )
					{
						const Physics ph = rig.plugin.CurrentPhysics();
						std::vector< V3 > p;
						rig.plugin.GlobeForTest().Positions( rig.plugin.LastFrame().clockUs, ph, p );
						for( int i = 0; i < ph.count; ++i )
							outside = worse( outside, std::sqrt( dot( p[ static_cast< size_t >( i ) ], p[ static_cast< size_t >( i ) ] ) ) - kRadius );
					}
				}
				const Floats o = rig.Output();
				for( float v : o )
				{
					nonFinite += !std::isfinite( v );
					outOfRange += v < 0.0f || v > 1.0f;
				}
				const int shakes = static_cast< int >( rig.plugin.ShakeLog().size() );
				Check( nonFinite == 0 && outOfRange == 0 && !( outside > 0.0 ) && shakes == presses,
				       fmt( "%dx%d, %s, every control at %g: %d non-finite and %d out-of-range floats; furthest centre %.3g mm %s the glass; %d "
				            "presses, %d shakes",
				            size.w, size.h, shape ? "beads" : "flakes", end, nonFinite, outOfRange, 1e3 * std::fabs( outside ),
				            outside > 0.0 ? "OUTSIDE" : "inside", presses, shakes ) );
			}
	return Verdict();
}

//===========================================================================
// --model (no GL): the CPU half against the printed tables and the
// mode's own equations.
//===========================================================================
int runModel( const Perturb& perturb )
{
	std::printf( "\n=== model: the fill against the GPA's printed tables; x1 and the swirl mode against their equations; the harness's own trace "
	             "against Hecht's paraxial n; the controls; a run replays exactly\n" );
	auto fillAt = [ & ]( double w ) {
		FillProps f = FillAt( w );
		if( perturb.model == 1 )
			f.mu = ChengViscosity( w, 25.0 );//the wrong temperature
		if( perturb.model == 4 )
			f.n = 1.33303;//water's index, whatever the fill
		return f;
	};
	//(a) Viscosity: Cheng (2008) against Table 17 at 20 C, every printed row
	//60-80 %. Cheng's own count against these data: 3.5 % at most.
	{
		double worst = 0.0;
		std::string rows;
		for( const auto& row : kGpaViscosity )
		{
			const double rel = fillAt( row.first ).mu / row.second - 1.0;
			worst            = worse( worst, std::fabs( rel ) );
			rows += fmt( " %.0f%% %+.1f%%", 100.0 * row.first, 100.0 * rel );
		}
		Check( worst <= kChengError, fmt( "viscosity, Cheng against GPA Table 17 at 20 C:%s; worst %.2f%% (tolerance 3.5%%, Cheng's own)", rows.c_str(),
		                                  100.0 * worst ) );
	}
	//(b) Density: Volk and Kaehler (2018) against Table 4 at 20 C (Bosart and
	//Snoddy), its rows 60, 70, 80 %. Their stated accuracy: 0.07 %.
	{
		double worst = 0.0;
		std::string rows;
		for( const TableRow& row : kGpaRows )
		{
			const double rel = fillAt( row.w ).rho / row.rho - 1.0;
			worst            = worse( worst, std::fabs( rel ) );
			rows += fmt( " %.0f%% %+.3f%%", 100.0 * row.w, 100.0 * rel );
		}
		Check( worst <= kVolkError, fmt( "density, Volk and Kaehler against GPA Table 4 at 20 C:%s; worst %.3f%% (tolerance 0.07%%, theirs)", rows.c_str(),
		                                 100.0 * worst ) );
	}
	//(c) Index: Hoyt's fits against Table 23 at 20.0 C, every 1 %, 60-80 %.
	//The GPA: the fits deviate from Hoyt's observations by 0.00011 at most.
	{
		double worst = 0.0;
		int at       = 0;
		for( int k = 0; k <= 20; ++k )
		{
			const double d = std::fabs( fillAt( 0.60 + 0.01 * k ).n - kGpaIndex[ k ] );
			if( !( d <= worst ) )
				at = 60 + k;
			worst = worse( worst, d );
		}
		Check( worst <= 0.00011, fmt( "index, Hoyt's fits against GPA Table 23 at 20.0 C, 60-80 %% by 1 %%: worst %.5f at %d %% (tolerance 0.00011, the "
		                              "GPA's)",
		                              worst, at ) );
	}
	//(d) x1, the first positive root of tan x = x, by bisection in long double.
	{
		const double x1 = rootTanX();
		Check( std::fabs( x1 - kX1 ) <= 1e-15 * kX1 * 4.0,
		       fmt( "x1 by bisection %.17g against the plugin's %.17g (tolerance 4 ulp)", x1, kX1 ) );
	}
	//(e) The swirl mode: u = f(r) (W x x) satisfies the unsteady Stokes
	//equations' mode equation lap u = -k^2 u with k = x1 / R, is divergence
	//free and vanishes on the glass. Central differences of the plugin's own
	//velocity field at 2,000 seeded points; h = 1e-4 R, so the truncation is
	//(k h)^2 / 12 ~ 2e-9 relative and the rounding eps / h^2 ~ 2e-8 of |u|
	//over R^2: tolerance 1e-5 relative.
	{
		Globe globe;
		Physics ph;
		ph.fill = fillAt( 0.70 );
		const V3 W     = { 0.3, 1.1, -0.7 };
		const double k = ( perturb.model == 5 ? kPi : kX1 ) / kRadius;
		const double h = 1e-4 * kRadius;
		double worstLap = 0.0, worstDiv = 0.0, worstWall = 0.0;
		for( uint32_t n = 0; n < 2000; ++n )
		{
			const V3 x = { ( 2.0 * Hash01( n, 11, 0 ) - 1.0 ) * 0.9 * kRadius, ( 2.0 * Hash01( n, 11, 1 ) - 1.0 ) * 0.9 * kRadius,
				           ( 2.0 * Hash01( n, 11, 2 ) - 1.0 ) * 0.9 * kRadius };
			if( dot( x, x ) > 0.81 * kRadius * kRadius || dot( x, x ) < 1e-4 * kRadius * kRadius )
				continue;
			const V3 u = globe.FluidVelocity( x, W, ph );
			V3 lap;
			double div = 0.0;
			const V3 e[ 3 ] = { { h, 0, 0 }, { 0, h, 0 }, { 0, 0, h } };
			for( int a = 0; a < 3; ++a )
			{
				const V3 up = globe.FluidVelocity( x + e[ a ], W, ph ), dn = globe.FluidVelocity( x - e[ a ], W, ph );
				lap         = lap + ( 1.0 / ( h * h ) ) * ( up + dn - 2.0 * u );
				div += ( a == 0 ? up.x - dn.x : a == 1 ? up.y - dn.y : up.z - dn.z ) / ( 2.0 * h );
			}
			const double scale = std::sqrt( dot( W, W ) ) * kRadius * k * k;
			const V3 r         = lap + ( k * k ) * u;
			worstLap           = worse( worstLap, std::sqrt( dot( r, r ) ) / scale );
			worstDiv           = worse( worstDiv, std::fabs( div ) / std::sqrt( dot( W, W ) ) );
			//On the glass: the same direction, at R.
			const V3 wall = ( kRadius / std::sqrt( dot( x, x ) ) ) * x;
			const V3 uw   = globe.FluidVelocity( wall, W, ph );
			worstWall     = worse( worstWall, std::sqrt( dot( uw, uw ) ) / ( std::sqrt( dot( W, W ) ) * kRadius ) );
		}
		Check( worstLap <= 1e-5 && worstDiv <= 1e-5 && worstWall <= 1e-12,
		       fmt( "the swirl mode at 2,000 points: |lap u + k^2 u| / (|W| R k^2) worst %.1e, |div u| / |W| worst %.1e (tolerances 1e-5: "
		            "truncation and rounding of the differences); |u| on the glass / (|W| R) worst %.1e (tolerance 1e-12)",
		            worstLap, worstDiv, worstWall ) );
	}
	//(f) The harness's trace: paraxially, an object on the card at the
	//centre appears magnified by n (Hecht; SPEC "The optics"). The slope of
	//xi(rho) at rho = 1e-6 R against n, for the fill's n at 60 and 80 %.
	{
		double worst = 0.0;
		for( double w : { 0.60, 0.80 } )
		{
			const LD n = static_cast< LD >( fillAt( w ).n );
			LD xi      = 0.0L;
			if( !harnessXi( 1e-6L, 0.0L, n, xi ) )
				worst = worse( worst, 1.0 );
			else
			{
				//The angle at the eye for the same rho without the water.
				const LD bare = 1e-6L / ( static_cast< LD >( kViewDistance ) );
				worst         = worse( worst, static_cast< double >( std::fabs( xi / bare / static_cast< LD >( FillAt( w ).n ) - 1.0L ) ) );
			}
		}
		Check( worst <= 1e-9,
		       fmt( "the harness's trace, an object 1e-6 R from the centre on the card: magnified by n within %.1e (tolerance 1e-9: the next term "
		            "is of order (rho/R)^2 = 1e-12, the rounding a few ulp after the trace's one-digit cancellation)",
		            worst ) );
	}
	//(g) The controls: each default lands on its stated value; each inverse
	//inverts.
	{
		double worst = 0.0;
		worst        = worse( worst, std::fabs( SizeFromParam( InfoOf( PT_SIZE ).defaultValue, Shape::Flakes ) / 1.0e-3 - 1.0 ) );
		worst        = worse( worst, std::fabs( RangeFromParam( InfoOf( PT_RANGE ).defaultValue ) - 0.5 ) );
		worst        = worse( worst, std::fabs( GlobeFromParam( InfoOf( PT_GLOBE ).defaultValue ) - 0.9 ) );
		worst        = worse( worst, std::fabs( FillFromParam( InfoOf( PT_FILL ).defaultValue ) - 0.60 ) );
		bool counts  = CountFromParam( InfoOf( PT_COUNT_FLAKES ).defaultValue ) == 600 && CountFromParam( 0.0f ) == kCountLow
		              && CountFromParam( 1.0f ) == kCountHigh;
		for( int c : { 50, 51, 99, 600, 1234, 2999, 3000 } )
			counts = counts && CountFromParam( ParamFromCount( c ) ) == c;
		for( float v = 0.0f; v <= 1.0f; v += 0.125f )
		{
			worst = worse( worst, std::fabs( static_cast< double >( ParamFromFill( FillFromParam( v ) ) ) - v ) );
			worst = worse( worst, std::fabs( static_cast< double >( ParamFromSize( SizeFromParam( v, Shape::Beads ), Shape::Beads ) ) - v ) );
			worst = worse( worst, std::fabs( static_cast< double >( ParamFromRange( RangeFromParam( v ) ) ) - v ) );
			worst = worse( worst, std::fabs( static_cast< double >( ParamFromGlobe( GlobeFromParam( v ) ) ) - v ) );
		}
		Check( worst < 1e-6 && counts, fmt( "defaults 1.0 mm, 0.5, 0.9, 60 %% and the inverses: worst %.1e; Count 600 by default, 50..3000, round-trips: "
		                                    "%s",
		                                    worst, counts ? "yes" : "NO" ) );
	}
	//(h) A run replays exactly: two globes, the same physics, the same five
	//shakes, 20 s: the same state, every bit.
	{
		Physics ph;
		ph.fill = fillAt( 0.65 );
		Globe a, b;
		a.Start( ph );
		b.Start( ph );
		const std::vector< ShakeEvent > shakes = { { 100'003, 1.0 }, { 2'500'017, 0.4 }, { 2'500'018, 0.7 }, { 7'777'777, 0.9 }, { 12'000'001, 0.2 } };
		for( int64_t t = 0; t <= 20'000'000; t += 16'667 )
		{
			std::vector< ShakeEvent > due;
			for( const ShakeEvent& s : shakes )
				if( s.us > a.StateUs() && s.us <= t )
					due.push_back( s );
			a.Advance( t, due, ph );
			b.Advance( t, due, ph );
		}
		Check( a.Hash( ph.count ) == b.Hash( ph.count ) && a.ShakeCount() == 5,
		       fmt( "two runs, five shakes, 20 s: %llu shakes, state %s", static_cast< unsigned long long >( a.ShakeCount() ),
		            a.Hash( ph.count ) == b.Hash( ph.count ) ? "identical" : "DIFFERS" ) );
	}
	return Verdict();
}

int runNames( const Perturb& perturb )
{
	std::printf( "\n=== names: every parameter unique (as Arena addresses them, too), within FFGL's 16 characters, the About block last\n" );
	SnowglobePlugin plugin;
	std::vector< std::string > names;
	for( unsigned int i = 0; i < plugin.GetNumParams(); ++i )
		names.push_back( plugin.GetParamName( i ) ? plugin.GetParamName( i ) : "" );
	if( perturb.model == 2 )
		names.push_back( "Seventeen Chars!!" );
	std::map< std::string, int > seen, address;
	int longNames = 0, dupes = 0, clashes = 0, slashes = 0;
	for( const std::string& name : names )
	{
		longNames += name.size() > 16;
		dupes += seen[ name ]++ > 0;
		slashes += name.find( '/' ) != std::string::npos;
		std::string key;
		for( char c : name )
			if( c != ' ' )
				key += static_cast< char >( std::tolower( static_cast< unsigned char >( c ) ) );
		clashes += address[ key ]++ > 0;
	}
	const unsigned int aboutFirst = plugin.GetNumParams() - stoatworks::about::kParamCount;
	const bool aboutLast          = std::string( plugin.GetParamName( aboutFirst ) ) == "About";
	Check( longNames == 0 && dupes == 0 && clashes == 0 && slashes == 0 && aboutLast,
	       fmt( "%zu parameters: %d too long, %d duplicated, %d clashing addresses, %d with a '/'; the About block last: %s", names.size(), longNames,
	            dupes, clashes, slashes, aboutLast ? "yes" : "NO" ) );
	return Verdict();
}

int runCues( const Perturb& perturb )
{
	std::printf( "\n=== cues: a cue sheet steps options and events, ramps standard controls, and names an option by its name\n" );
	SnowglobePlugin plugin;
	std::istringstream sheet( "0 Snow Flakes\n60 Snow Beads\n0 Shake Now 0\n60 Shake Now 1\n0 Fill 0\n60 Fill 1\n0 Trigger On Kick\n"
	                          "60 Trigger On Button\n" );
	std::map< unsigned int, Cue > cues;
	std::string error;
	bindScript( plugin, sheet, "cues", cues, error );
	int bad = 0;
	std::string what;
	for( const auto& entry : cues )
	{
		const unsigned int type = plugin.GetParamType( entry.first );
		const bool ramp         = perturb.model == 3 ? true : entry.second.ramp;
		const float mid         = valueAt( entry.second.track, 29, ramp );
		const float a = entry.second.track.front().second, b = entry.second.track.back().second;
		const bool expectRamp = type == FF_TYPE_STANDARD;
		const bool ok         = expectRamp ? ( mid > a && mid < b ) : ( mid == a && valueAt( entry.second.track, 60, ramp ) == b );
		bad += !ok;
		what += fmt( " %s %s(%g at frame 29)", plugin.GetParamName( entry.first ), expectRamp ? "ramps " : "steps ", mid );
	}
	const auto snow  = cues.find( PT_SNOW );
	const auto on    = cues.find( PT_TRIGGER_ON );
	const bool named = snow != cues.end() && snow->second.track.back().second == static_cast< float >( Shape::Beads ) && on != cues.end()
	                   && on->second.track.back().second == static_cast< float >( TriggerOn::Button );
	Check( error.empty() && bad == 0 && cues.size() == 4 && named,
	       fmt( "%d wrong:%s; 'Beads' and 'Button' read as their options: %s%s", bad, what.c_str(), named ? "yes" : "NO",
	            error.empty() ? "" : ( "; " + error ).c_str() ) );
	return Verdict();
}
