"""Resolume's own demo clips through `sgtest --pipe`: the alpha check, and
what can be measured of the defaults. (intensifier's script, by way of
nipkow and marbling.)

    tools/clips.py alpha [--build DIR]      the output alpha on the DXV alpha clips
    tools/clips.py defaults [--build DIR]   the snow's cover and contrast, every clip

The clips are Resolume Arena's (`/Applications/Resolume Arena/media`), DXV,
decoded by ffmpeg (`-nostdin`: ffmpeg reads the terminal otherwise, and a
script running it in a loop loses its input). Nothing here is a second copy
of the plugin's formulas: every number is read off the pipe's frames.

**alpha.** The plugin's decision (SPEC-snowglobe.md): the clip is resampled
premultiplied, where it is transparent the globe is transparent, and outside
the globe the output is transparent. On BattleWeapon_Tank_09 and Ethnik2,
the two clips with real transparency, with the snow shaken up by four
presses of Shake Now: at Mix 0 every output byte is the input's; at Mix 1
and 0.5, white painted under every fully transparent pixel changes no
output byte (the clips are straight alpha over black, so without the paint
a plugin that ignored alpha would pass). The negative control paints about
200 pixels opaque instead, which must show.

**defaults.** The defaults are chosen (SPEC-snowglobe.md); what this
measures is how they look against real clips. The snow's state depends only
on the shakes and the controls, never on the clip, so one run over an
opaque black card finds the snow: there, a pixel's luminance is the snow's
coverage. For each demo clip, one frame held still, a full shake (Shake Now)
at frame 5, read 1.5 s later with the snow in the air, against the same
clip with no shake (the snow at rest on the floor): **cover** is the mean
coverage over the globe's disc, and **contrast** the median luminance step
(0..1) the snow adds over the clip on pixels the airborne snow covers at
least half and the resting snow not at all. Count is tried at 150, 600 (the
default) and 2400, each at the default Size.
"""

import argparse
import pathlib
import statistics
import subprocess
import sys
import tempfile

import numpy as np

REPO = pathlib.Path(__file__).resolve().parent.parent
MEDIA = pathlib.Path("/Applications/Resolume Arena/media")
ALPHA_CLIPS = ["Shop74/BattleWeapon_Tank_09.mov", "Shop74/Ethnik2.mov"]
COUNTS = [150, 600, 2400]
W, H = 320, 180


def decode(path, width, height, frames, start=1.0):
    args = ["ffmpeg", "-nostdin", "-v", "error", "-ss", str(start), "-i", str(path), "-frames:v", str(frames),
            "-vf", f"scale={width}:{height}", "-f", "rawvideo", "-pix_fmt", "rgba", "-"]
    raw = subprocess.run(args, capture_output=True, check=True).stdout
    size = width * height * 4
    got = len(raw) // size
    if got == 0:
        raise RuntimeError(f"ffmpeg decoded nothing from {path}")
    return raw[:got * size], got


def pipe(sgtest, raw, width, height, settings, script):
    args = [str(sgtest), "--pipe", "--size", f"{width}x{height}", "--script", str(script)]
    for s in settings:
        args += ["--set", s]
    out = subprocess.run(args, input=raw, capture_output=True, env={"SNOWGLOBE_NO_DEVICES": "1", "PATH": "/usr/bin:/bin"})
    if out.returncode != 0:
        raise RuntimeError(f"sgtest --pipe failed ({out.returncode}): {out.stderr.decode()[-400:]}")
    return out.stdout


def presses(path, frames):
    """A cue sheet: Shake Now pressed at each frame, released the next."""
    lines = []
    for f in frames:
        lines += [f"{f} Shake Now 1", f"{f + 1} Shake Now 0"]
    path.write_text("\n".join(lines) + "\n")


def alpha(sgtest, scratch):
    n = 160
    script = scratch / "presses.cue"
    presses(script, [5, 20, 35, 50])  # all four inside the shorter clip's 60 frames
    ok = True
    for name in ALPHA_CLIPS:
        raw, got = decode(MEDIA / name, W, H, n)
        frames = np.frombuffer(raw, np.uint8).reshape(got, H, W, 4).copy()
        clear = frames[..., 3] == 0
        white = frames.copy()
        white[clear] = [255, 255, 255, 0]
        # The negative: ~200 pixels made opaque white, which must show.
        loud = frames.copy()
        idx = np.argwhere(clear[0])[:: max(1, int(clear[0].sum()) // 200)][:200]
        for j, i in idx:
            loud[:, j, i] = [255, 255, 255, 255]
        base = ["Trigger On=Button"]
        same0 = pipe(sgtest, raw, W, H, base + ["Mix=0"], script) == raw
        print(f"  {'ok  ' if same0 else 'FAIL'}  {name}: Mix 0, {got} frames, every byte the input's: {'yes' if same0 else 'NO'}")
        ok = ok and same0
        for mix in ("1", "0.5"):
            a = np.frombuffer(pipe(sgtest, raw, W, H, base + [f"Mix={mix}"], script), np.uint8)
            b = np.frombuffer(pipe(sgtest, white.tobytes(), W, H, base + [f"Mix={mix}"], script), np.uint8)
            c = np.frombuffer(pipe(sgtest, loud.tobytes(), W, H, base + [f"Mix={mix}"], script), np.uint8)
            differ, shows = int((a != b).sum()), int((a != c).sum())
            good = differ == 0 and shows > 0 and clear.mean() > 0.05
            ok = ok and good
            print(f"  {'ok  ' if good else 'FAIL'}  {name}: Mix {mix}, {clear.mean() * 100:.0f}% of pixels transparent; white under them "
                  f"changes {differ} output bytes; ~200 painted opaque change {shows} (the negative control)")
    return ok


def luminance(rgba):
    rgb = rgba[..., :3].astype(float) / 255.0
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def last_frame(out):
    return np.frombuffer(out[-W * H * 4:], np.uint8).reshape(H, W, 4)


def count_param(count, low=50.0, high=3000.0):
    """Count's host value (Controls.cpp's geometric range, 50 .. 3000, read from the spec's table)."""
    import math
    return math.log(count / low) / math.log(high / low)


def defaults(sgtest, scratch):
    frames = 5 + 90 + 1  # the shake at frame 5, read 1.5 s (90 frames at 60 fps) after it
    shaken, still = scratch / "shake.cue", scratch / "still.cue"
    presses(shaken, [5])
    presses(still, [])
    black = (np.zeros((H, W, 4), np.uint8) + np.array([0, 0, 0, 255], np.uint8)).tobytes() * frames
    yy, xx = np.mgrid[0:H, 0:W]
    disc = np.hypot(xx + 0.5 - W / 2, yy + 0.5 - H / 2) < 0.45 * H - 1.0  # the default Globe, 0.9 of the height
    clips = sorted(p for p in MEDIA.rglob("*.mov"))
    ok = True
    for count in COUNTS:
        settings = ["Trigger On=Button", f"Count={count_param(count):.6f}"]
        up = luminance(last_frame(pipe(sgtest, black, W, H, settings, shaken)))
        down = luminance(last_frame(pipe(sgtest, black, W, H, settings, still)))
        cover = float(up[disc].mean())
        mask = disc & (up >= 0.5) & (down == 0.0)
        contrasts = []
        for clip in clips:
            raw, _ = decode(clip, W, H, 1, start=2.0)
            held = raw * frames
            a = luminance(last_frame(pipe(sgtest, held, W, H, settings, shaken)))
            b = luminance(last_frame(pipe(sgtest, held, W, H, settings, still)))
            if mask.sum() > 0:
                contrasts.append(float(np.median((a - b)[mask])))
        if not contrasts:
            ok = False
            print(f"  FAIL  Count {count}: no pixel the snow covers")
            continue
        weak = sum(c < 0.1 for c in contrasts)
        print(f"  Count {count:4d}{' (default)' if count == 600 else '          '}: the snow covers {cover * 100:.2f}% of the globe "
              f"({int(mask.sum())} px at least half); its contrast over the clip, median over {len(contrasts)} clips "
              f"{statistics.median(contrasts):.3f} (worst {min(contrasts):.3f}, best {max(contrasts):.3f}); "
              f"{weak} clips under 0.1")
    return ok


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("what", choices=["alpha", "defaults"])
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    args = parser.parse_args()
    sgtest = (args.build if args.build.is_absolute() else REPO / args.build) / "sgtest"
    if not MEDIA.exists():
        print(f"  FAIL  {MEDIA} not found: the demo clips are Resolume Arena's")
        return 1
    with tempfile.TemporaryDirectory() as scratch:
        scratch = pathlib.Path(scratch)
        ok = alpha(sgtest, scratch) if args.what == "alpha" else defaults(sgtest, scratch)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
