"""Render every parameter at both ends of its range and fail if any made no
difference.

**This is the only thing in the repo that catches a dead control.** A GLSL
uniform whose name does not match the C++ is silently ignored --
`glGetUniformLocation` returns -1 and `glUniform` on -1 is a documented no-op
-- so a slider can be stone dead while everything compiles, links, loads and
renders.

Each control is swept where it can act. Every render is 240 frames (4 s at
60 fps) at 320x180 on the defaults (Nonpareil, Trigger On = Onsets), with the
harness's kicks and hats fed through earshot's feed(), on matest's colour
card: at 4 s the first recipe is in its strokes. (At 5 s Auto Paper has just
laid a fresh sheet, and Trigger On, Drop Rate and Mix all read as dead: the
read time matters.) An option is set by name, an event pressed with `--at`. A control whose
ends differ by less than `--floor` (mean 8-bit difference per channel) is
barely alive: vectrix's lesson.

Audio Input is not swept: it acts on audio devices, and no step of
verification may open one (MARBLING_NO_DEVICES=1 keeps the plugin from even
listing them). `matest --coreaudio` checks that it is left alone. Audio is
the host's FFT buffer, which matest writes itself.

Usage::

    tools/sweep.py [--build BUILD_DIR] [--verbose] [--jobs N] [--bare]

`--bare` drops every per-control context, the honesty check: what goes dead
there is the list of controls that only act with another one set, which
AGENTS.md records.
"""

import argparse
import concurrent.futures
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent

# name -> (low settings, high settings, context settings). A string is a
# --set; a list is passed as it stands.
SWEEP = {
    "Pattern": (["Pattern=Stone"], ["Pattern=Nonpareil"], []),
    "Trigger On": (["Trigger On=Manual"], ["Trigger On=Onsets"], []),
    # A press at frame 10 against none; under Onsets it adds a trigger, so it
    # acts bare too.
    "Trigger": ([], [["--at", "10", "Trigger=1"]], ["Trigger On=Manual"]),
    # Paper ten frames before the read: under Manual nothing comes after it.
    "Paper": ([], [["--at", "230", "Paper=1"]], ["Trigger On=Manual", ["--at", "10", "Trigger=1"]]),
    # Auto Paper acts only once a recipe has finished: read at 5.5 s, after
    # the first one (matest takes the last --frames).
    "Auto Paper": (["Auto Paper=0"], ["Auto Paper=1"], [["--frames", "330"]]),
    "Drop Size": (["Drop Size=0.3"], ["Drop Size=0.8"], []),
    "Drop Rate": (["Drop Rate=0.1"], ["Drop Rate=0.9"], []),
    "Tine Spacing": (["Tine Spacing=0.2"], ["Tine Spacing=0.8"], []),
    "Depth": (["Depth=0.2"], ["Depth=0.8"], []),
    "Speed": (["Speed=0.2"], ["Speed=0.8"], []),
    "Mix": (["Mix=0"], ["Mix=1"], []),
}

# The About block (a text line and browser buttons, no pixel to sweep), the
# host's FFT buffer, and the device menu.
SKIP = {"About", "Project page", "Source on GitHub", "Support the work", "User guide", "Audio", "Audio Input"}


def read_png(path):
    """Enough of PNG for matest's own writer: 8-bit RGBA, filter 0 rows."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, width, height, idat = 8, 0, 0, b""
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(body[0:4], "big")
            height = int.from_bytes(body[4:8], "big")
            if body[8] != 8 or body[9] != 6:
                raise ValueError("not 8-bit RGBA")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray()
    for y in range(height):
        start = y * (stride + 1)
        if raw[start] != 0:
            raise ValueError("a PNG row with a filter")
        out += raw[start + 1:start + 1 + stride]
    return bytes(out)


def difference(a, b):
    if len(a) != len(b):
        return 255.0
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def env():
    return dict(os.environ, MARBLING_NO_DEVICES="1")


def render(matest, out, settings):
    args = [str(matest), "--out", str(out), "--size", "320x180", "--frames", "240"]
    for setting in settings:
        args += list(setting) if isinstance(setting, list) else ["--set", setting]
    result = subprocess.run(args, capture_output=True, text=True, env=env())
    if result.returncode != 0:
        raise RuntimeError(f"matest failed: {' '.join(args)}\n{result.stderr.strip()}")
    return read_png(out)


def parameters(matest):
    result = subprocess.run([str(matest), "--list"], capture_output=True, text=True, env=env())
    if result.returncode != 0:
        raise RuntimeError(f"matest --list failed: {result.stderr.strip()}")
    names = []
    for line in result.stdout.splitlines()[1:]:
        # The name column is 16 wide and a 16-character name leaves one space.
        m = re.match(r"^(\d+)\s+(.+?)\s+(standard|option|event|bool|buffer|text|integer|other)\s", line.strip() + " ")
        if m:
            names.append(m.group(2).strip())
    return names


def sweep_one(matest, scratch, name, bare):
    low, high, context = SWEEP[name]
    context = [] if bare else list(context)
    tag = re.sub(r"[^A-Za-z]", "", name)
    before = render(matest, scratch / f"{tag}-a.png", context + low)
    after = render(matest, scratch / f"{tag}-b.png", context + high)
    return name, difference(before, after)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--bare", action="store_true")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--floor", type=float, default=0.05,
                        help="mean 8-bit difference below which a control is 'barely alive'")
    args = parser.parse_args()

    build = args.build if args.build.is_absolute() else REPO / args.build
    matest = build / "matest"
    if not matest.exists():
        print(f"{matest} not found", file=sys.stderr)
        return 1

    declared = parameters(matest)
    if not declared:
        print("matest --list gave no parameters", file=sys.stderr)
        return 1
    unknown = [n for n in declared if n not in SWEEP and n not in SKIP]
    if unknown:
        # A new parameter with no sweep is a hole, not a pass.
        print(f"no sweep defined for: {', '.join(unknown)}", file=sys.stderr)
        return 1
    missing = [n for n in SWEEP if n not in declared]
    if missing:
        print(f"swept but not a parameter: {', '.join(missing)}", file=sys.stderr)
        return 1

    dead, weak = [], []
    with tempfile.TemporaryDirectory() as scratch:
        scratch = pathlib.Path(scratch)
        names = [n for n in declared if n in SWEEP]
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            results = list(pool.map(lambda n: sweep_one(matest, scratch, n, args.bare), names))
    for name, delta in results:
        if delta == 0.0:
            dead.append(name)
            print(f"  DEAD {name:16s} both ends identical")
        elif delta < args.floor:
            weak.append(name)
            print(f"  WEAK {name:16s} mean delta {delta:.4f}")
        elif args.verbose:
            print(f"  ok   {name:16s} mean delta {delta:.3f}")
    print(f"SW Marbling: {len(results)} parameters swept{' (bare: no contexts)' if args.bare else ''}, "
          f"{len(dead)} dead, {len(weak)} barely alive; {len(SKIP & set(declared))} not swept ({', '.join(sorted(SKIP & set(declared)))})")
    if dead or weak:
        if not args.bare:
            print("\nA parameter that changes nothing is usually a uniform name that does not match\n"
                  "the C++, or a setting nothing reads. Both are silent everywhere else.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
