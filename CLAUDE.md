# snowglobe

A snow globe for Resolume Arena/Avenue as one FFGL effect: the clip is a card standing at the
centre of a water-glycerol sphere, seen through one refracting surface, and white flakes (PET
discs, or beads) fall through the fill at their Stokes-regime terminal speed, are carried by
a decaying swirl after each shake, and lie still on the glass once they land. The state (up
to 3000 flakes and the swirl's one vector) lives on the CPU in double, stepped by RK4 on an
8 ms grid; the scene is ray-traced per pixel in the fragment shader. Shakes from earshot's
kicks, the host's beat or a button. C++/GLSL, CMake MODULE → universal `.bundle` (macOS) +
Windows `.dll` (built on GitHub's Windows runner). MIT. Public:
`github.com/stoatworks-labs/snowglobe`.

| id | display name | bundle | type |
| --- | --- | --- | --- |
| `SG01` | `SW Snowglobe` | `Snowglobe.bundle`, `com.stoatworks.ffgl.snowglobe` | effect |

Read `AGENTS.md` before touching the fill (`source/Fill.cpp`), the flakes' mobility, the swirl
or the shake (`source/Globe.cpp`), the projection of flakes (`source/Optics.cpp`), the scene
trace or the flake coverage (`source/Shaders.cpp`), the event times (`gatherEvents`,
`handleKicks`), the clock, or the device gate (`DevicesDisabled`).

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build`. Never from a shared session on this Mac:
  nothing here has been loaded into Arena.
- Render offline: `./build/sgtest --out /tmp/s.png` (the defaults on the harness's colour card,
  fed the harness's kicks and hats through earshot's `feed()`, 12 s in); `--frames N`,
  `--size WxH`, `--clip F` (raw RGBA at `--size`), `--silent`, `--beat BPM` (the host's beat:
  set `Trigger On=Beat` to use it).
- List parameters: `./build/sgtest --list`
- Set anything by name: `./build/sgtest --set "Snow=Beads" --set "Fill=0.5"` (a standard
  control takes the host's 0..1; an option takes its name; anything else, `nan` and values
  outside 0..1 included, is refused, exit 2). `--at N "Name=V"` sets it at frame N (an event is
  released the frame after): `--set "Trigger On=Button" --at 30 "Shake Now=1"` is one full
  shake.
- Film: `./build/sgtest --film 600 --size 1280x720 --script cues | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
  (a cue sheet is `frame Name value` lines).
- A clip through it: `ffmpeg -i in.mov -f rawvideo -pix_fmt rgba - | ./build/sgtest --pipe --fps 30 --size WxH | ffmpeg -f rawvideo -pix_fmt rgba -s WxH -r 30 -i - out.mp4`.
  A partial last frame is dropped (said on stderr); a reader that hangs up ends it with exit
  1, not SIGPIPE's 141.
- Music from a file: `--wav F.wav` (with `--pipe` or `--film`) feeds a 16/24/32-bit PCM or
  float WAV, mixed to mono, through earshot's `feed()` at the file's own rate instead of the
  harness's kicks and hats; silence after it ends. The video and the flash survey use
  Resolume's own demo stems this way, never a device.

## Verify
- Everything: `OXBOW=/path/to/oxbow TMPDIR=<a directory of your own> tools/verify.sh` under
  bash (GLSL reserved words and WebGL2 gaps, `glslc`, the submodule pin, the earshot snapshot,
  both workflows and their parameter count, a fresh universal build with no warnings outside `external/`, lipo, plist,
  ad-hoc signature, `oxbow probe` and `selftest` -- oxbow and TMPDIR are required, never
  skipped; every check at 1280x720 and 320x180; every picture check again on Apple's software
  renderer at 320x180; `--offline`; the box rule's error against `--flakes`' tolerances;
  Arena's alpha clips and the defaults' measurement through `--pipe`; the pipe; the negative
  controls; 46 mutants; the sweep, and again bare; the bench). The build directory (default
  `build-verify/`) is cleared first; `KEEP_BUILD=1` reuses it.
- One check at a time, each at both rasters (`--size WxH`: that raster alone):
  - **Physics, off the pixels**: `--fall` (single flakes' speeds against Katz's disc law and
    Stokes's, with the GPA's tables; the log-log slopes 1 and 2), `--settle` (the last
    changing frame after a lift-only shake against the derived time), `--swirl` (tracers'
    decay against νx1²/R², their ratio against the Bessel profile, a full shake's spin against
    Ω_max), `--still` (bit-identical frames with no kick, controls moving).
  - **Optics, off the pixels**: `--grid` (grid crossings against the harness's own traced
    picture), `--magnify` (the centre's magnification against the trace and Table 23's n),
    `--flakes` (flakes at known 3D points: centroid and area against the trace).
  - **Time and host**: `--rate` (24/60/144 fps), `--clock` (499 million ms), `--resize`,
    `--alpha`, `--triggers` (button, beats inside the frame, kicks through earshot),
    `--coreaudio` (an interposer counts audio calls in a child), `--glstate`, `--robust`.
  - **No GL**: `--model`, `--names`, `--cues` (also under `--offline`).
- The checks can fail: `./build/sgtest --negative` (27 wrong models), `tools/mutate.sh`
  (46 one-character mutants, arm64, 320x180, perl alarm at 300 s; its own tree, never a shared
  one; `tools/mutate.sh TEXT` runs only those whose description contains TEXT).
- No dead controls: `python3 tools/sweep.py --build build` (`--bare` drops the contexts).
- The demo clips: `python3 tools/clips.py alpha|defaults --build build` (Arena's media, read
  only).
- Where `--flakes`' tolerances come from: `python3 tools/coverage.py`.
- Render cost: `./build/sgtest --bench` (720p and 1080p, each frame timed alone).
- `SGTEST_RENDERER=software` runs any of it on Apple's software renderer.
- **No step opens an audio device.** sgtest sets `SNOWGLOBE_NO_DEVICES=1` (its `--coreaudio`
  children set it or not, case by case, always under an interposer that refuses every audio
  call); verify.sh exports it for oxbow and the scripts. With it set, or with Trigger On at
  Beat or Button, the plugin neither lists devices nor looks a name up. A microphone prompt in
  a test run is a bug.
- Shared Mac: arm64 builds while iterating, no 4K loops, never kill a process you didn't start.

## Notes
- **Units are metres and seconds in the physics, R = 40 mm, the globe's centre the origin, y
  up, z toward the viewer.** The optics work in units of R with the pinhole at D = 10R. The
  raster only scales the drawing, so a resize changes nothing in the state.
- **Time is integer microseconds from the first frame**; every event splits the 8 ms step it
  falls in at its microsecond, so the state at an instant depends on the events' instants
  alone and frames at the same instant are bit-identical at any frame rate.
- **The shaders have no trig** (Apple's software renderer's `sin` and `atan` are off): the
  sphere needs only sqrt, written without the cancellation of D² against D² − 1.
- **Flake coverage is a 4×4 box rule over a one-pixel edge ramp**, scaled on the CPU by the
  ramp's exact integral; `tools/coverage.py` gives the rule's worst error and `--flakes` holds
  it to that.
- All host parameters are 0..1 and mapped in `Controls.cpp`; options hold the element index,
  except Audio Input, whose values are `earshot::deviceKey(name)`.
- GLSL reserved words (`patch sample input output filter common active half layout flat
  packed precise`, `noise1`–`noise4`) must not be identifiers; `verify.sh` greps for them.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no host can
  instantiate the plugin.
- `snowglobe_core` is an OBJECT library: the plugin registers itself from a file-scope
  constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- `external/earshot/` is a snapshot of earshot (`github.com/stoatworks-labs/earshot`) at
  `8124191`, unpatched (`SNAPSHOT`). Never edit it here; change earshot upstream and take a new
  snapshot. Its own CLAUDE.md and AGENTS.md are earshot's as of that commit, status lines and all.
- **Provisional hand copies:** `source/StoatworksAbout*.h` (guide = "", no user guide) and
  `ATTRIBUTIONS.md`. Once the project is registered in stoatworks-backend, `sync-about.py`
  and `sync-attributions.py` generate them; edit them there then.
- **CI** (`.github/workflows/ci.yml`): macOS builds arm64 with the tools and runs `--list`,
  `--offline` and `glslc`; Windows builds the DLL with MSVC and loads it as a host does
  (`tools/wincheck.ps1`, whose `-Params` changes whenever a control or the About block does;
  `verify.sh` checks both workflows ask for the count `sgtest --list` gives). `release.yml`
  builds the universal bundle and the DLL with their zips, `.dmg` and installer; dispatched by
  hand it builds without publishing, and a `v*` tag publishes. Actions only because the repo is
  public: a private copy must never enable them.
- Checkout: `~/Projects/resolume/snowglobe`, public at `github.com/stoatworks-labs/snowglobe`.
  The spec is `~/Projects/resolume/specs/SPEC-snowglobe.md`.

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/snowglobe/snowglobe.YYYY-MM-DD.log
    %LOCALAPPDATA%\snowglobe\logs\snowglobe.YYYY-MM-DD.log

`SNOWGLOBE_LOG_DIR` overrides the folder.
