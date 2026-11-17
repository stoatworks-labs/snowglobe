# Attributions

Snowglobe is built on other people's work. This file lists what that work is, who did it, and
what it is doing here.

**A provisional hand copy (2026-11-17).** Snowglobe is not yet registered in
`stoatworks-backend`, so this was written by hand in the generated file's shape. Once it is
registered, `scripts/sync-attributions.py` generates it from the master lists; edit it there
then, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without
their work.

### Live audio — Stoatworks earshot

<https://github.com/stoatworks-labs/earshot> (intended home; on 2026-11-17 a local repository only)  
Licence: MIT  
Copyright: Stoatworks

Vendored at external/earshot as a snapshot of commit 8124191, unmodified (external/earshot/SNAPSHOT). The capture device worker, the device list and keys, the band onsets (`Band::onsetCount`, `level`) from which Trigger On = Kick takes its kicks, `feed()` (which the harness drives), and the fallback to Resolume's FFT bins. earshot's own attributions (spasis, pattern, miniaudio) are in external/earshot/ATTRIBUTIONS.md.

### The plugin shell, the triggers, the harness and the tooling — Stoatworks marbling

marbling: a local repository on 2026-11-17  
Licence: MIT  
Copyright: Stoatworks Labs

The OBJECT core with the plugin registered from the MODULE target alone, the table of controls, the device menu whose values are earshot's device keys (spasis's, by way of rubens), the host-clock vote (rosette's, by way of haze), `SetBeatInfo` read as ferrofluid's Drive does (each quarter of the bar placed inside the frame, the plugin's own clock when the phase stands still), the device gate, and the About block (source/StoatworksAbout*.h, a provisional hand copy of marbling's); the harness's rig, PNG writer, cue sheets, `--pipe`/`--film` (SIGPIPE ignored, a closed stdout exits 1), `--negative` with its perturbation hooks, `--names`, `--cues`, `--coreaudio` and its interposer (tools/sgtest/audiospy.c), `--offline` and `--bench`; tools/verify.sh, tools/mutate.sh, tools/glslc.sh, tools/sweep.py and tools/clips.py (nipkow's, by way of intensifier). All adapted; marbling took most of them from rubens and haze (local repositories).

### The pass buffers, the log and the GL state guard — Stoatworks millpond and vectrix, from resolume-scopes

<https://github.com/stoatworks-labs/millpond>, <https://github.com/stoatworks-labs/vectrix>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.{h,cpp} (FFGLFBO with the SDK's colour-texture leak fixed) and source/Diag.{h,cpp} are millpond's, by way of marbling; source/GLState.h is radar's, which is vectrix's, which lifted it from `resolume-scopes`' Scopes.cpp: the host's GL state captured and put back.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl, pinned at b1afaf9.

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### miniaudio — David Reid

<https://github.com/mackron/miniaudio>  
Licence: public domain (the Unlicense) or MIT No Attribution, at your choice  
Copyright: David Reid

Arrives inside the earshot snapshot at external/earshot/third_party/miniaudio, unmodified. Capture device enumeration and the audio callback.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

## Research this is built on

No code was taken from these. Each was read on 2026-11-17; the spec
(`SPEC-snowglobe.md`, "The literature") says where each was read and what could not be.

### The fill's viscosity — Nian-Sheng Cheng 2008

N.-S. Cheng, "Formula for the viscosity of a glycerol−water mixture", Ind. Eng. Chem. Res. 47(9) (2008) 3285–3288, read in the author's preprint. The viscosity of the fill against its glycerol fraction at 20 °C (source/Fill.cpp), and the 3.5 % maximum error the physics checks take as their tolerance.

### The fill's tables — the Glycerine Producers' Association 1963

*Physical Properties of Glycerine and its Solutions* (Glycerine Producers' Association, New York, 1963), read from the Wayback Machine's copy. Table 4 (density), Table 17 (viscosity, Segur & Oberstar 1951) and Table 23 (refractive index, Hoyt 1934, with Hoyt's fits). The speeds, the settling time and the swirl's decay are measured against these tables, not the fits; the index is Hoyt's fits (source/Fill.cpp).

### The fill's density — Andreas Volk and Christian J. Kähler 2018

A. Volk, C. J. Kähler, "Density model for aqueous glycerol solutions", Exp. Fluids 59:75 (2018), as implemented in Volk and Westbrook's MATLAB file `densityViscosityWaterGlycerolSolution.m`. The density of the fill (source/Fill.cpp).

### Water's index — Tilton and Taylor 1938

L. W. Tilton, J. K. Taylor, "Refractive index and dispersion of distilled water for visible radiation, at temperatures 0 to 60 °C", J. Res. NBS 20, RP1085 (1938), Table 5. A cross-check of the GPA's 0 % row; the wrong model "water's index" in the harness.

### The flakes' drag — J. I. Katz; Tomita, Taguchi and Tsuji; Mercier et al.

J. I. Katz, "Sub-Suns and Low Reynolds Number Flow" (arXiv ao-sci/9609001): a thin disc's drag broadside (16 μaU) and edgewise ((32/3) μaU), citing Happel & Brenner and Lamb (neither read), and no torque in the Stokes limit. T. Tomita, S. Taguchi, T. Tsuji (arXiv 2504.13493): the broadside value again. M. J. Mercier et al., "Settling disks in a linearly stratified fluid", J. Fluid Mech. (2020): a disc keeps its orientation in the Stokes regime, and the drag correlation used to bound the error above Re → 0. The disc mobility in source/Globe.cpp.

### The flakes themselves — glitter and snow globes

Geotech's "Geoglit" brochure (polyester glitter cut from 25 and 48 µm foil, 0.1–2.5 mm); Wikipedia's "Polyethylene terephthalate" (density 1.38 g/cm³) and "Glitter"; D.-B. Johnson, "Let it Snow! Those Sensational Snow Globes", Journal of Antiques and Collectibles (6 Dec 2024), and Wikipedia's "Snow globe": what globes are filled with and what their snow was and is.

### The optics — OpenStax; Young and Freedman

OpenStax *University Physics* vol. 3, §2.3 "Images Formed by Refraction", eq. 2.11 (refraction at one spherical surface); H. D. Young, R. A. Freedman, *University Physics* 14th ed., problem 34.21, "A Spherical Fish Bowl", as quoted on a third-party solutions page (the book was not read): an object at the centre appears at the centre, magnified by n. Hecht's *Optics* was not reachable.

### The fluid after a shake — Greenspan and Howard 1963

H. P. Greenspan, L. N. Howard, "On a time-dependent motion of a rotating fluid", J. Fluid Mech. 17 (1963) 385–404: the linearised equations, and the Ekman spin-up time against the diffusion time, by which the spec argues the linear viscous decay is the leading process here. The swirl's mode and its root are derived in the spec, not read.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
