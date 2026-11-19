# snowglobe

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. **It has run in Resolume only briefly,
> on one layer over a still, in Arena 7.29.0 on a Mac and 7.27.1 on Windows, and it has never
> opened a live audio device. In Arena on the Mac its default trigger, Kick, shakes it on
> Resolume's own FFT from v0.1.1 (in v0.1.0 it never fired), and Beat and the Shake Now button
> shake it too. Resolume's lowest FFT bin holds everything below 375 Hz, so on Resolume's FFT a
> bass line shakes it as well as a kick (see [In Resolume](#in-resolume)).** What's established is established
> by an offline harness, `sgtest`, that drives the real plugin class through the same
> `ProcessOpenGL` a host calls, in a headless GL context on a synthetic clock, on this Mac's GPU
> at 1280x720 and 320x180 and on Apple's software renderer, and feeds it audio through the same
> earshot `feed()` path a device would use. Read back from the rendered picture: single flakes
> fall at the speed the Glycerine Producers' Association's tables and the low-Reynolds-number
> drag of a disc (or Stokes's law for a bead) give them, to **−0.84 %** and **−2.30 %** at two
> fills against a tolerance of 3.57 % (Cheng's own 3.5 % plus the fit's); speed goes as size
> for discs cut from one film and as size squared for beads (log-log slopes **1.0000** and
> **2.0000**); the swirl a shake leaves dies away at the rate the sphere's slowest viscous mode
> gives, to **+1.1 %** and **+2.7 %**; the clip seen through the glass lands where an
> independent ray trace puts it to **1.2e-4 px**, magnified at the centre by the fill's
> refractive index to **0.15 %**; a lift-only shake settles in **25.43 s** against 25.20 s
> derived from the tables. Once the snow has landed, **300 frames over music with no kick are
> bit-identical**, and 300 more with eight controls moving change no flake by a bit. 24, 60 and
> 144 fps give **the same flurry, bit for bit**. **30** deliberately wrong models and **46** one-character mutants of the shipped
> code are all caught. The globe's size, the shake's strength and the defaults are **chosen**:
> see [Status](#status).

A snow globe for [Resolume](https://resolume.com) Arena and Avenue, as an FFGL effect,
**SW Snowglobe**: your clip is the scene standing inside a glass globe of water and glycerine,
and the music shakes it.

![A glass globe in the middle of a transparent frame, filled with the harness's colour card:
a rainbow of hues sweeping left to right in horizontal bands, dark navy squares in a chequer,
and a white bar across the middle, all bent by the glass so the bands curve and squeeze
toward the rim. White flakes, ellipses of many sizes and tilts, some edge-on as thin slivers,
are scattered through the whole globe after a shake](docs/hero.png)

<sub>One frame rendered by `sgtest`, not captured from Resolume: SW Snowglobe on the harness's
colour card at the defaults, 3.5 s after one press of Shake Now (`--frames 240 --set
"Trigger On=Button" --at 30 "Shake Now=1"`). Outside the globe the frame is transparent.</sub>

<!-- downloads:start -->

## Download

**[v0.1.1](https://github.com/stoatworks-labs/snowglobe/releases/tag/v0.1.1)** — prebuilt for macOS and Windows. Pick your platform:

<details>
<summary><b>macOS</b> — Universal (Apple Silicon + Intel)</summary>

| Build | Download | Size |
| --- | --- | --- |
| Universal (Apple Silicon + Intel) · .dmg disk image | [`snowglobe-0.1.1-macos-universal.dmg`](https://github.com/stoatworks-labs/snowglobe/releases/download/v0.1.1/snowglobe-0.1.1-macos-universal.dmg) | 603 KB |
| Universal (Apple Silicon + Intel) · .zip archive | [`snowglobe-macos-universal.zip`](https://github.com/stoatworks-labs/snowglobe/releases/latest/download/snowglobe-macos-universal.zip) | 537 KB |

</details>

<details>
<summary><b>Windows</b> — x64</summary>

| Build | Download | Size |
| --- | --- | --- |
| x64 · .exe installer | [`snowglobe-0.1.1-windows-x86_64-setup.exe`](https://github.com/stoatworks-labs/snowglobe/releases/download/v0.1.1/snowglobe-0.1.1-windows-x86_64-setup.exe) | 363 KB |
| x64 · .zip archive | [`snowglobe-windows-x86_64.zip`](https://github.com/stoatworks-labs/snowglobe/releases/latest/download/snowglobe-windows-x86_64.zip) | 251 KB |

</details>

All builds, checksums and release notes: [github.com/stoatworks-labs/snowglobe/releases](https://github.com/stoatworks-labs/snowglobe/releases).

macOS builds are signed and notarised and open normally. The Windows builds are unsigned, so SmartScreen warns once.

<!-- downloads:end -->

## The one idea

**A snow globe is a slow, viscous, settling flow seen through one curved surface.** The flakes
are tiny and the fill is thick, so nothing has inertia worth the name: each flake moves with
the fluid plus its own terminal speed, which low-Reynolds-number drag fixes. A flat flake
falls at its weight over 16μa broadside (Katz, after Happel and Brenner), so flakes cut from
one film fall in proportion to their radius; a bead falls by Stokes's law, as its radius
squared. A shake sets the fill turning in the sphere's slowest mode, a Bessel-function swirl
that dies away as e^(−νk²t). And the clip, standing at the centre, is seen through the surface
between fill and air: an object at the centre of a fish bowl appears at the centre, magnified
by the water's refractive index.

The fill is a real water-glycerol mixture: Fill picks the glycerol fraction, from 60 % to 80 %
by mass, and with it the viscosity, the density and the refractive index from the published
tables. So Fill changes how fast the snow falls, how long the swirl lasts, and, slightly, how
much the glass magnifies the clip.

## What falls out of it

- **A flurry on the kick that takes seconds to settle.** A kick lifts the snow on the floor
  and stirs the fill; the snow sinks back over tens of seconds at the default fill, as real
  globes do.
- **Big flakes fall faster.** Twice the radius, twice the speed for flakes; four times for
  beads.
- **Flakes tumble and glide.** Each launched flake has its own fixed tilt, so it glides
  sideways as it falls (a fifth of its fall at 45°) and shows edge-on as a sliver.
- **The middle magnified, the rim squeezed.** About 1.41× at the centre, and the outer tenth of
  the card is never seen at all.
- **Thicker fill, slower snow.** In the model, at 80 % glycerol the same flake falls about
  seven times slower than at 60 % and the swirl dies about five times faster (derived from the
  tables; the harness measures 60 % and 70 %).
- **Nothing moves without a shake.** Snow at rest stays exactly where it is, frame after frame.

## Triggers

- **Kick** (the default): kick drums, from [earshot](https://github.com/stoatworks-labs/earshot)'s lowest band (40–160 Hz). Resolume's FFT by
  default, or a capture device. Kicks within 50 ms are one shake. On Resolume's FFT the lowest
  bin runs from 0 to 375 Hz, so a bass note can shake it as well as a kick. It never fired on
  Resolume's FFT in v0.1.0; it does from v0.1.1 (see [In Resolume](#in-resolume)).
- **Beat**: each quarter of Resolume's bar, placed inside the frame.
- **Button**: Shake Now only. Shake Now is a full shake, and works under every source.

## Controls

| Control | What it does |
| --- | --- |
| Shake | how hard a kick or beat shakes the globe, 0–1 (0.3) |
| Trigger On | Kick (default), Beat, Button |
| Audio Input | Resolume FFT (default), System default, a device by name |
| Shake Now | one full shake |
| Fill | glycerol 60–80 % by mass (60 %): viscosity 10.9–60.9 mPa·s, n 1.413–1.443 |
| Snow | Flakes (PET discs, 48 µm thick) or Beads (PET spheres) |
| Size | the largest flake: radius 0.3–1.25 mm, or 0.08–0.3 mm for beads (1.0 mm) |
| Range | the smallest flake as a fraction of the largest, 0.25–1 (0.5) |
| Count | 50–3000 flakes (600) |
| Globe | the globe's diameter as a fraction of the frame's height, 0.25–1 (0.9) |
| Mix | the globe over the clip, premultiplied (1) |

**Alpha.** Outside the globe the output is transparent, so the globe stands over the layers
below. The clip is premultiplied before the glass resamples it, so nothing under a
transparent pixel ever shows, and where the clip is transparent you see through the globe (and
the snow behind the card). Mix 0 hands the clip through bit for bit.

## Status

**v0.1.1, 19 November 2026: Kick fires on Resolume's FFT** (in v0.1.0, released on 18
November, it never did). **Run in Resolume only briefly, over a still, in Arena on Windows and
on a Mac, and no live audio device has ever been opened.**
No OpenFX port; a [browser demo](#browser-demo) and a [user guide](https://stoatworks-labs.com/software/snowglobe/guide/).

> **Photosensitivity.** At the defaults it added no flash to any of Resolume's 33 demo clips.
> **With Shake and Count both at full**, the snow can fill the globe and blank the clip on every
> kick or beat: on one dark clip (Cyberspace 09) that crossed three flashes a second over a third
> of the picture, 7 to 8 rises and falls in its worst second (the line is 6), with every music
> tried. The [user guide](https://stoatworks-labs.com/software/snowglobe/guide/#flashing) has the table. Check your settings before you
> put this in front of an audience; outside the globe is transparent, so the layer you put behind
> it matters too.

### In Resolume

- **Windows: Arena 7.27.1 on win-lab** (Mesa llvmpipe, no GPU, no sound device), with the x64 DLL
  the release workflow built from this source: for v0.1.0 once from a build before the tag and
  again from the release's own zip (the two DLLs differ by hash), and for v0.1.1 from the
  release's own zip (19 November 2026): the fleet's Arena gate passed 9 checks, failed none and
  skipped 2, all three times. It loads, registers as `SW Snowglobe` / `SG01`, an effect, shows all 18 controls
  (Opacity and the 17 declared) with the declared name, order, type, range and default, renders,
  and 10 controls move the picture: with no sound the snow lies still on the glass, where Fill,
  Snow, Size, Range, Count, Globe and Mix act, and Shake acts under Trigger On at Beat, shaking on
  Arena's clock. Audio Input and the Audio buffer are skipped there (no sound device), and Shake
  Now is recorded inert (the gate presses no buttons). Arena's log was clean and it survived the
  run. The MSVC build of snowglobe and of earshot has no warnings, and `tools/wincheck.ps1` loads
  the DLL as a host does and reads back its 17 parameters.
- **macOS: Arena 7.29.0 on an Apple Silicon Mac** (19 November 2026), with the v0.1.0 release's
  own universal bundle from its zip, mounted over Arena's REST API on an opaque card in a new
  composition and read off the composition monitor in snapshots about a fifth of a second
  apart. It registers as `SW Snowglobe` / `SG01`, an effect, and shows all 18 controls (Opacity
  and the 17 declared) with the declared name, order, type, range and default; the About line
  reads "Snowglobe v0.1.0 - MIT - Stoatworks Labs, stoatworks-labs.com". **At rest it holds
  still**: two snapshots 2 s apart were identical, and Mix 0 was identical to the bare card.
  **Each control acts**: Fill, Snow, Size, Range, Count, Globe and Mix each changed the picture
  (by 2.4 to 90.9 levels on average) and, set back, gave the first picture again, identical.
  **Beat**, on Arena's clock: Shake 0 moved nothing (0 of 24 snapshot pairs), the default 0.3
  shook it in 23 of 24 and 1.0 in 24 of 24. **Button**: once the snow had settled, nothing moved
  without a press, and one Shake Now press over REST (held 0.3 s) changed 24 of 24 pairs. The
  plugin listed 12 audio inputs on its first frame under Kick and opened none; no microphone
  prompt appeared, and Arena's composition files were left as they were.
- **Kick on Resolume's FFT, v0.1.1** (Arena 7.29.0 on the Mac, 19 November 2026, with the
  v0.1.1 release's own universal bundle from its zip, Developer ID-signed and notarised, mounted
  the same way). With Arena's demo loop `Beat 001.wav` (the sound file) playing on the layer
  below, the Audio buffer on Composition and Audio Input on Resolume FFT, **the globe moved in 69
  of 69 snapshot pairs over 13.5 s at the default Shake 0.3** (a mean change of 10.6 levels in
  the globe), and in 40 of 40 at Shake 1.0 (mean 15.8). Before the loop started it was still
  (0 of 24 pairs), and after the loop stopped it came to rest and stayed still (0 of 24). With
  Beat and the same loop it shook in 24 of 24; under Button, 0 of 9 before a press and 24 of 24
  after one. It registered with the same 18 controls, the About line reading "Snowglobe v0.1.1 -
  MIT - Stoatworks Labs, stoatworks-labs.com", and the plugin's log reads "host audio at 48000
  Hz: Resolume's FFT bins are 375 Hz each". With `Bass 001.wav`, a bass line with no drums, Kick
  shook it too, in 40 of 40 pairs (mean 3.3): Resolume's lowest bin holds everything from 0 to
  375 Hz, kick and bass alike, so on Resolume's FFT Kick follows the low end, not the kick drum
  alone. A bundle built from the same source before the tag gave the same picture (69 of 69,
  40 of 40, Bass 40 of 40). A capture device (never tried in Resolume) gives earshot the
  waveform and its real 40–160 Hz band.
- **Why v0.1.0's Kick never fired.** The same set-up with v0.1.0's own bundle moved in 0 of 69
  pairs at Shake 0.3, 0 of 41 at 1.0, and none with `Bass 001.wav` or on Local or External,
  while Beat with the loop playing shook it in 19 of 19. earshot read Resolume's 64 bins as
  spread evenly up to 22.05 kHz and gave each bin to the band holding its centre. Bin 0's
  centre (172 Hz) is above the top of the kick band (160 Hz), so the kick band got no bin at
  all. Measured in Arena 7.29.0 ([AGENTS.md](AGENTS.md#resolumes-fft-measured)), Resolume's bins
  are 64 linear bins of 375 Hz from 0 to 24 kHz, the axis set by the audio engine's 48 kHz
  (the clip's own rate doesn't change it). v0.1.1 takes earshot `02306da`, which shares each bin
  among the bands it overlaps (the kick band gets 0.32 of bin 0), and passes Resolume's sample
  rate to it.
- It has never been in a show, and nothing was timed inside Resolume.

### Measured offline, on macOS

Every number is read off the rendered picture: flakes and tracers found by their alpha
centroids and turned into positions by the harness's own ray trace (angles and scalar Snell,
nothing shared with the shader or the plugin's projection), on cards made to show the glass.
Each check names its tolerance and where the tolerance comes from; AGENTS.md has them all.

- **The fall.** Eight flakes and beads at each of two fills (60 % and 70 %), dropped one at a
  time: speeds against Katz's disc law (broadside, edgewise, tilted 45°) and Stokes's law with
  the GPA's Tables 4 and 17: −0.84 % at 60 %, −2.30 % at 70 % (Cheng's fit, which the plugin
  uses, sits 1.0–2.6 % above the table), against 3.57 % (Cheng's stated 3.5 %, Volk's 0.07 %,
  three standard errors of the fit). The 45° flake glides at −0.20000 of its fall (law −0.2).
  Log-log slopes over size 1.0000 and 2.0000 at 1280x720 (tolerances from the fits' errors);
  the beads' 1.9980 at 320x180 is inside its 0.0036.
- **The swirl.** Tracers at 0.15 R and 0.6 R: decay 0.1194 /s against 0.1181 at 60 % (+1.07 %)
  and 0.2468 against 0.2404 at 70 % (+2.69 %), tolerance 3.57 %; the two tracers' rates in the
  ratio 0.45823, the Bessel profile's 0.45823. A full shake spins the centre at 2.0001 rad/s
  against the chosen 2 (tolerance 0.020 %, three standard errors of the fits).
- **Settling.** Forty discs lifted by one shake with the swirl off: the last frame that changes
  is 25.429 s after it, against 25.201 s derived from the tables (tolerance 0.915 s: Cheng's
  3.5 % and two frames) and 25.415 s from the plugin's own viscosity (measured inside one
  physics step and one frame after it).
- **Still.** After the kicks, every flake at rest by 17.0 s; then 300 frames of music with no
  kick: 0 output floats differ and no flake's state changes; then 300 more with Shake, Fill,
  Snow, Size, Range, Count, Globe and Mix moving: the picture changes on every frame and the
  state on none.
- **The glass.** A grid card at two fills: 69 crossings in the plugin's picture against the same
  crossings in the harness's traced picture, worst 1.2e-4 px (tolerance 1e-3 px, the shader's
  float trace). Magnification at the centre 1.41309 and 1.44290 against the trace's 1.41289 and
  1.44278 (n = 1.41299 and 1.44290, Hoyt's), within 0.014 % (tolerance 0.44 %, 0.2 px over the
  separation measured); at 320x180 within 0.15 % (0.50 %).
- **Flakes through the glass.** Beads held at known points, in front of the card, behind it,
  near the rim: centroids within 0.0005 px of the trace, areas within 0.03 % of πr² through the
  trace's Jacobian (tolerances 0.04 px and 1 %, the box rule's worst over 6000 ellipses).
- **Time.** Shakes placed off every frame and step, at 24, 60 and 144 fps: at every common half
  second, the state, every flake and every output float the same bits. The host's beat at
  97 BPM: shakes within 1 µs of the beat's instant (tolerance 2 µs, the float bar phase).
  Resolume's clock at 499 million ms: bit-identical. Kicks through earshot land 0–26.7 ms
  after the kick (one analysis window), latest 6.7 ms, none missed or doubled; hats alone, none.
- **Kick on Resolume's FFT.** A drum loop at 128 BPM turned into 64 bins the way Arena makes
  them (a 2048-point Hann FFT, every 16 fine bins summed into one, a new spectrum every 2048
  samples and held between), fed to the plugin's FFT buffer: at 48 kHz, 10 kicks, 10 shakes, the
  latest 60.4 ms after its kick (limit 102.0 ms, two spectra and a frame); at 44.1 kHz the same,
  latest 62.5 ms (limit 109.5 ms). Claps and hats alone: no shake. Over a held 49 Hz bass:
  every kick shook it, plus 4 shakes from the bass. With earshot's old rule (each bin to the
  band holding its centre) the check fails, 0 of 10.
- **Raster and alpha.** A resize mid-run: the state and the last frame identical to a run at
  the new size. Mix 0: the clip bit for bit. White painted under transparent pixels: 0 floats
  differ, on a synthetic card and on Arena's two DXV alpha clips. The silhouette's radius off
  the alpha: 324.012 px against 324.
- **No audio device.** With `SNOWGLOBE_NO_DEVICES=1`, or with Trigger On at Beat or Button, an
  interposer sees 0 audio-framework loads and 0 CoreAudio calls (and does see them with the
  gate off and Kick on, so it can see).
- **Defaults** are chosen. On Arena's 33 demo clips, the default snow covers 5.3 % of the globe
  1.5 s after a full shake, and adds a median luminance step of 0.75 over the clip (worst clip
  0.46).
- **Cost** (this Mac's GPU, shared with other builds; GL_TIME_ELAPSED medians, each frame timed
  alone): the defaults **0.34 ms at 720p and 0.68 ms at 1080p (4.1% of a 60 fps frame)**, plus
  about 1.16 ms of CPU; 3000 flakes, the cap, about 1.28 ms of GPU at either size and 2.2 ms of
  CPU. 4K was not measured.

### Not established

- Whether it looks like a snow globe to someone holding one. The fill, the flakes and the
  optics follow the sources; the globe's size (80 mm), the viewing distance, the shake's
  strength (2 rad/s) and how high it lifts the snow are choices.
- Drag above Re → 0: the largest discs in the thinnest fill reach Re 0.63, where the true drag
  is perhaps 10 % higher (an extrapolated correlation). No tumbling into a broadside fall, no
  rotation in the swirl's shear, no sliding down the glass, no flakes bumping.
- Only the swirl's slowest mode, linear: no secondary circulation, no inertial waves.
- No glass wall, no reflections, no meniscus or bubble, no base or figurine; rays through
  transparent parts of the clip leave unrefracted.
- **Kick on Resolume's FFT is the low end, not the kick drum.** Bin 0 holds 0–375 Hz, so bass
  notes, a snare's body or a kick beating against the bass can shake it (above). It is a coarse
  feed too: Arena sends a new spectrum about every 43 ms and holds it between, so a kick is
  placed to that update and the frame, and the kick gate (a quarter of the loudest band) was set
  on the harness's music. Kick from a live audio device has never been tried in Resolume.
- **Chosen, not measured, and open:** a shake lifts only flakes at rest, so under steady kicks
  the snow stays in the lower part of the globe; settling takes tens of seconds (25 s after a
  lift-only shake at the defaults), as real globes do, and no faster fill stays in Stokes;
  outside the globe is transparent, with no option to keep the clip there; the kick gate's 0.25,
  Ω_max 2 rad/s and the 2R·S lift are choices; and a flake grown (Size, Range) while resting
  pokes past the glass until the next shake.
- **Full Shake and Count can flash** on a dark clip (above, and the [user guide](https://stoatworks-labs.com/software/snowglobe/guide/#flashing)).
- No presets and no OpenFX port. The [user guide](https://stoatworks-labs.com/software/snowglobe/guide/) is `docs/USER-GUIDE.md`.

## Browser demo

[snowglobe-demo.stoatworks-labs.com](https://snowglobe-demo.stoatworks-labs.com/) runs the
plugin in your browser: its own C++, the plugin class with the flakes and the swirl stepped in
double, the fill's tables, the optics, its audio input and earshot under it, compiled unmodified
to WebAssembly and drawing with its own GLSL in WebGL2. The page is the host, and it never opens
or lists an audio device or asks for a microphone. It feeds the plugin through earshot's
`feed()` as `sgtest` does: `sgtest`'s own track of kicks and hats, nothing at all (`sgtest
--silent`: Resolume's FFT, empty, so Kick never fires), or an audio file of yours decoded in the
page and never uploaded. The beat is a tempo in the page's host row, and Shake Now has a button
there too. Audio Input, the Audio buffer and the live-device path are not in the browser. The
output is transparent outside the globe, as in Resolume, where you put a layer behind it; the
page puts a checkerboard there. A frame of the page matched `sgtest`'s on the same settings and
audio to a mean of at most 0.013 of an 8-bit code in eight cases, under 1 % of values off, all
at the edges of flakes that came to rest on the glass in a different 8 ms step
([AGENTS.md](AGENTS.md#browser-demo) says why). It is a demo, not the plugin, and the page lists what it leaves out.

## Build

macOS (universal: Apple Silicon and Intel):

```bash
git clone --recursive <this repo>
```

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
cmake --install build
```

The last one copies `Snowglobe.bundle` into `~/Documents/Resolume Arena/Extra Effects`.
Restart Arena: **SW Snowglobe** is under Effects.

Windows needs Visual Studio 2022 and vcpkg (for GLEW); `.github/workflows/release.yml` is the
recipe the release builds with.

## Building and testing

```bash
OXBOW=/path/to/oxbow TMPDIR=/path/to/a/scratch/dir tools/verify.sh
```

That's the whole lot, under bash: shader hygiene and `glslc`, the submodule pin and the
earshot snapshot, a fresh universal build with no warnings, the bundle's architecture, plist
and ad-hoc signature, a host's view of it through `oxbow` (required), every check at two
rasters and every picture check again on the software renderer, the checks that need no GL
context, Arena's demo clips, the pipe, the negative controls, the mutants, the sweep for dead
controls and the render cost. No step opens an audio device. `CLAUDE.md` lists the checks one
by one.

A clip through it, shaken on a 120 BPM beat (without `--set` and `--beat` it shakes on the
harness's own kicks, fed through earshot):

```bash
ffmpeg -i clip.mov -f rawvideo -pix_fmt rgba - \
  | ./build/sgtest --pipe --fps 30 --size 1280x720 --set "Trigger On=Beat" --beat 120 \
  | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 30 -i - out.mp4
```

## License

MIT. See [LICENSE](LICENSE) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->
