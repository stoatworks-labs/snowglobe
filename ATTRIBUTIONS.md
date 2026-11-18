# Attributions

Snowglobe is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### earshot — Stoatworks earshot

<https://github.com/stoatworks-labs/earshot>  
Licence: MIT  
Copyright: Stoatworks Labs

external/earshot/ is earshot, the fleet's shared audio front end, as a snapshot: one commit taken with git archive and left unmodified, named in external/earshot/SNAPSHOT. It is built into the plugin by add_subdirectory and supplies the capture device, chosen by name with a Silent state, and the audio analysis the plugin reads every frame. It brings miniaudio with it, credited below.

### The plugin shell, the triggers, the harness and the tooling — Stoatworks marbling, and rubens and haze before it

<https://github.com/stoatworks-labs/marbling>  
Licence: MIT  
Copyright: Stoatworks Labs

The OBJECT core with the plugin registered from the MODULE target alone, the table of controls, the device menu whose values are earshot's device keys (spasis's, by way of rubens), the host-clock vote (rosette's, by way of haze), SetBeatInfo read as ferrofluid's Drive does (each quarter of the bar placed inside the frame, the plugin's own clock when the phase stands still) and the device gate; the harness's rig, PNG writer, cue sheets, --pipe/--film (SIGPIPE ignored, a closed stdout exits 1), --negative with its perturbation hooks, --names, --cues, --coreaudio and its interposer (tools/sgtest/audiospy.c), --offline and --bench; tools/verify.sh, tools/mutate.sh, tools/glslc.sh, tools/sweep.py and tools/clips.py (nipkow's, by way of intensifier). All adapted; marbling took most of them from rubens (https://github.com/stoatworks-labs/rubens) and haze (https://github.com/stoatworks-labs/haze).

### The pass buffers, the log and the GL state guard — Stoatworks millpond and vectrix, from resolume-scopes

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.{h,cpp} (FFGLFBO with the SDK's colour-texture leak fixed) and source/Diag.{h,cpp} are millpond's, by way of marbling; source/GLState.h is radar's, which is vectrix's (https://github.com/stoatworks-labs/vectrix), which lifted it from resolume-scopes' Scopes.cpp: the host's GL state captured and put back.

### The release workflow and the DLL check — Stoatworks woofer, and mirrorball and filings before it

<https://github.com/stoatworks-labs/woofer>  
Licence: MIT  
Copyright: Stoatworks Labs

.github/workflows/ci.yml and release.yml are woofer's, which are mirrorball's CI and filings' release (xenon's line): the universal bundle and the Windows DLL, ATTRIBUTIONS.md in each zip, the Windows installer per user into Resolume's Extra Effects. tools/wincheck.ps1, which loads the DLL as a host loads it and reads back its name, id, type and parameters, is woofer's (mirrorball's). Adapted. scripts/release-lib.sh is the fleet's, unchanged.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

### miniaudio, inside earshot

<https://github.com/mackron/miniaudio>  
Licence: Public Domain (Unlicense) or MIT-0, at your choice  
Copyright: David Reid

Arrives inside the vendored earshot snapshot, at external/earshot/third_party/miniaudio/miniaudio.h, unmodified. Not vendored separately.

earshot's capture backend: it lists the capture devices and runs the audio callback, on CoreAudio and WASAPI. The plugin never calls miniaudio itself; it arrives only with earshot.

## Data this project ships

Measurements made by other people and carried inside the product. Each is credited the way its publisher asks, and where a licence requires its attribution to travel with the data, that text is reproduced here.

### The fill's viscosity: Cheng's formula — Nian-Sheng Cheng

<https://doi.org/10.1021/ie071349z>

N.-S. Cheng, "Formula for the viscosity of a glycerol-water mixture", Ind. Eng. Chem. Res. 47(9) (2008) 3285-3288, read in the author's preprint on 2026-11-17. source/Fill.cpp evaluates his equations for the viscosity of water and of glycerol against temperature and his mixing rule between them, at 20 °C, for every Fill; the physics checks take his 3.5 % maximum error against Segur and Oberstar's measurements as their tolerance. Only the equations and their coefficients are used.

### The fill's density: Volk and Kähler's model — Andreas Volk and Christian J. Kähler

<https://doi.org/10.1007/s00348-018-2527-y>

A. Volk, C. J. Kähler, "Density model for aqueous glycerol solutions", Exp. Fluids 59:75 (2018), as the authors' MATLAB file densityViscosityWaterGlycerolSolution.m (Volk and Westbrook 2017) writes it out, read on 2026-11-17: the densities of water and glycerol against temperature and the mixture's contraction, evaluated in source/Fill.cpp. Only the equations and their coefficients are used.

### The fill's tables, and Hoyt's index fits — The Glycerine Producers' Association

Physical Properties of Glycerine and its Solutions (Glycerine Producers' Association, New York, 1963), read from the Wayback Machine's copy on 2026-11-17. The harness carries rows of Table 4 (density, Bosart and Snoddy), Table 17 (viscosity, Segur and Oberstar 1951) and Table 23 (refractive index, Hoyt 1934), and measures the flakes' speeds, the settling time and the swirl's decay against them rather than against the fits (tools/sgtest). source/Fill.cpp evaluates Hoyt's fits for the index as Table 23 prints them, linear for 45-79 % and the cubic for 80-100 %, and takes water's index from the table's 0 % row. Only the numbers are used.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### Water's index — L. W. Tilton and J. K. Taylor

"Refractive index and dispersion of distilled water for visible radiation, at temperatures 0 to 60 °C", J. Res. NBS 20, RP1085 (1938), Table 5, read on 2026-11-17: a cross-check of the GPA's 0 % row, and the harness's wrong model "water's index".

### The flakes' drag — J. I. Katz; T. Tomita, S. Taguchi and T. Tsuji; M. J. Mercier et al.

Katz, "Sub-Suns and Low Reynolds Number Flow" (arXiv ao-sci/9609001): a thin disc's drag broadside (16 μaU) and edgewise ((32/3) μaU), citing Happel and Brenner and Lamb (neither read), and no torque in the Stokes limit. Tomita, Taguchi and Tsuji (arXiv 2504.13493): the broadside value again. Mercier et al., "Settling disks in a linearly stratified fluid", J. Fluid Mech. (2020): a disc keeps its orientation in the Stokes regime, and the drag correlation used to bound the error above Re → 0. Each read on 2026-11-17; together they are the disc mobility in source/Globe.cpp.

### The flakes themselves: glitter and snow globes — Geotech; D.-B. Johnson; Wikipedia

Geotech's "Geoglit" brochure (polyester glitter cut from 25 and 48 µm foil, 0.1-2.5 mm); Wikipedia's "Polyethylene terephthalate" (density 1.38 g/cm³) and "Glitter"; D.-B. Johnson, "Let it Snow! Those Sensational Snow Globes", Journal of Antiques and Collectibles (6 Dec 2024), and Wikipedia's "Snow globe": what globes are filled with, and what their snow was and is. Read on 2026-11-17; they set the flakes' material, thickness and sizes.

### The optics — OpenStax; H. D. Young and R. A. Freedman

OpenStax University Physics vol. 3, §2.3 "Images Formed by Refraction", eq. 2.11 (refraction at one spherical surface); Young and Freedman, University Physics 14th ed., problem 34.21, "A Spherical Fish Bowl", as quoted on a third-party solutions page (the book was not read): an object at the centre appears at the centre, magnified by n. Read on 2026-11-17; the harness's --magnify holds the plugin to it. Hecht's Optics was not reachable.

### The fluid after a shake — H. P. Greenspan and L. N. Howard

<https://doi.org/10.1017/S0022112063001415>

"On a time-dependent motion of a rotating fluid", J. Fluid Mech. 17 (1963) 385-404, read on 2026-11-17: the linearised equations, and the Ekman spin-up time against the diffusion time, by which the spec argues the linear viscous decay is the leading process here. The swirl's mode and its root are derived in the spec, not read.

## Standards and published specifications

What the implementation is measured against.

- **Standard acceleration of gravity** — g_n = 9.80665 m s⁻², exact (NIST CODATA): what pulls every flake down through the fill (source/Globe.h).

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
