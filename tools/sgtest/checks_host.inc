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
	const TriggerOn on = m == "onsets" ? TriggerOn::Onsets : m == "beat" ? TriggerOn::Beat : TriggerOn::Manual;
	rig.Set( PT_TRIGGER_ON, static_cast< float >( on ) );
	//A composition saved with a device chosen: the System default.
	rig.Set( PT_INPUT, elementValue( rig.plugin, static_cast< unsigned int >( rig.plugin.HostIndexOf( PT_INPUT ) ), 1 ) );
	rig.beatBpm     = 120.0;
	rig.beforeFrame = [ &rig ]( int f ) { rig.Set( PT_TRIGGER, f % 10 == 5 ? 1.0f : 0.0f ); };
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
		{ "SNOWGLOBE_NO_DEVICES=1, Trigger On = Onsets", "onsets", true, true },
		{ "no gate, Trigger On = Beat", "beat", false, true },
		{ "no gate, Trigger On = Manual", "manual", false, true },
		{ "the instrument's own control: no gate, Onsets (refused, so nothing opens)", "onsets", false, false },
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
	std::printf( "\n=== glstate: the GL state the host hands over is the state it gets back, through drops, strokes, a bake, Paper and a resize\n" );
	Rig rig;
	if( !startRig( rig, 320, 180, perturb ) )
		return 1;
	rig.plugin.SetBakeEveryTriggerForTest( true );
	rig.Set( PT_SPEED, ParamFromStrokeSeconds( 0.1 ) );
	GLuint hostArray = 0, hostBuffer = 0;
	glGenVertexArrays( 1, &hostArray );
	glGenBuffers( 1, &hostBuffer );
	int problems = 0, bakes = 0;
	std::string what;
	for( int frame = 0; frame < 80; ++frame )
	{
		if( frame == 60 && !rig.Resize( 240, 160 ) )
			return 1;
		rig.plugin.SetTime( frame / 60.0 );
		rig.Set( PT_TRIGGER, frame % 4 == 1 ? 1.0f : 0.0f );
		rig.Set( PT_PAPER, frame == 50 ? 1.0f : 0.0f );
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
		bakes = rig.plugin.LastFrame().bakesTotal;
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
	Check( problems == 0 && bakes > 0,
	       fmt( "80 frames, %d bakes: viewport, vertex array, array buffer, program, active unit, framebuffer, blend, scissor, clear colour, colour "
	            "mask, unpack alignment, eight texture units (%d wrong:%s)",
	            bakes, problems, what.empty() ? " none" : what.c_str() ) );
	return Verdict();
}

//===========================================================================
// --robust: the extremes. Finite output; never more ops waiting than the
// shader traces; no trigger lost.
//===========================================================================
int runRobust( const Perturb& perturb )
{
	std::printf( "\n=== robust: every pattern, every control at both ends, a press every other frame: output finite and in range, never more than "
	             "%d ops waiting, every press a trigger\n",
	             kMaxOps );
	struct Size
	{
		int w, h;
	};
	const Size sizes[] = { { 320, 180 }, { 97, 61 } };
	for( const Size& size : sizes )
		for( float end : { 0.0f, 1.0f } )
		{
			Rig rig;
			if( !startRig( rig, size.w, size.h, perturb ) )
				return 1;
			for( unsigned int id : { PT_DROP_SIZE, PT_DROP_RATE, PT_SPACING, PT_DEPTH, PT_SPEED } )
				rig.Set( id, end );
			//The recipe again on top of the last, never a fresh sheet: the
			//ops pile up and the bakes have to keep up.
			rig.Set( PT_AUTO_PAPER, 0.0f );
			int presses = 0, worstOps = 0, nonFinite = 0, outOfRange = 0;
			for( int p = 0; p < static_cast< int >( Pattern::Count ); ++p )
			{
				rig.Set( PT_PATTERN, static_cast< float >( p ) );
				for( int f = 0; f < 160; ++f )
				{
					const bool down = f % 2 == 0;
					presses += down;
					rig.Set( PT_TRIGGER, down ? 1.0f : 0.0f );
					if( !rig.Render( 1 ) )
						return 1;
					worstOps = std::max( worstOps, rig.plugin.LastFrame().opCount );
				}
				const Floats o = rig.Output();
				for( size_t k = 0; k < o.size(); ++k )
				{
					nonFinite += !std::isfinite( o[ k ] );
					outOfRange += o[ k ] < 0.0f || o[ k ] > 1.0f;
				}
			}
			rig.Set( PT_TRIGGER, 0.0f );
			if( !rig.Render( 30 ) )
				return 1;
			const int triggers = static_cast< int >( rig.plugin.TriggerLog().size() );
			Check( nonFinite == 0 && outOfRange == 0 && worstOps <= kMaxOps && triggers == presses && rig.plugin.LastFrame().bakesTotal > 0,
			       fmt( "%dx%d, every control at %g: %d non-finite and %d out-of-range floats; at most %d ops waiting; %d presses, %d triggers; %d "
			            "bakes",
			            size.w, size.h, end, nonFinite, outOfRange, worstOps, presses, triggers, rig.plugin.LastFrame().bakesTotal ) );
		}
	return Verdict();
}

//===========================================================================
// --model (no GL): the CPU half.
//===========================================================================
/// The shader's sine, in float, step for step.
float sinSeriesF( float x )
{
	const float pi = 3.14159265358979f;
	const float k  = std::floor( x * ( 0.5f / pi ) + 0.5f );
	float y        = x - k * ( 2.0f * pi );
	if( y > 0.5f * pi )
		y = pi - y;
	else if( y < -0.5f * pi )
		y = -pi - y;
	const float y2 = y * y;
	return y * ( 1.0f + y2 * ( -1.0f / 6.0f + y2 * ( 1.0f / 120.0f + y2 * ( -1.0f / 5040.0f + y2 * ( 1.0f / 362880.0f + y2 * ( -1.0f / 39916800.0f ) ) ) ) ) );
}

int runModel( const Perturb& perturb )
{
	std::printf( "\n=== model: a stroke in progress is the same stroke, smaller (alpha, theta and r^2 add); the shader's sine; the recipes against "
	             "[BAB] and [J-Wavy]; the controls; the bake's choice\n" );
	//(a) Additivity, in double, at 10,000 points.
	{
		double worst[ 3 ] = {};
		for( int n = 0; n < 10000; ++n )
		{
			const double x0 = ( Hash01( static_cast< uint32_t >( n ), 1u ) - 0.5 ) * 1280.0, y0 = ( Hash01( static_cast< uint32_t >( n ), 2u ) - 0.5 ) * 720.0;
			const double p  = Hash01( static_cast< uint32_t >( n ), 3u );
			const Op ops[ 3 ] = { combOp( 0.6, 0.8, 0.02, 0.07, 0.15, 0.01, 0.01, 12.0, 0.4 ), circleOp( 0.1, -0.1, 0.2, 0.2, 0.05 ), dropOp( 0.05, 0.02, 0.1 ) };
			for( int t = 0; t < 3; ++t )
			{
				double ax = x0, ay = y0, bx = x0, by = y0;
				//Whole, against the first p of it then the rest. Backward: the
				//later part is undone first.
				const bool inA = backThrough( ops[ t ].At( 1.0 ), 720.0, ax, ay );
				Op rest        = ops[ t ];
				if( ops[ t ].type == OpType::Drop )
					rest.r = perturb.model == 1 ? ops[ t ].r * ( 1.0 - std::sqrt( p ) ) : ops[ t ].r * std::sqrt( 1.0 - p );
				else
					rest.alpha = ops[ t ].alpha * ( 1.0 - p );
				bool inB = backThrough( rest, 720.0, bx, by );
				inB      = inB || backThrough( ops[ t ].At( p ), 720.0, bx, by );
				if( inA != inB )
				{
					//On a rim, in double: count as off by the distance to it.
					worst[ t ] = std::max( worst[ t ], 1e-9 );
					continue;
				}
				if( !inA )
					worst[ t ] = std::max( worst[ t ], std::hypot( ax - bx, ay - by ) );
			}
		}
		Check( worst[ 0 ] < 1e-6 && worst[ 1 ] < 1e-6 && worst[ 2 ] < 1e-6,
		       fmt( "10,000 points: the whole op against its first part then the rest, worst apart: comb %.1e px, circle %.1e px, drop %.1e px "
		            "(tolerance 1e-6)",
		            worst[ 0 ], worst[ 1 ], worst[ 2 ] ) );
	}
	//(b) The shader's sine, in float, against libm.
	{
		double worstSmall = 0.0, worstRel = 0.0;
		for( int n = 0; n <= 200000; ++n )
		{
			const float x    = static_cast< float >( -300.0 + 600.0 * n / 200000.0 );
			const double err = std::fabs( static_cast< double >( sinSeriesF( x ) ) - std::sin( static_cast< double >( x ) ) );
			if( std::fabs( x ) <= kPi / 2 )
				worstSmall = std::max( worstSmall, err );
			worstRel = std::max( worstRel, err / ( 1.0 + std::fabs( x ) ) );
		}
		Check( worstSmall < 2e-7 && worstRel < 2e-7,
		       fmt( "the shader's sine in float, 200,001 points on |x| <= 300: worst %.1e on |x| <= pi/2; worst error / (1 + |x|) %.1e (the range "
		            "reduction's rounding; tolerance 2e-7)",
		            worstSmall, worstRel ) );
	}
	//(c) The recipes.
	{
		auto isReturn = []( const Stroke& a, const Stroke& b ) {
			return a.kind == Stroke::CombStroke && b.kind == Stroke::CombStroke && a.dirX == -b.dirX && a.dirY == -b.dirY && a.spacingS == b.spacingS
			       && std::fabs( std::fabs( b.offsetS - a.offsetS ) - 0.5 * a.spacingS ) < 1e-12;
		};
		const auto& g  = Recipe( Pattern::Gelgit );
		const auto& c  = Recipe( Pattern::Chevron );
		const auto& np = Recipe( Pattern::Nonpareil );
		const auto& b  = Recipe( Pattern::Bouquet );
		const auto& fc = Recipe( Pattern::FrenchCurl );
		const bool gelgit    = g.size() == 4 && isReturn( g[ 0 ], g[ 1 ] ) && isReturn( g[ 2 ], g[ 3 ] ) && g[ 0 ].dirX * g[ 2 ].dirX + g[ 0 ].dirY * g[ 2 ].dirY == 0.0;
		const bool chevron   = c.size() == 4 && isReturn( c[ 0 ], c[ 1 ] ) && isReturn( c[ 2 ], c[ 3 ] ) && c[ 2 ].spacingS * 2.0 == c[ 0 ].spacingS;
		const bool nonpareil = np.size() == 5 && np[ 4 ].spacingS * 4.0 == np[ 0 ].spacingS && np[ 4 ].dirY == 0.0;
		const bool bouquet   = b.size() == 7 && b[ 5 ].waveS != 0.0 && b[ 6 ].waveS == -b[ 5 ].waveS && b[ 5 ].spacingS > np[ 0 ].spacingS
		                     && std::fabs( b[ 6 ].offsetS - b[ 5 ].offsetS ) == 0.5 * b[ 5 ].spacingS;
		const bool curl      = fc.size() == 6 && fc.back().kind == Stroke::Curls;
		const bool stones    = Recipe( Pattern::Stone ).empty() && Recipe( Pattern::Suminagashi ).empty();
		Check( gelgit && chevron && nonpareil && bouquet && curl && stones,
		       fmt( "Gelgit = a 1\" comb, back bisecting, then across and back (%s); Chevron's second pair half the first's width (%s); Nonpareil "
		            "= Gelgit + a 1/4\" comb lengthwise (%s); Bouquet = Nonpareil + two wide rakes, opposite waves, bisecting (%s); French Curl = "
		            "Nonpareil + curls (%s); Stone and Suminagashi no strokes (%s)",
		            gelgit ? "yes" : "NO", chevron ? "yes" : "NO", nonpareil ? "yes" : "NO", bouquet ? "yes" : "NO", curl ? "yes" : "NO",
		            stones ? "yes" : "NO" ) );
	}
	//(d) The controls: each default lands on its stated value; each inverse
	//inverts.
	{
		double worst = 0.0;
		worst        = std::max( worst, std::fabs( DropRadiusFromParam( InfoOf( PT_DROP_SIZE ).defaultValue ) - 0.08 ) );
		worst        = std::max( worst, std::fabs( SpacingFromParam( InfoOf( PT_SPACING ).defaultValue ) - 0.03 ) );
		worst        = std::max( worst, std::fabs( DepthFromParam( InfoOf( PT_DEPTH ).defaultValue ) - 0.15 ) );
		worst        = std::max( worst, std::fabs( StrokeSecondsFromParam( InfoOf( PT_SPEED ).defaultValue ) - 0.6 ) );
		bool rates   = DropRateFromParam( InfoOf( PT_DROP_RATE ).defaultValue ) == 3;
		for( int d = 1; d <= 8; ++d )
			rates = rates && DropRateFromParam( ParamFromDropRate( d ) ) == d;
		for( float v = 0.0f; v <= 1.0f; v += 0.125f )
			worst = std::max( worst, std::fabs( static_cast< double >( ParamFromDepth( DepthFromParam( v ) ) ) - v ) );
		Check( worst < 1e-6 && rates, fmt( "defaults 0.08, 3, 0.03, 0.15, 0.6 s and the inverses: worst %.1e; Drop Rate round-trips 1..8: %s", worst,
		                                   rates ? "yes" : "NO" ) );
	}
	//(e) The bake's choice.
	{
		Bath bath;
		for( int k = 0; k < 60; ++k )
		{
			Op o    = combOp( 1, 0, 0, 0.1, 0.1, 0.01 );
			o.start = 0.0;
			o.duration = k < 30 ? 1.0 : 10.0;
			bath.AddForTest( o );
		}
		const int finishedOnly = bath.BakeCount( 2.0 );  //30 finished: 30 of the 44 allowed
		const int allFinished  = bath.BakeCount( 20.0 ); //all: keep 16 newest
		const int noneFinished = bath.BakeCount( 0.5 );  //none: only enough to leave 48
		Bath small;
		for( int k = 0; k < 48; ++k )
			small.AddForTest( combOp( 1, 0, 0, 0.1, 0.1, 0.01 ) );
		Check( finishedOnly == 30 && allFinished == 44 && noneFinished == 12 && small.BakeCount( 1.0 ) == 0,
		       fmt( "60 ops waiting: %d baked when 30 have finished (30), %d when all have (44), %d when none has (12); 48 waiting: %d (0)",
		            finishedOnly, allFinished, noneFinished, small.BakeCount( 1.0 ) ) );
	}
	//(f) Where the drops land.
	{
		double worst = 0.0;
		for( uint32_t k = 0; k < 10000; ++k )
		{
			double x = 0.0, y = 0.0;
			Bath::DropPosition( k, k % 7, 16.0 / 9.0, x, y );
			worst = std::max( { worst, std::fabs( x ) / ( 8.0 / 9.0 ), std::fabs( y ) / 0.5 } );
		}
		Check( worst <= 0.92, fmt( "10,000 hashed drop centres inside 92%% of the window: furthest at %.3f of the half-width", worst ) );
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
	std::istringstream sheet( "0 Pattern Stone\n60 Pattern French Curl\n0 Trigger 0\n60 Trigger 1\n0 Depth 0\n60 Depth 1\n0 Trigger On Onsets\n"
	                          "60 Trigger On Manual\n" );
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
	const auto pattern = cues.find( PT_PATTERN );
	const auto on      = cues.find( PT_TRIGGER_ON );
	const bool named   = pattern != cues.end() && pattern->second.track.back().second == static_cast< float >( Pattern::FrenchCurl ) && on != cues.end()
	                   && on->second.track.back().second == static_cast< float >( TriggerOn::Manual );
	Check( error.empty() && bad == 0 && cues.size() == 4 && named,
	       fmt( "%d wrong:%s; 'French Curl' and 'Manual' read as their options: %s%s", bad, what.c_str(), named ? "yes" : "NO",
	            error.empty() ? "" : ( "; " + error ).c_str() ) );
	return Verdict();
}
