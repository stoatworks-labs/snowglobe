# AGENTS.md: Snowglobe

Read this before changing the fill, the flakes' mobility, the swirl or the shake, the
projection of flakes, the scene trace, the flake coverage, the event times, the clock or the
device gate. `CLAUDE.md` has the commands; the spec is
`~/Projects/resolume/specs/SPEC-snowglobe.md` (its As built section records where this repo
departs from it).

## Status

**v0.1.1, 2026-11-19: Kick on Resolume's FFT fires** (v0.1.0, public from 2026-11-18, never
fired it). `OXBOW=… TMPDIR=… tools/verify.sh` passes on this
Mac (Apple Silicon) against a fresh universal Release build: 109 checks at 1280x720 and
320x180, every picture check again on Apple's software renderer at 320x180, `--offline` with
no GL context, the box rule against `--flakes`' tolerances, Arena's two DXV alpha clips and the
defaults' measurement through `--pipe`, the pipe, 30 of 30 negative controls, 46 of 46
mutants, 10 of 10 controls alive in the sweep and in the bare sweep, oxbow reading and
rendering the bundle with devices disabled. **Windows**: Arena 7.27.1 on
win-lab (llvmpipe, no sound device), the fleet's gate on the release workflow's DLL, 9 passed,
0 failed, 2 skipped (Audio Input and Audio need sound; Shake Now inert), on a pre-tag build and
again on the v0.1.0 zip's own DLL (a different hash), and on the v0.1.1 zip's own DLL
(2026-11-19, sha256 d78f69a6…be85): 9, 0, 2 again. **Arena 7.29.0 on
this Mac (2026-11-19)**, the v0.1.0 zip's bundle over REST: registers, 18 controls as declared,
still at rest, every control acts, Beat shakes it on Arena's clock and Shake Now on a press;
Kick never fired on Resolume's FFT with `Beat 001.wav` playing below (0 of 69 pairs at
Shake 0.3, 0 of 41 at 1.0): earshot `8124191` put bin j at (j + 0.5)/64 of 22.05 kHz and gave
each bin to the band holding its centre, so band 0 (40–160 Hz) got no bin. **v0.1.1 on the
same Mac, the same day** (the v0.1.1 zip's own signed bundle, and a pre-tag build before it):
**Kick moved the globe in 69 of 69 pairs at Shake 0.3 and 40 of 40 at 1.0 with `Beat 001.wav`**
(both bundles), 0 of 24 before the loop, at rest again after it; `Bass 001.wav` (no drums) shook it too, 40 of 40,
because Resolume's bin 0 is 0–375 Hz ("Resolume's FFT, measured" below); Beat 24 of 24, Shake
Now 0 of 9 unpressed and 24 of 24 after one press; the log reads "host audio at 48000 Hz".
**No live audio device ever opened.** MSVC compiles
snowglobe and earshot with no warnings (CI). earshot is a snapshot of
`github.com/stoatworks-labs/earshot` at `02306da`, unpatched. A user guide (`docs/USER-GUIDE.md`)
and a browser demo ("Browser demo" below); no OpenFX port.
**Flashes:** the release's survey (33 demo clips, 20 s each at 60 fps, music through `--wav`,
the fleet's flashcheck) found none over the line at the defaults (worst window 5 transitions in
a second) and one clip over it at Shake 1 and Count 1 under Kick or Beat (Cyberspace_09, 7-8 in
a third-of-frame window, bare 0, with four different mixes): the guide, README, demo, video and
posts carry the warning; no default was changed.

## The idea

**A snow globe is a slow, viscous, settling flow seen through one curved surface.** The clip
is a card standing at the centre of a sphere of water and glycerol. White flakes cut from PET
film fall through the fill at the speed low-Reynolds-number drag allows them, a shake sets the
fill turning in its slowest mode, which dies away viscously, and a flake that reaches the
glass lies there until the next shake. The viewer looks at it all through the one surface
between the fill and the air, which magnifies the middle of the card by the fill's n and
squeezes its rim. Every one of those numbers comes from a table or a fit someone else
published, and the harness measures each off the rendered pixels.

## What the literature settled (the spec has the sources and the claim-by-claim table)

- **"Settling at their Stokes terminal velocity … as r²" holds for spheres only.** Real flakes
  are flat: a thin disc falls at F/(16μa) broadside and F/((32/3)μa) edgewise ([Katz],
  [Tomita]), and a disc cut from one film weighs ∝ a², so it falls ∝ a. The plugin has both
  shapes (Snow = Flakes or Beads); `--fall` measures the exponent 1 for discs and 2 for beads.
- **"As a ball lens" is not what it is.** The scene is inside the water, so there is one
  refracting surface; an object at the centre appears at the centre magnified by n ([OpenStax]
  eq. 2.11; [Y&F]'s fish bowl). The thin glass wall changes where a ray enters, not where it
  goes (the spec shows why), so it is left out.
- **The fill's numbers** are the GPA's tables (density, Segur & Oberstar's viscosity, Hoyt's
  index), [Cheng]'s viscosity formula and [Volk]'s density model. The physics checks measure
  against the tables; the plugin uses the fits. Fill spans w = 0.60–0.80 because below 0.60
  the largest discs pass Re 1 and leave Stokes.
- **"Seconds to settle" is the fall, not the fluid.** A flake's response time is under 3 ms, so
  it moves at the fluid's velocity plus its settling velocity; the swirl dies in τ = R²/(x1²ν)
  (8.4 s at w = 0.60); settling takes tens of seconds at the defaults, as real globes do. No
  water-glycerol fill settles mm flakes over 80 mm in a few seconds while staying in Stokes.
- Not read: Happel & Brenner and Lamb (the disc drags are taken from [Katz] and [Tomita],
  which cite them), Hecht's *Optics*, the Smithsonian's Perzy article (403), Anton Paar's page
  (behind a bot check, not bypassed). No source gives a globe's usual size, viewing distance
  or a maker's fill fraction.

## Decisions taken without asking (the brief says decide and write it down)

- **Identity.** `SW Snowglobe`, `SG01`, effect, `com.stoatworks.ffgl.snowglobe`, harness
  `sgtest`.
- **Geometry (chosen).** Inner radius R = 40 mm; the viewer a pinhole at D = 10R; the card the
  disc z = 0 with the clip's height across the diameter; the silhouette's diameter Globe ×
  the frame's height.
- **State on the CPU in double, not the GPU** (spec, State): the "nothing moves" and frame-rate
  claims are bit-exact, the counts are at most 3000, and the harness can read the state.
  Counter-based seeds (SplitMix64 of the flake and the shake's sequence number) so a run
  replays exactly and the n-th shake lifts the same flakes the same way at any frame rate.
- **Time.** Integer microseconds from the first frame; RK4 on an 8 ms grid; an event splits the
  step it lands in at its microsecond; a frame draws the state advanced from the last grid
  point by one partial step that is not stored. The host's clock is voted seconds or
  milliseconds against the wall clock (marbling's), so 499 million ms is only an offset.
- **The swirl is one vector.** Only the slowest toroidal mode, u = f(r)(W × x),
  f = 3j1(kr)/(kr), k = x1/R, decaying at νk²: every other mode of the sphere decays at least
  1.65 times faster. A shake adds S·Ω_max along a seeded horizontal axis, Ω_max = 2 rad/s
  (chosen).
- **The shake (chosen; no source models a hand)** lifts every *resting* flake straight up by a
  seeded height in [0, 2R·S] (clipped inside the glass) with a new seeded orientation, and adds
  to the swirl. A flake already in suspension is moved only by the swirl.
- **A flake at rest is pinned** until a shake: no sliding down the glass. That is what makes
  `--still` bit-exact, and it is the claim the brief asks for.
- **Kicks.** An earshot onset in band 0 (40–160 Hz) is a kick only while band 0's level is at
  least **0.25 of the loudest band's** (`kKickDominance`, chosen). Without that gate the hats'
  low-frequency splatter fired band 0 once the kicks stopped (band-0 level 3.9e-4 against band
  3's 0.022), and the globe never stayed still on hats. Kicks within 50 ms are one. Each is
  placed at hostNow − (audioTime − lastOnsetTime), clamped inside the frame.
- **Resolume's FFT (v0.1.1).** The host's bins are read with earshot's default law, Arena's
  measured one (Magnitude, Linear), and the sample rate FFGL's `SetSampleRate` reports: the
  plugin overrides it, keeps the rate in an atomic, and hands it to earshot on the render
  thread before the bins are read (`AudioInput::SetHostRate`; a rate outside 8–768 kHz, or
  none, leaves earshot's 48 kHz). A rate change is logged once ("host audio at 48000 Hz:
  Resolume's FFT bins are 375 Hz each"). No control for the law: snowglobe never had one, and
  woofer and rubens are where a user can change it. earshot shares each bin among the bands it
  overlaps, so band 0 is 0.32 of bin 0 (0–375 Hz): **on the host path Kick is the low end**,
  bass notes and kick/bass beating included. The dominance gate and the 50 ms merge are
  unchanged.
- **Beat.** Each quarter of the bar is a shake at Shake's strength, the crossing interpolated
  inside the frame (ferrofluid's, by way of marbling); a bar phase that never moves gets the
  plugin's own clock at the last BPM.
- **The button (Shake Now) is a full shake, S = 1, under every Trigger On.**
- **Alpha.** The clip is filtered by hand, premultiplied, four texels (mirrorball's trap: a
  host bilinear on straight alpha bleeds colour). Where the card is transparent, the output is
  transparent (the ray is let out unrefracted; flakes behind the card show through); outside
  the globe the output is transparent, so the globe stands over the layers below. Mix is a
  premultiplied mix with the clip; Mix 0 returns the clip by texelFetch, bit for bit.
- **Flakes drawn as instanced quads**, never points (Apple's GL rounds point sizes). Each is an
  ellipse from the projection's Jacobian (discs: the disc's own plane; beads: the full 2×3
  Jacobian, J Jᵀ), semi-axes at least half a pixel, coverage a one-pixel ramp box-filtered by a
  4×4 midpoint rule per pixel, scaled on the CPU by the ramp's exact integral
  s1·s2 / (w1·w2 + (w1/w2 + w2/w1)/24), and clipped to the silhouette. In three layers: behind
  the card, the card, in front.
- **Resize.** The state is in metres; a resize changes only the drawing. `--resize` requires the
  state and the last frame to be identical to a run at the new size throughout.
- **Count lower, then higher.** Flakes past the count are neither stepped nor drawn; when the
  count rises they come back where they were, at rest or in mid-fall.
- **Size and Range move no resting flake.** A flake that grows while at rest pokes up to the
  difference in radius past the glass until the next shake (it is clipped to the silhouette in
  the picture). `--robust` checks every flake inside the glass at fixed controls only.
- **The device gate.** CoreAudio is touched only when Trigger On is Kick and
  `SNOWGLOBE_NO_DEVICES` is unset; the device menu is listed on the first frame that needs it,
  never in the constructor. earshot's CMake links CoreAudio, AudioToolbox and CoreFoundation at
  load time, so the claim is "nothing calls into them or loads them by name", not "not linked".
- **Workflows only once public.** Actions are for public repos only on this machine. Until it
  was public snowglobe shipped none, so nothing could run by accident; `ci.yml` and
  `release.yml` came with the public repo (woofer's, from mirrorball's CI and filings' release,
  with `wincheck.ps1`).

## Resolume's FFT, measured

Measured in Arena 7.29.0 on this Mac on 2026-11-19 with SW Hostprobe; earshot's AGENTS.md
("Resolume's 64 bins, measured") has the probe and every number. What snowglobe depends on:

- **64 linear bins of 375 Hz, 0–24 kHz**: bin j is [j, j+1) × 375 Hz, 1/64 of the Nyquist of
  the audio engine's 48 kHz. A 44.1 kHz clip reads the same bins; FFGL `SetSampleRate` says
  48000 (snowglobe's log in Arena: "host audio at 48000 Hz: Resolume's FFT bins are 375 Hz
  each").
- **Magnitude, linear in amplitude** (×1000 over 60 dB; a sine of amplitude A reads 0.25–0.263
  A), mono (L+R)/2, silence exactly 0.
- **A new spectrum about every 43 ms** (2048 samples at 48 kHz), held between updates.
- Grouping inferred, not seen: a tone on a bin edge splits 1:3, which fits each bin being the
  sum of 16 fine bins of a 2048-point Hann FFT. `--hostfft` builds its bins that way.
- `Beat 001.wav` in bin 0: mean 0.0199, peak 0.109. A kick, the bass and the low toms all land
  in bin 0, and 64 linear bins can't separate them.

`--hostfft` feeds a 128 BPM loop (a kick, claps on 2 and 4, hats on the eighths, the claps and
hats high-passed so they carry next to nothing below 375 Hz) through that model into the
plugin's FFT buffer, with `SetSampleRate` called as Arena calls it: 10 of 10 kicks shake it at
48 kHz (latest 60.4 ms after its kick, limit 102.0: two spectra and a frame) and at 44.1 kHz
(62.5 ms, limit 109.5); claps and hats alone, none; over a held 49 Hz bass, no kick missed (14
shakes for 10 kicks, 4 of them the bass's). It also reads the host rate back from earshot (48000,
44100, and 48000 kept when the host sends 0). Built against earshot `8124191` the check fails,
0 of 10.

Replaying Arena's own recorded bins for `Beat 001.wav` (30 beats) through earshot `02306da` and
snowglobe's rule: 30 of 30 beats shake it, plus 18 shakes 130–155 ms after a beat (the loop's
low notes, or kick and bass beating, in bin 0). Through `8124191`'s rule: 0.

## Defaults

All chosen (the spec's table): Shake 0.3, Trigger On Kick, Audio Input Resolume FFT, Fill 0
(w = 0.60, the fastest fill Stokes allows), Snow Flakes, Size a = 1.0 mm, Range 0.5, Count
600, Globe 0.9, Mix 1. No source gives a globe's flake count or a VJ's shake.

**What the demo clips measure of them** (`tools/clips.py defaults`, every one of the 33 `.mov`s
under `/Applications/Resolume Arena/media`, one frame held still at 320x180, a full shake, read
1.5 s later against the same clip with no shake; the snow found on an opaque black card, since
the snow's state never depends on the clip):

| Count | the snow covers (mean over the globe) | its contrast over the clip, median of 33 clips | worst clip | clips under 0.1 |
| --- | --- | --- | --- | --- |
| 150 | 1.29 % | 0.722 | 0.474 | 0 |
| **600 (default)** | **5.28 %** | **0.752** | **0.455** | **0** |
| 2400 | 20.24 % | 0.800 | 0.450 | 0 |

Contrast is the luminance step (0..1) the snow adds on pixels it covers at least half. White
snow stays visible on every demo clip at every count tried; at the default it covers about a
twentieth of the globe, leaving the clip readable. That is a reading of the choice, not a
derivation of it.

## Cost

`sgtest --bench` in the final verify.sh run of 2026-11-17 (23:23–23:32) (this Mac's GPU, Apple Silicon, shared with
other builds; load average about 15–20 throughout, so these wander between runs). Each frame
timed alone: glFinish before and after, GL_TIME_ELAPSED for the GPU, the thread's CPU clock
inside ProcessOpenGL, and wall time; median and worst of 120 frames after 240 of warm-up;
every timed frame checked to have rendered (0 did not).

| case | raster | GPU median | GPU worst | CPU median | wall median |
| --- | --- | --- | --- | --- | --- |
| defaults (600 flakes), kicks from the music | 720p | 0.342 ms (2.1% of a 60 fps frame) | 1.761 ms | 1.162 ms | 1.645 ms |
| defaults (600 flakes), kicks from the music | 1080p | 0.678 ms (4.1%) | 0.885 ms | 1.162 ms | 2.111 ms |
| 3000 flakes (the cap), 1.25 mm, Globe 1.0, a full shake each second | 720p | 1.286 ms (7.7%) | 1.629 ms | 2.159 ms | 3.738 ms |
| 3000 flakes (the cap), 1.25 mm, Globe 1.0, a full shake each second | 1080p | 1.276 ms (7.7%) | 2.882 ms | 2.181 ms | 3.855 ms |

The CPU side (the physics, the flakes' projections and the instance upload) is about 1.16 ms
at the defaults and 2.2 ms at the cap, whatever the raster. The cap's GPU median at 1080p came
out no higher than at 720p in this run and the one before it (1.272 and 1.248 ms; earlier the
same day, 1.228 and 0.925 ms), which is the machine's load as much as the plugin: read the
cap's GPU cost as 1–1.3 ms. Not measured: 4K
(the brief says no 4K loops on this shared Mac).

## The traps actually hit

- **The fall check measured nothing in front of the card.** The harness's inverse trace
  (`harnessXi`) bisected over the whole angular range, and for a point at z > 0 the grazing ray
  enters behind it, so the bracket failed. It now bisects for the largest angle whose ray
  still reaches z first.
- **Hats fired kicks.** earshot's band-0 detector rose on the hats' low splatter once the kicks
  stopped, so `--still` never saw a still globe: the dominance gate above.
- **The swirl keeps decaying after every flake has landed** (until |W| < 1e-9 rad/s), so a
  bit-exact "state unchanged" over the whole globe failed while no flake moved. `--still`
  compares every flake's state and every output float; the swirl is the fluid, not the snow.
- **A centroid finder has a bias of its own.** Grid crossings found in the plugin's picture
  were 0.15–0.3 px from the trace's analytic crossings, the finder's bias on a line drawn
  through refraction, not the plugin's error. `--grid` now renders the harness's own traced
  picture and runs the same finder on both: they agree to 1.2e-4 px, and the finder's bias is
  reported beside it.
- **Asking for 100 grid crossings** inside 0.8 of the silhouette when the grid has 69 there.
- **Flake areas 10.7 % off at 320x180.** Point-sampling the ramp on the pixel lattice, and an
  area correction that is exact only for circles: now the 4×4 box rule and the ramp's integral
  for any ellipse (`tools/coverage.py`: worst 0.84 %, 0.032 px over 6000 ellipses). Beads use
  the full 2×3 Jacobian, not one in-plane pair.
- **Flakes overhung the silhouette**: 55 pixels outside the globe not clear. Subsamples past
  the silhouette are now clipped.
- **Blending rounded output floats a hair outside 0..1** (`--robust`): the composite clamps.
- **GPA Table 4 is printed every 10 %**: rows at 65 % and 75 % had been put in the harness's
  density table from memory. Removed; the viscosity table (Table 17) keeps its six printed rows.
- **A quadratic fit hid the spin.** The full shake's spin measured +1.1 % with a quadratic over
  0.2 s: the tracer turns 0.33 rad on its circle, and the cubic term leaks (ωT)²/10 into the
  slope. A cubic fit over 0.1 s at 240 fps gives 2.0001 rad/s.
- **A tolerance that grew when the measurement was wrong.** The fall and swirl tolerances
  divided a fit's residual by the *measured* span: a swirl that never decayed got a 5300 %
  tolerance and the mutant "decay ν, not νk²" passed. Both are now three standard errors of
  the slope relative to the *law's* value.
- **A check read the plugin's own figure.** `--alpha` placed the silhouette at the plugin's
  globePx, so a mutant making the globe a fifth larger passed. It now takes 0.45 H from the
  spec and also measures the radius off the alpha (324.012 px against 324).
- **A citation from memory in the harness.** `--model` said "Hecht's paraxial n"; Hecht was
  never reached. Now OpenStax eq. 2.11.
- **Kick never fired on Resolume's FFT** in v0.1.0, and nothing offline could show it: every
  kick check fed earshot the waveform through `feed()`, and `--silent` fed the host path
  nothing. The host path's layout was never checked against a host. `--hostfft` now feeds
  bins built the way Arena was measured to build them.
- **A truncated sample index breaks a high-pass.** `--hostfft`'s hiss is a second difference
  of hashed noise; indexing it by `(long)(t * rate)` repeats and skips samples at 60 fps, and
  the repeats put energy back below 375 Hz (a stray shake at 5.167 s on claps and hats alone).
  `llround(t * rate)` fixed it. `musicAt`'s own index still truncates; nothing it feeds
  depends on a high-pass.
- **On arm64, long double is double.** The harness's trace "in long double" is double here; its
  tolerances are written for double.
- The machine's: heredocs hang without TMPDIR (verify.sh refuses to run without one); no
  `timeout` (perl alarm); `head`, `grep` and `cc` shadowed (`/usr/bin/head`, `command grep`);
  ffmpeg needs `-nostdin`; the load average sat near 20 for most of the build, so the bench
  numbers wander.

## Verified, and assumed

Verified, by `tools/verify.sh` on 17 November 2026 and again for v0.1.1 on 19 November: every claim in README's Status, at
1280x720 and 320x180 on this Mac's GPU, every picture check again on Apple's software renderer
at 320x180; the CPU checks with no GL context; the alpha check on Arena's DXV clips; the
negative controls and mutants; the sweep, and `--bare`; the bundle universal, signed ad hoc,
read and rendered by oxbow.

Assumed: Stokes drag (Re → 0) up to Re 0.63 at the worst corner, where [Mercier]'s correlation,
extrapolated below its range, puts the true drag ~10 % higher; discs that keep their
orientation; the slowest mode only, linear; the shake (Ω_max, the lift); the geometry (R, D,
the card at the centre); 20 °C; n at the sodium D line; Resolume's FFT onsets usable
(earshot's host fallback is quantised to Arena's ~43 ms updates and the frame, and
uncalibrated); `--hostfft`'s model of Arena's grouping (16 fine bins of a 2048-point Hann FFT
summed) is inferred from the measurement, not seen; the law measured on one Mac, in Arena
7.29.0 only (Windows, Avenue and other versions not measured).

Not done: Kick from a live device; Kick on Resolume's FFT on Windows (win-lab has no sound
device); presets, an OpenFX port; a 4K bench.

## Would this hold on another rasteriser, at another raster?

The scene shader reads the clip by texelFetch with the bilinear done by hand, traces with
algebra and sqrt only, and averages four rays a pixel; flakes are quads whose coverage is
computed in the fragment shader, not by the rasteriser's coverage. Positions are measured by
alpha centroids and converted through the harness's own trace, so tolerances come from the
fit or the float trace, not from a pixel count. One line per check (software = Apple's
renderer at 320x180, run by verify.sh):

- `--fall`: Cheng's 3.5 % + Volk's 0.07 % + three standard errors of the fit, relative to the
  law. Positions are alpha centroids through the harness's trace. Software: passes, the same
  speeds to four figures (−0.84 %, −2.30 %), bead slope 1.9980 (tolerance 0.0036).
- `--settle`: Cheng's 3.5 % + two frames. Software: passes, 25.429 s.
- `--swirl`: decay as `--fall`; the ratio three standard errors of the windows; the full
  shake three standard errors of two cubic fits. Software: passes (2.0031 rad/s, +0.155 %
  against 0.873 %).
- `--still`: bit-exact, a CPU state and output floats compared within one run on one renderer.
  Software: passes (twice, both times under a load average near 20, though the brief warns the
  software renderer is not always bit-identical under load).
- `--grid`: the shader's float trace against the harness's double trace through the same
  finder, 1e-3 px (a few ulp of float times the card's pixels per R). Software: passes, worst
  3.3e-5 px; the GPU at 320x180 3.2e-5 px.
- `--magnify`: 0.2 px over the measured separation (the line finder's sub-pixel reach), against
  the trace at that separation. Software: passes, the GPU's numbers to five figures.
- `--flakes`: the box rule's worst error over 6000 ellipses (`tools/coverage.py`), which does
  not depend on the rasteriser because coverage is computed in the fragment shader. Software:
  passes, 0.0043 px and 0.16 %, as the GPU.
- `--rate`, `--clock`, `--resize`: bit-exact within one renderer. Software: pass.
- `--alpha`: bit-exact through texelFetch; the silhouette's radius within 1 px (the edge band).
  Software: passes, 81.010 px.
- `--triggers`: event times on the CPU in double, read from the plugin's log: no rasteriser.
  Software: passes.
- `--glstate`: GL queries. Software: passes.
- `--robust`: finite, in range, inside the glass, at 320x180 and 97x61. Software: passes.
- `--coreaudio`: a process-level interposer (DYLD_INSERT_LIBRARIES): macOS only; GPU run only.
- `--model`, `--names`, `--cues`: no GL; run by `--offline`.

## The negative controls and mutants

`sgtest --negative`, 30 wrong models, each must fail its check; a check with none fails:

| check | wrong model |
| --- | --- |
| `--fall` | discs fall at their equal-volume sphere's Stokes speed |
| `--fall` | settling speeds from water's viscosity, whatever the fill |
| `--settle` | settling speeds from water's viscosity, whatever the fill |
| `--swirl` | the swirl decays with k = π/R, not x1/R |
| `--still` | resting flakes slide down the glass |
| `--still` | a kick from any band: the hats shake it |
| `--still` | a band-0 onset a kick whatever the other bands carry |
| `--grid` | the scene not refracted at the glass |
| `--grid` | the scene refracted with water's n, whatever the fill |
| `--magnify` | the scene refracted with water's n, whatever the fill |
| `--flakes` | flakes projected without the glass's refraction |
| `--flakes` | flakes drawn with hard edges, no coverage |
| `--rate` | beats and placed shakes at the frame, not inside it |
| `--clock` | the host's clock read through a float |
| `--resize` | a resize starts the globe again |
| `--alpha` | the clip sampled straight, not premultiplied |
| `--triggers` | beats and kicks placed at the frame, not inside it |
| `--triggers` | a kick from any band, not the lowest |
| `--triggers` | a band-0 onset a kick whatever the other bands carry |
| `--hostfft` | Resolume's bins filed whole under the band holding each one's centre (earshot `8124191`'s rule): band 0 gets none |
| `--hostfft` | the host's sample rate never reaches earshot |
| `--hostfft` | a kick from any band, not the lowest |
| `--coreaudio` | the device menu listed and Audio Input applied whatever Trigger On says |
| `--glstate` | the composite leaves its program and textures bound |
| `--robust` | presses between two frames collapse to one |
| `--model` | the fill's viscosity at 25 °C, not 20 °C |
| `--model` | water's index, whatever the fill |
| `--model` | the swirl mode held to k = π/R |
| `--names` | a seventeen-character name added |
| `--cues` | every control ramped between keys |

`tools/mutate.sh`, 46 one-character mutants of the shipped code (21 in the GLSL), each caught.
One arm64 tree; before each mutant the object and the sgtest binary are deleted, and the
mutant is proved in the build (the object differs from the unmutated one; a GLSL mutant's text
is in the binary); every named check must pass unmutated first; 320x180; perl alarm 300 s (a
timeout counts as caught, and is said; none timed out):

| file | mutant | caught by |
| --- | --- | --- |
| Shaders.cpp (GLSL) | the silhouette's discriminant 1 + D^2 x^2 / L^2, - -> + | `--grid` |
| Shaders.cpp (GLSL) | the ray meeting the glass where it leaves, not where it enters, - -> + | `--grid` |
| Shaders.cpp (GLSL) | the entry point's depth with the root subtracted, + -> - | `--grid` |
| Shaders.cpp (GLSL) | the index ratio 2 / n, 1 -> 2 | `--magnify` |
| Shaders.cpp (GLSL) | Snell's cosine term 1 + eta^2 (1 - c^2), - -> + | `--grid` |
| Shaders.cpp (GLSL) | the refracted ray's normal part with the wrong sign, - -> + | `--grid` |
| Shaders.cpp (GLSL) | the ray walked away from the card, + -> - | `--grid` |
| Shaders.cpp (GLSL) | the card's width read as 3 aspect, 2 -> 3 | `--grid` |
| Shaders.cpp (GLSL) | the card read a texel off, - -> + | `--grid` |
| Shaders.cpp (GLSL) | the card's upper row weighted by y, x -> y | `--grid` |
| Shaders.cpp (GLSL) | the card premultiplied by adding its alpha, * -> + | `--alpha` |
| Shaders.cpp (GLSL) | the composite's clip premultiplied by adding its alpha, * -> + | `--alpha` |
| Shaders.cpp (GLSL) | Mix 1 handing the clip through untouched, 0 -> 1 | `--alpha` |
| Shaders.cpp (GLSL) | one of the four subsamples half a pixel out, 2 -> 7 | `--grid` |
| Shaders.cpp (GLSL) | the centre of the raster off by a tenth of it, 5 -> 6 | `--grid` |
| Shaders.cpp (GLSL) | the flake's edge ramp a pixel outside its edge, 0 -> 1 | `--flakes` |
| Shaders.cpp (GLSL) | the box rule's sixteen subsamples divided by 15, 6 -> 5 | `--flakes` |
| Shaders.cpp (GLSL) | the box rule's subsamples a quarter pixel left, 1 -> 2 | `--flakes` |
| Shaders.cpp (GLSL) | flakes drawn only outside the glass, < -> > | `--flakes` |
| Shaders.cpp (GLSL) | the flake's quad cut inside its ellipse, + -> - | `--flakes` |
| Shaders.cpp (GLSL) | flakes placed for a raster a fifth larger, 5 -> 6 | `--flakes` |
| Globe.cpp | a disc's broadside mobility 1/17, 6 -> 7 | `--fall` |
| Globe.cpp | a disc's edgewise mobility 3/22, 3 -> 2 | `--fall` |
| Globe.cpp | Stokes's law over 8, 9 -> 8 | `--fall` |
| Globe.cpp | the swirl's decay nu, not nu k^2, * -> / | `--swirl` |
| Globe.cpp | the swirl's profile with j1's sign flipped, - -> + | `--swirl` |
| Globe.cpp | a full shake spinning the fill at 1/2 rad/s, not 2, * -> / | `--swirl` |
| Globe.cpp | flakes landing a radius outside the glass, - -> + | `--robust` |
| Globe.cpp | a shake pushing flakes down into the glass, + -> - | `--settle` |
| Globe.cpp | RK4's step weighted 1/5, 6 -> 5 | `--fall` |
| Fill.cpp | glycerol's density 100 kg/m^3 high, 2 -> 3 | `--model` |
| Fill.cpp | Hoyt's slope 0.00148, 9 -> 8 | `--model` |
| Fill.cpp | Cheng's a 0.805, 7 -> 8 | `--model` |
| Optics.cpp | the flakes' index ratio 2 / n, 1 -> 2 | `--flakes` |
| Optics.cpp | the flakes' ray meeting the glass where it leaves, - -> + | `--flakes` |
| Optics.cpp | the silhouette's tangent 1 / sqrt(D^2 + 1), - -> + | `--grid` |
| Snowglobe.cpp | the clock in tenths of a millisecond, 6 -> 5 | `--triggers` |
| Snowglobe.cpp | a beat placed in the frame by boundary + p0, - -> + | `--triggers` |
| Snowglobe.cpp | kicks kept only inside the merge window, > -> < | `--triggers` |
| Snowglobe.cpp | a button that can never be pressed, 0 -> 1 | `--triggers` |
| Snowglobe.cpp | a kick only when band 0 is quiet, > -> < | `--triggers` |
| Snowglobe.cpp | the frame's step the sum of two clock readings, - -> + | `--triggers` |
| Snowglobe.cpp | the audio device opened when Trigger On is not Kick, = -> ! | `--coreaudio` |
| Snowglobe.cpp | the ramp's integral term over 34, 2 -> 3 | `--flakes` |
| Snowglobe.cpp | the globe a fifth larger than Globe says, 5 -> 6 | `--alpha` |
| Audio.cpp | SNOWGLOBE_NO_DEVICES=1 read as off, 0 -> 1 | `--coreaudio` |

## The sweep's honest list

`tools/sweep.py` renders every control at both ends, 240 frames at 320x180 on the defaults
with the harness's music: 10 swept, 0 dead, 0 barely alive (mean 8-bit deltas from 1.88 for
Size to 102.9 for Mix). With `--bare` (the context table emptied) nothing goes dead either:
the one context, Trigger On = Button for Shake Now, only isolates the press, and under Kick the
press still adds a shake to the music's. Audio Input is not swept (it acts on devices;
`--coreaudio` covers its gate), nor Audio (the host's FFT buffer) or the About block.

## Browser demo

https://snowglobe-demo.stoatworks-labs.com/ (2026-11-18), from `demo/`, deployed by
`wrangler.toml` (a Worker route: the zone is at Cloudflare's 100 custom domains, so
`snowglobe-demo` is a proxied AAAA `100::` record made through the API) and
`.github/workflows/deploy.yml` on every push to main. woofer's pattern, which is mirrorball's.

- **It runs the plugin, it doesn't port it.** `demo/snowglobe-core.wasm` is `Snowglobe.cpp` and
  everything it calls (Globe, Fill, Optics, Audio, Controls, PassBuffer, Shaders, Diag),
  earshot's snapshot (every `src/*.cpp` but `Miniaudio.cpp`/`MiniaudioImpl.cpp`, with
  `NoCapture.cpp`: its own `EARSHOT_CAPTURE=OFF` build, so the module has no device backend at
  all) and the FFGL SDK's plugin classes, FFGLLog, FFGLShader, FFGLFBO, the scoped bindings and
  FFGLUtilities, compiled unmodified by emscripten (`demo/tools/build-wasm.sh`). Left out:
  `EffectPlugin.cpp`. The script fails if a `source/*.cpp` or an earshot `src/*.cpp` it
  doesn't list appears. No change to `source/` was needed for WebGL2 or emscripten.
- **What is the page's**: `demo/wasm/glue.cpp` plays the host (constructs the plugin, reads its
  declarations back through the SDK so the panel is built from them, forwards
  SetFloatParameter, SetBeatInfo, SetTime and ProcessOpenGL as sgtest's Rig does, and hands the
  page the harness's accessors: LastFrame(), ShakeLog(), GlobeForTest(), CurrentPhysics(),
  earshot's frame); `demo/wasm/gl_shim.cpp` takes glShaderSource (refusing any text that isn't
  byte for byte the page's assembly of `demo/shaders.js`, then the kit's `port()`) and
  glEnable/glDisable/glIsEnabled (GL_PROGRAM_POINT_SIZE, which WebGL2 lacks); the host row
  (audio, tempo, a Shake Now button); the read-outs' units, tooltips, the nine presets (in
  glycerol fractions, metres and counts, through the plugin's own inverses via
  `sg_param_from`, Size's with the preset's Snow), the lines under the picture and Diag's log
  from emscripten's memory file system (`SNOWGLOBE_LOG_DIR=/snowglobe/logs`, made by
  `sg_prepare`, since Diag's own `mkdir -p` goes through `system()`). Off the panel: Audio
  Input, the Audio buffer and the About block (the panel's note says so).
- **The audio, never a device.** The page never calls a device API (`check_shaders.py` fails if
  a served file names getUserMedia, enumerateDevices or mediaDevices), the module cannot open
  one, and `sg_prepare` sets `SNOWGLOBE_NO_DEVICES=1`. The host row's choices: sgtest's track
  (`musicAt` and `kRate` copied from `tools/sgtest/main.cpp` into the glue, and the Rig's
  Render lines that feed it, from `const int from` to `fedUntil = t;`, held by
  `check_shaders.py`); nothing (`--silent`: Resolume's FFT on a buffer nobody writes, so Kick
  never fires); and a file of the visitor's, decoded by `decodeAudioData` in an
  `OfflineAudioContext` at 48 kHz (no output device), its first two channels averaged as
  `readWav` does, fed L = R through `sg_feed` and looped (`sgtest --wav` plays once, at the
  file's rate). Nothing is played aloud. Every audio change is a new instance from time 0, and
  the page's default is the track where the plugin's own is Resolume's FFT.
- **The beat and the button.** SetBeatInfo goes every frame with the host row's tempo (120 BPM
  at first) and a bar phase on the page's clock, `fmod( t * bpm / 240, 1 )` from time 0 at a
  fixed tempo (sgtest `--beat`'s phase), continuous across a tempo change and still while
  paused. Shake Now (FF_TYPE_EVENT) is a kit boolean the page lets go of after one frame
  (filings' rule): one rising edge.
- **The float target.** The scene pass blends into RGBA32F, which WebGL2 does only with
  EXT_color_buffer_float (the kit's `needFloat`) and EXT_float_blend (checked by the page,
  which stops with a message rather than show another picture).
- **The clock.** Seconds from frame deltas (capped at 0.1 s). Each new instance (load, a change
  of audio, Restart) is primed with five frames about a millisecond apart so the
  seconds-or-milliseconds vote settles before the first frame shown; nothing is fed and the
  phase stands still during them. The plugin steps on its own wall clock until the vote
  settles, so its clock then runs 6 to 12 ms ahead of the page's (the wall decides).
- **Checked by `tools/verify.sh`** ("Browser demo's copies", `demo/tools/check_shaders.py`):
  the six GLSL pieces and kVersion character for character and the order
  AssembleVertex()/AssembleFragment() join them for all three programs (read from Shaders.cpp
  and InitGL's stage table); all 21 uniforms set by the setter their type takes (the two
  `uniform()` overloads typed by their argument) and nothing undeclared set; sgtest's track and
  feed lines in the glue; no served file naming a device API; the hashes of every input the
  .wasm was built from (`demo/wasm/inputs.sha256`, 68 files) and the SDK pin. Any change under
  `source/` (a regenerated `StoatworksAbout.h` included) or `external/earshot/` needs
  `demo/tools/build-wasm.sh` rerun and its outputs committed (mode 644).
- **Compared with sgtest, once (2026-11-18), by `demo/tools/crosscheck.py`** (in the repo;
  nothing runs it by itself: it needs Chrome, cdpshot and a built sgtest). Headless Chrome on
  the real GPU (ANGLE on Metal), the page paused, the settings applied, Restart, then N presses
  of Step (the clock at N/60, the track fed to N/60); Mix 0 hands the clip back so the canvas is
  the exact input frame; `sgtest --pipe --fps 60 --beat 120` gets that frame N + 1 times with
  the same host values, and its frame N is compared, as premultiplied colour and alpha. Apple
  M4 Max, 640×360 (921,600 values a case: colour and alpha; "off" is by more than half a code),
  the last of three runs (the offset, and with it the numbers, move from run to run: the
  defaults' mean was 6.7e-3, 8.1e-3 and 1.3e-2):

  | case | clip | N | mean colour diff (codes) | values off | max | shakes (page) |
  | --- | --- | --- | --- | --- | --- | --- |
  | defaults, the track | scene | 360 | 1.3e-2 | 5,715 | 12 | 11 |
  | Trigger On Beat at 120 BPM | grid | 240 | 9.1e-3 | 3,671 | 17 | 8 |
  | beads 0.2 mm in 75 % glycerol | bars | 300 | 3.2e-5 | 22 | 1 | 9 |
  | 3000 flakes, Shake 0.8, Globe 0.6 | detail | 200 | 4.4e-3 | 2,349 | 8 | 6 |
  | Mix 50 % | spot | 180 | 2.0e-3 | 968 | 8 | 5 |
  | the shape on transparency | alpha | 240 | 8.4e-3 | 4,090 | 19 | 7 |
  | nothing fed (Kick never fires) | ramp | 120 | 3.0e-5 | 23 | 1 | 0 |
  | Button, Shake Now after step 30 | scene | 150 | 2.6e-3 | 936 | 15 | 1 |

  Not exact, and why: sgtest presets the plugin's clock, the page lets it vote, so the page's
  instance runs some milliseconds ahead and the 8 ms RK4 grid falls elsewhere between the same
  shakes. `Globe::step` lands a flake at the end of the grid step it crossed the glass in
  (`Globe::land`, along the radius), so a landed flake lies up to one step's fall from sgtest's,
  a few hundredths of a pixel, and the next shake lifts it from there. Hence the few values off
  at flake edges, growing with the snow that has landed, and the beads (slower, smaller)
  agreeing to a code. The negative controls (Shake one slider step off; no press) differ by 1.03
  and 7.1 codes on average, 80 and 2,700 times their cases'. The script fails a case past a
  mean of 0.02 codes or 1 % of values off. A Resolume host's vote moves the
  grid the same way.
- **Not checked on the page**: the vendored kit (`demo/vendor/`, stoatworks-backend's
  `resolume-demo/sync.sh`, never edited here); the read-outs' units and the presets beyond
  their inverses; any browser but headless Chrome on this Mac.

## Open questions

1. **A shake lifts only resting flakes.** Under kicks the snow stays in the lower part of the
   globe: each kick lifts what has landed by up to 2R·Shake, and a later full shake (the
   button) lifts nothing that is still falling, so the button after a run of kicks looks
   weaker than the button on a settled globe. A shake that also lifted suspended flakes (or
   redistributed them) would read more like a hand; it is a new choice, not in any source.
2. **Settling takes tens of seconds** (25 s for a lift-only shake at the defaults, 33 s for the
   largest disc to fall the whole globe at w = 0.60), as real globes do. A faster flurry needs
   a thinner fill and leaves Stokes, so the model would need a drag law above Re 1; or a
   "time scale" control that is honest about being one.
3. **Outside the globe is transparent.** An option to keep the clip around the globe (or a
   backdrop) was left out.
4. **The kick gate's 0.25** is chosen against the harness's own music. *Closed in v0.1.1:*
   Kick on Resolume's FFT never fired in v0.1.0 (Arena 7.29.0, 2026-11-19) because earshot
   `8124191` gave bin 0 (centred at 172 Hz) to the band above the kick band. Resolume's bins
   were measured (below), earshot `02306da` shares each bin by overlap, and v0.1.1 fires in
   Arena: 69 of 69 pairs with `Beat 001.wav`. Still open: on the host path Kick is 0–375 Hz,
   so a bass line shakes the globe too (40 of 40 pairs with `Bass 001.wav`). A gate against
   that (a kick is band 0 rising while bins 1–2 don't, say) would be a new choice, not
   measured.
5. **Ω_max = 2 rad/s and the lift height 2R·S** are chosen; no source models a hand's shake.
6. **No base, no figurine, no glass, no Fresnel**: the outer 9 % of the card is never seen, and
   the last pixels of the globe hold a squeezed ring with no reflection.
7. **A flake that grows while resting pokes past the glass** until the next shake: settle it
   again on a Size or Range change, or leave it.
