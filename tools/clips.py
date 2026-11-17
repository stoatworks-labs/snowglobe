"""Resolume's own demo clips through `matest --pipe`: the alpha check, and
the measurement Drop Size's default was set from. (intensifier's script, by
way of nipkow.)

    tools/clips.py alpha [--build DIR]      the output alpha on the DXV alpha clips
    tools/clips.py defaults [--build DIR]   colour kept against Drop Size, every clip

The clips are Resolume Arena's (`/Applications/Resolume Arena/media`), DXV,
decoded by ffmpeg (`-nostdin`: ffmpeg reads the terminal otherwise, and a
script running it in a loop loses its input). Nothing here is a second copy
of the plugin's formulas: every number is read off the pipe's frames.

**alpha.** The plugin's decision (SPEC-marbling.md): output alpha is ink
density, a premultiplied Mix. On BattleWeapon_Tank_09 and Ethnik2, the two
clips with real transparency, through a marbling made by twelve presses: at
Mix 0 every output byte is the input's; at Mix 1 and 0.5, white painted
under every fully transparent pixel changes no output byte (the clips are
straight alpha over black, so without the paint a plugin that ignored alpha
would pass). The negative control paints about 200 pixels opaque instead,
which must show.

**defaults.** Colour kept: a drop's ink is the clip's mean over half its
radius, so a big drop greys the clip's colours. For each demo clip, one
frame held still, Stone (drops only), Drop Rate 3, twelve presses: the
standard deviation of the output's colours over the input's (RGB, mean of
the three). Drop Size's default is the largest of the sizes tried whose
median over the clips keeps at least 80%.
"""

import argparse
import pathlib
import statistics
import subprocess
import sys

import numpy as np

REPO = pathlib.Path(__file__).resolve().parent.parent
MEDIA = pathlib.Path("/Applications/Resolume Arena/media")
ALPHA_CLIPS = ["Shop74/BattleWeapon_Tank_09.mov", "Shop74/Ethnik2.mov"]
SIZES = [0.03, 0.05, 0.08, 0.12, 0.18]
KEEP = 0.8


def decode(path, width, height, frames, start=1.0):
    args = ["ffmpeg", "-nostdin", "-v", "error", "-ss", str(start), "-i", str(path), "-frames:v", str(frames),
            "-vf", f"scale={width}:{height}", "-f", "rawvideo", "-pix_fmt", "rgba", "-"]
    raw = subprocess.run(args, capture_output=True, check=True).stdout
    size = width * height * 4
    got = len(raw) // size
    if got == 0:
        raise RuntimeError(f"ffmpeg decoded nothing from {path}")
    return raw[:got * size], got


def pipe(matest, raw, width, height, settings, script):
    args = [str(matest), "--pipe", "--size", f"{width}x{height}", "--script", str(script)]
    for s in settings:
        args += ["--set", s]
    out = subprocess.run(args, input=raw, capture_output=True, env={"MARBLING_NO_DEVICES": "1", "PATH": "/usr/bin:/bin"})
    if out.returncode != 0:
        raise RuntimeError(f"matest --pipe failed ({out.returncode}): {out.stderr.decode()[-400:]}")
    return out.stdout


def presses(path, count, every=10, first=5):
    """A cue sheet: Trigger pressed every `every` frames, released the next."""
    lines = []
    for k in range(count):
        f = first + k * every
        lines += [f"{f} Trigger 1", f"{f + 1} Trigger 0"]
    path.write_text("\n".join(lines) + "\n")


def alpha(matest, scratch):
    w, h, n = 320, 180, 160
    script = scratch / "presses.cue"
    presses(script, 12, every=4)  # all twelve inside the shorter clip's 60 frames
    ok = True
    for name in ALPHA_CLIPS:
        raw, got = decode(MEDIA / name, w, h, n)
        frames = np.frombuffer(raw, np.uint8).reshape(got, h, w, 4).copy()
        clear = frames[..., 3] == 0
        white = frames.copy()
        white[clear] = [255, 255, 255, 0]
        # The negative: ~200 pixels made opaque white, which must show.
        loud = frames.copy()
        idx = np.argwhere(clear[0])[:: max(1, int(clear[0].sum()) // 200)][:200]
        for j, i in idx:
            loud[:, j, i] = [255, 255, 255, 255]
        base = ["Trigger On=Manual", "Drop Size=0.6"]
        same0 = pipe(matest, raw, w, h, base + ["Mix=0"], script) == raw
        print(f"  {'ok  ' if same0 else 'FAIL'}  {name}: Mix 0, {got} frames, every byte the input's: {'yes' if same0 else 'NO'}")
        ok = ok and same0
        for mix in ("1", "0.5"):
            a = np.frombuffer(pipe(matest, raw, w, h, base + [f"Mix={mix}"], script), np.uint8)
            b = np.frombuffer(pipe(matest, white.tobytes(), w, h, base + [f"Mix={mix}"], script), np.uint8)
            c = np.frombuffer(pipe(matest, loud.tobytes(), w, h, base + [f"Mix={mix}"], script), np.uint8)
            differ, shows = int((a != b).sum()), int((a != c).sum())
            good = differ == 0 and shows > 0 and clear.mean() > 0.05
            ok = ok and good
            print(f"  {'ok  ' if good else 'FAIL'}  {name}: Mix {mix}, {clear.mean() * 100:.0f}% of pixels transparent; white under them "
                  f"changes {differ} output bytes; ~200 painted opaque change {shows} (the negative control)")
    return ok


def colour_kept(matest, scratch, clip, size):
    w, h = 320, 180
    raw, got = decode(clip, w, h, 1, start=2.0)
    held = raw * 150
    script = scratch / "drops.cue"
    presses(script, 12)
    out = pipe(matest, held, w, h, ["Trigger On=Manual", "Pattern=Stone", "Auto Paper=0", "Drop Rate=0.2857",
                                     f"Drop Size={size_param(size):.6f}", "Mix=1"], script)
    last = np.frombuffer(out[-w * h * 4:], np.uint8).reshape(h, w, 4)[..., :3].astype(float)
    first = np.frombuffer(raw, np.uint8).reshape(h, w, 4)[..., :3].astype(float)
    sin = first.reshape(-1, 3).std(axis=0).mean()
    return last.reshape(-1, 3).std(axis=0).mean() / sin if sin > 1.0 else None


def size_param(radius, low=0.01, high=0.25):
    """Drop Size's host value for a radius (Controls.cpp's geometric range, read from its comment)."""
    import math
    return math.log(radius / low) / math.log(high / low)


def defaults(matest, scratch):
    clips = sorted(p for p in MEDIA.rglob("*.mov"))
    table = {s: [] for s in SIZES}
    for clip in clips:
        row = []
        for s in SIZES:
            k = colour_kept(matest, scratch, clip, s)
            if k is not None:
                table[s].append(k)
                row.append(f"{k:.2f}")
            else:
                row.append("flat")
        print(f"  {clip.relative_to(MEDIA)}: " + "  ".join(f"r {s}: {v}" for s, v in zip(SIZES, row)))
    best = None
    for s in SIZES:
        med = statistics.median(table[s]) if table[s] else 0.0
        print(f"  Drop Size {s}: median colour kept {med:.3f} over {len(table[s])} clips")
        if med >= KEEP:
            best = s
    print(f"  the largest size keeping {KEEP:.0%} on the median clip: {best}")
    return best is not None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("what", choices=["alpha", "defaults"])
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    args = parser.parse_args()
    matest = (args.build if args.build.is_absolute() else REPO / args.build) / "matest"
    if not MEDIA.exists():
        print(f"  FAIL  {MEDIA} not found: the demo clips are Resolume Arena's")
        return 1
    import tempfile
    with tempfile.TemporaryDirectory() as scratch:
        scratch = pathlib.Path(scratch)
        ok = alpha(matest, scratch) if args.what == "alpha" else defaults(matest, scratch)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
