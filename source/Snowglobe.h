#pragma once

#include "Audio.h"
#include "Controls.h"
#include "Globe.h"
#include "Optics.h"
#include "PassBuffer.h"
#include "Shaders.h"

#include <FFGLSDK.h>

#include "StoatworksAboutParams.h"

#include <cstdint>
#include <vector>

namespace snowglobe
{

/// CPU-side negative controls (the harness's hooks; a shipped instance sets 0).
enum CpuHook : int
{
	kCpuEventsAtFrame   = 1 << 0,///< beats and kicks placed at the frame, not inside it
	kCpuFloatClock      = 1 << 1,///< the host clock read through a float
	kCpuResizeRestarts  = 1 << 2,///< a resize starts the globe again
	kCpuIgnoreGate      = 1 << 3,///< the device menu listed and Audio Input applied whatever Trigger On says
	kCpuLeakState       = 1 << 4,///< the composite leaves its program and textures bound
	kCpuWaterIndex      = 1 << 5,///< the scene refracts with water's n, whatever the fill
	kCpuFlakesStraight  = 1 << 6,///< flakes projected without refraction
	kCpuAnyBand         = 1 << 7,///< a kick from any band, not the lowest
	kCpuDropPresses     = 1 << 8 ///< presses between two frames collapse to one
};

/// Kicks are earshot onsets in this band only (40-160 Hz).
constexpr int kKickBand = 0;

struct ShakeRecord
{
	int64_t us;     ///< on the plugin's clock
	double strength;///< S
};

/// What the last frame did: the harness reads it.
struct FrameData
{
	int64_t clockUs   = 0;
	int64_t stateUs   = 0;
	int drawn         = 0;///< flakes drawn
	int behind        = 0;///< of them, behind the card
	int suspended     = 0;///< flakes not at rest
	uint64_t stateHash = 0;
	double globePx    = 0.0;///< the silhouette's radius, px
	double index      = 1.0;///< n the scene used
};

/// One drawn flake, as the GPU got it (the harness reads these).
struct DrawnFlake
{
	int index;
	double cx, cy;      ///< centred px, y up
	double a1x, a1y, a2x, a2y;
	double alpha;
	bool behind;
};

/**
    The plugin, `SW Snowglobe` (EffectPlugin.cpp registers it).

    Host indices are ParamIds (one plugin, HostOrder() is the table).
*/
class SnowglobePlugin : public CFFGLPlugin
{
public:
	SnowglobePlugin();

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;
	void SetBeatInfo( float bpm, float barPhase ) override;

	int HostIndexOf( unsigned int id ) const
	{
		return id < PT_COUNT ? static_cast< int >( id ) : -1;
	}
	void SetById( unsigned int id, float value )
	{
		SetFloatParameter( id, value );
	}
	float GetById( unsigned int id ) const
	{
		return id < PT_COUNT ? params[ id ] : 0.0f;
	}

	//The harness's hooks. A host never calls these.
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	void SetHooksForTest( int shaderBits, int cpuBits, int physicsBits )
	{
		hooks    = shaderBits;
		cpuHooks = cpuBits;
		globe.SetHooks( physicsBits );
	}
	/// Flakes simulated and drawn: the Count control's, or this if >= 0.
	void SetCountForTest( int count )
	{
		countOverride = count;
	}
	/// Shakes the harness places itself (on the plugin's clock), as if kicks.
	void QueueShakeForTest( int64_t us, double strength )
	{
		queued.push_back( { us, strength } );
	}
	const FrameData& LastFrame() const
	{
		return frame;
	}
	const std::vector< ShakeRecord >& ShakeLog() const
	{
		return shakeLog;
	}
	const std::vector< DrawnFlake >& DrawnForTest() const
	{
		return drawn;
	}
	Globe& GlobeForTest()
	{
		return globe;
	}
	AudioInput& AudioForTest()
	{
		return audio;
	}
	Physics CurrentPhysics() const;

private:
	void UpdateClock();
	std::vector< ShakeEvent > gatherEvents( int64_t frameStartUs, int64_t frameUs );
	void handleKicks( int64_t frameStartUs, int64_t frameUs, std::vector< ShakeEvent >& events );
	void refreshMenu();
	void buildFlakes( const Physics& physics, int width, int height, double globePx );
	void drawFlakes( int first, int count, int width, int height );

	GLint where( const ffglex::FFGLShader& shader, const char* name );
	void uniform( const ffglex::FFGLShader& shader, const char* name, int v );
	void uniform( const ffglex::FFGLShader& shader, const char* name, float v );
	std::vector< std::pair< std::pair< GLuint, const char* >, GLint > > uniformCache;

	const std::vector< unsigned int >& hostOrder;
	float params[ PT_COUNT ] = {};

	ffglex::FFGLShader sceneShader, flakeShader, compositeShader;
	ffglex::FFGLScreenQuad quad;
	GLuint flakeVao = 0, flakeVbo = 0;
	PassBuffer target;
	int targetW = 0, targetH = 0;

	Globe globe;
	AudioInput audio;
	FrameData frame;
	std::vector< ShakeRecord > shakeLog;
	std::vector< ShakeEvent > queued;
	std::vector< V3 > positions;
	std::vector< DrawnFlake > drawn;
	std::vector< float > instances;
	int behindCount   = 0;
	int countOverride = -1;
	int shakePresses  = 0;
	bool shakeHeld    = false;
	int64_t lastKickUs = INT64_MIN / 2;
	uint32_t lastOnsetCount[ earshot::kMaxBands ] = {};
	bool onsetsPrimed = false;

	//The beat (marbling's): the host's bar phase, or a clock of our own.
	float bpm          = 120.0f;
	float barPhase     = 0.0f;
	float lastBarPhase = -1.0f;
	bool hostBeatSeen  = false;
	double lastPhase   = -1.0;

	//The clock. Integer microseconds from the first frame (marbling's).
	double hostTime     = -1.0;
	int64_t nowUs       = 0;
	int64_t lastNowUs   = INT64_MIN;
	int64_t clockUs     = 0;
	double clockScale   = 0.0;
	double wallStart    = -1.0;
	double lastRawTime  = -1.0;
	double lastWallTime = -1.0;
	int secondsVotes = 0, millisVotes = 0, clockFrames = 0;
	bool settledJump = false;

	int hooks    = 0;
	int cpuHooks = 0;
};

} // namespace snowglobe
