#pragma once

#include <string>

/**
    The passes, all at the host's raster, into one RGBA32F buffer of
    premultiplied colour (code values, as the clip's), then onto the host's:

        1. **flakes behind** the card (z < 0), instanced quads, over-blended;
        2. **scene**: each pixel's four rays from the pinhole, refracted into
           the fill at the sphere and met with the card z = 0; the clip
           filtered by hand, premultiplied (mirrorball's trap); over-blended
           onto the flakes behind; transparent outside the globe;
        3. **flakes in front**, instanced quads, over-blended;
        4. **composite** onto the host's target: the premultiplied Mix with
           the clip (Mix 0 hands the clip through by texelFetch), and
           unpremultiplied.

    No trig in any of them (the software renderer's sin and atan are off):
    the sphere needs only sqrt. Assembled at InitGL by `Assemble()`, the one
    place the order is written; tools/glslc.sh's ASSEMBLED table mirrors it.
*/
namespace snowglobe::shaders
{
extern const char* const kVersion;
extern const char* const kQuadVertex;
extern const char* const kCommon;
extern const char* const kSceneFragment;
extern const char* const kFlakeVertex;
extern const char* const kFlakeFragment;
extern const char* const kCompositeFragment;

enum class Pass
{
	Scene,
	Flake,
	Composite
};

std::string AssembleFragment( Pass pass );
std::string AssembleVertex( Pass pass );

/// Hook bits (the Hooks uniform). A shipped instance sets 0. Each is a
/// negative control: a model that must make a check FAIL.
enum Hook : int
{
	kHookNoPremultiply = 1 << 0,///< the clip sampled straight: what is under alpha 0 leaks
	kHookNoRefract     = 1 << 1,///< the rays go straight through the fill
	kHookHardEdge      = 1 << 2 ///< flakes drawn without their edge ramp (coverage 0 or 1)
};
} // namespace snowglobe::shaders
