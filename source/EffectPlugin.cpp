#include "Snowglobe.h"

/**
    The registration, in its own MODULE target (CMakeLists.txt): the
    CFFGLPluginInfo below registers itself from a file-scope constructor that
    nothing references, so it must not live in the OBJECT library the harness
    also links. `SW Snowglobe` is twelve characters, inside FFGL's 16.
*/
static CFFGLPluginInfo PluginInfo(
	PluginFactory< snowglobe::SnowglobePlugin >,// Create method
	"SG01",                                    // Plugin unique ID of maximum length 4
	"SW Snowglobe",                            // Plugin name
	2,                                        // API major version number
	1,                                        // API minor version number
	0,                                        // Plugin major version number
	1,                                        // Plugin minor version number
	FF_EFFECT,                                // Plugin type
	"A snow globe shaken by the music, with the clip as the scene inside: PET flakes (discs, or beads) falling at "
	"their low-Reynolds-number terminal speed in a water-glycerol fill, carried by the slowest viscous swirl mode "
	"after each kick, beat or button press, and lying still once they land; the scene seen through the fill's one "
	"refracting surface, magnified by n at the centre and squeezed at the rim.",
	"Snowglobe FFGL effect"// About
);

extern "C" const char* SnowglobeBuildStamp()
{
	return "snowglobe " SNOWGLOBE_VERSION ", built " __DATE__ " " __TIME__;
}
