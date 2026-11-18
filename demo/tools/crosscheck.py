"""The browser demo against the plugin's harness: the same clip, the same raster,
the same settings, the same audio, the same beat, two renderers.

    python3 demo/tools/crosscheck.py --sgtest build/sgtest [--keep DIR] [--only TEXT]

Needs a GPU, Google Chrome, numpy, a built sgtest, and stoatworks-backend's
`release/cdpshot.py` (CDPSHOT=path overrides ~/Projects/infrastructure/
stoatworks-backend/release/cdpshot.py). Not run by tools/verify.sh.
(woofer's crosscheck, which is mirrorball's, with snowglobe's clock, beat and
button.)

Each case is rendered twice:

  - by the PAGE: demo/ served from here on a free port and loaded in headless
    Chrome on the real GPU (ANGLE on Metal) at ?size=WxH&clip=C. The page is
    paused, the case's settings applied, its audio chosen, and Restart pressed:
    a new instance at time 0, primed. Then Step is pressed N times, one frame
    of 1/60 s each, so the page has fed the track up to N/60 s at 48 kHz and
    its clock reads N/60 (a case with a press presses Shake Now after step K,
    paused, and the page lets go of it after one frame, as it does for a
    visitor). Mix is set to 0 (the plugin hands the clip through untouched,
    and nothing is fed while paused, so the snow stands still), the canvas is
    read back as the input frame, Mix is put back and the page's output is read
    back. preserveDrawingBuffer is forced on from outside so the canvas can be
    read after its frame; nothing in the page is changed for this.
  - by the PLUGIN: `sgtest --pipe --fps 60 --size WxH --beat 120 --set ...`
    (the real SnowglobePlugin in a GL 4.1 context, its clock preset to
    seconds), fed that input frame for every frame, with the same host values:
    every control as the page's own hostValue() sends it (an option by its
    element's name, which is what --set takes; a standard control's float32
    printed to 9 digits). sgtest's frame k is at k/60 and has been fed the
    track up to k/60, so the page after N steps is sgtest's frame N, the last
    of N + 1. A press after step K is a cue sheet (--script) holding Shake Now
    at 1 on frame K and 0 from K + 1. The page always sends SetBeatInfo, so
    sgtest is always given --beat at the page's tempo.

What cannot agree exactly, and why: sgtest presets the plugin's clock to
seconds (SetClockScaleForTest); the page lets the plugin vote, as a host would,
and primes each new instance with five frames about a millisecond apart so the
vote settles before the first frame shown. Until it settles the plugin steps on
its own wall clock, so the page's instance runs some 6 to 12 ms ahead of
sgtest's from then on (it varies with the wall clock; printed per case, from the
page's clock accessor). Every shake and every frame moves by the same amount,
so the time between them is the same, but the plugin's 8 ms RK4 grid falls at
different places between them. In flight that changes nothing visible (the
integrator's truncation). On landing it does: Globe::step moves a flake a whole
grid step and, if that took it past the glass, puts it on the glass along the
radius (Globe::land), so where a flake comes to rest depends on which 8 ms step
it crossed in, and a flake the next shake lifts starts from there. A landed
flake can lie up to one step's fall (8 ms at its settling speed, a few
hundredths of a pixel at 640x360) from where sgtest's lies, and an edge pixel
of a white flake on a dark clip moves by many codes for that. So the
differences are few, at flake edges, and shrink with the snow's speed and
size: 0.2 mm beads in 75 % glycerol, a few times slower than the default flakes
and a fifth their size, agree to one code after nine shakes. WebAssembly's lack
of fused multiply-add and musl's libm add the same kind of difference, smaller. A shake placed inside a frame is the same microsecond in both (both
clocks count microseconds from their first frame).

The two outputs are compared as premultiplied colour (what shows over black:
colour times alpha), because the plugin writes straight colour and a pixel with
almost no alpha can hold any colour at all, and alpha separately. There is no
pass mark the plugin's own checks give for this. The script prints the largest
difference and does not judge it (see above); it fails a case when the mean
difference is over 0.02 codes or more than 1% of values differ by more than
half a code, in colour or in alpha. A negative control -- sgtest with Shake one
slider step (0.001) from the page's on the defaults case -- must differ from
the page by more than the defaults case does, so the comparison is shown to see
a slip that small; and the press case is also run through sgtest with no
press, which must differ by more than the press case does.

Exit 1 when a case is past that mark, a negative control passes, or the page
logs an error or a WebGL warning.
"""
import argparse
import base64
import functools
import http.server
import json
import os
import subprocess
import sys
import tempfile
import threading
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DEMO = os.path.abspath(os.path.join(HERE, ".."))
sys.dont_write_bytecode = True

CDPSHOT = os.environ.get("CDPSHOT", os.path.expanduser("~/Projects/infrastructure/stoatworks-backend/release/cdpshot.py"))
sys.path.insert(0, os.path.dirname(CDPSHOT))
import cdpshot  # noqa: E402

# The real GPU: SwiftShader rounds differently and is slow; --disable-gpu has no WebGL.
cdpshot.GL_FLAGS = ["--use-gl=angle", "--use-angle=metal", "--ignore-gpu-blocklist"]

SIZE = "640x360"
BPM = 120

# name, clip, audio (the host row's id), frames N, the step after which Shake
# Now is pressed (or None), settings by the plugin's own names (the page's
# kitValues(): {'units': x} is in the plugin's units and goes through its
# compiled ParamFrom*, an option is its element's name, a bare number the
# host's 0..1).
CASES = [
    ("defaults, 6 s into the track", "scene", "track", 360, None, {}),
    ("Trigger On Beat at 120 BPM", "grid", "track", 240, None, {"Trigger On": "Beat"}),
    ("beads in 75 % glycerol", "bars", "track", 300, None,
     {"Snow": "Beads", "Size": {"units": 0.2e-3}, "Fill": {"units": 0.75}}),
    ("3000 flakes, Shake 0.8, a small globe", "detail", "track", 200, None,
     {"Count": {"units": 3000}, "Shake": 0.8, "Globe": {"units": 0.6}}),
    ("Mix 50 % on the lights", "spot", "track", 180, None, {"Mix": 0.5}),
    ("the shape on transparency", "alpha", "track", 240, None, {}),
    ("nothing fed: Kick never fires", "ramp", "silence", 120, None, {}),
    ("Button: Shake Now after step 30", "scene", "silence", 150, 30, {"Trigger On": "Button"}),
]

PRESERVE = """
(() => {
  const real = HTMLCanvasElement.prototype.getContext;
  HTMLCanvasElement.prototype.getContext = function (kind, attrs) {
    if (kind === 'webgl2') attrs = Object.assign({}, attrs || {}, { preserveDrawingBuffer: true });
    return real.call(this, kind, attrs);
  };
})();
"""

# Every control on the panel, as the page sends it to the instance: an option
# by its element's name (what sgtest's --set takes), an event left out (the
# page lets go of it after one frame), anything else its float32.
HOST = """import('./plugin.js').then(m => {
  const out = [];
  for (const p of m.PARAMS) {
    const h = m.hostValue(p.ff, m.mounted.params.get(p.id));
    if (h === null || p.ff.type === 1) continue;
    out.push(p.type === 'option' ? [p.ff.name, p.elements[Math.round(m.mounted.params.get(p.id))]] : [p.ff.name, h]);
  }
  return out;
})"""

# Restart with the audio chosen: the host row's own path (a new instance at time 0).
CHOOSE = """import('./plugin.js').then(m => { m.audio.id = %s; m.restartClock(); })"""

STEP = """(async () => {
  const step = [...document.querySelectorAll('.transport .btn')].find((b) => b.textContent === 'Step');
  for (let i = 0; i < %d; i += 1) {
    step.click();
    await new Promise((resolve) => requestAnimationFrame(() => resolve()));
  }
  const m = await import('./plugin.js');
  return { time: m.mounted.state.time, stats: m.renderer.stats() };
})()"""

PRESS = """(async () => {
  const m = await import('./plugin.js');
  document.querySelector('#snowglobe-shake').click();
  for (let i = 0; i < 4; i += 1) await new Promise((resolve) => requestAnimationFrame(() => resolve()));
  return { held: m.mounted.params.get('shakenow'), shakes: m.renderer.stats().shakes };
})()"""


class Page(cdpshot.Chrome):
    """cdpshot's Chrome, keeping the events it would discard."""

    def __init__(self):
        self.events = []
        super().__init__()
        self.call("Runtime.enable")
        self.call("Log.enable")
        self.call("Page.addScriptToEvaluateOnNewDocument", {"source": PRESERVE})

    def call(self, method, params=None):
        self.msg_id += 1
        want = self.msg_id
        self.ws.send(json.dumps({"id": want, "method": method, "params": params or {}}))
        while True:
            msg = json.loads(self.ws.recv())
            if msg.get("id") == want:
                if "error" in msg:
                    raise RuntimeError(f"{method}: {msg['error']}")
                return msg.get("result", {})
            if "method" in msg:
                self.events.append(msg)

    def run(self, expression):
        r = self.call("Runtime.evaluate", {"expression": expression, "returnByValue": True, "awaitPromise": True})
        if "exceptionDetails" in r:
            raise RuntimeError(f"the page threw: {json.dumps(r['exceptionDetails'])[:400]}")
        return r.get("result", {}).get("value")

    def frames(self):
        return int(self.run("Number(document.querySelector('canvas')?.dataset.frames || 0)"))

    def instances(self):
        return int(self.run("Number(document.querySelector('canvas')?.dataset.instances || 0)"))

    def load(self, url):
        self.call("Page.navigate", {"url": url})
        deadline = time.time() + 30
        while time.time() < deadline:
            try:
                if self.frames() > 2:
                    return
            except RuntimeError:
                pass
            time.sleep(0.25)
        raise RuntimeError(f"{url} drew no frames")

    def after(self, expression):
        """Run, then wait for the frame it causes (the page is paused)."""
        probe = "Number(document.querySelector('canvas')?.dataset.frames || 0) + ':' + (document.querySelector('canvas')?.dataset.instances || 0)"
        n0 = self.run(probe)
        self.run(expression)
        deadline = time.time() + 20
        while time.time() < deadline:
            if self.run(probe) != n0:
                return
            time.sleep(0.03)
        raise RuntimeError("no frame after " + expression[:80])

    def canvas(self):
        """The canvas's bytes, rows top first."""
        r = self.run("""(() => {
          const c = document.querySelector('canvas'); const gl = c.getContext('webgl2');
          const w = c.width, h = c.height, px = new Uint8Array(w * h * 4);
          gl.bindFramebuffer(gl.FRAMEBUFFER, null);
          gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, px);
          const out = new Uint8Array(w * h * 4);
          for (let y = 0; y < h; y += 1) out.set(px.subarray((h - 1 - y) * w * 4, (h - y) * w * 4), y * w * 4);
          let s = '';
          for (let i = 0; i < out.length; i += 0x8000) s += String.fromCharCode.apply(null, out.subarray(i, i + 0x8000));
          return { w, h, b64: btoa(s) };
        })()""")
        return r["w"], r["h"], base64.b64decode(r["b64"])

    def problems(self):
        out = []
        for e in self.events:
            m, p = e["method"], e.get("params", {})
            if m == "Runtime.exceptionThrown":
                out.append(json.dumps(p.get("exceptionDetails", {}))[:300])
            elif m == "Runtime.consoleAPICalled" and p.get("type") in ("error", "warning", "assert"):
                out.append(" ".join(str(a.get("value", a.get("description", ""))) for a in p.get("args", []))[:300])
            elif m == "Log.entryAdded" and p.get("entry", {}).get("level") in ("error", "warning"):
                out.append(f"{p['entry'].get('source')}: {p['entry'].get('text')}"[:300])
        return out


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def serve():
    handler = functools.partial(Quiet, directory=DEMO)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, f"http://127.0.0.1:{server.server_address[1]}/"


def sgtest(binary, host, frame, count, audio, press, scratch, override=None):
    """The last of `count` frames, each fed the same input frame."""
    sets = []
    for name, value in host:
        value = (override or {}).get(name, value)
        sets += ["--set", f"{name}={value:.9g}" if isinstance(value, float) else f"{name}={value}"]
    extra = ["--beat", str(BPM)] + (["--silent"] if audio == "silence" else [])
    if press is not None:
        cues = os.path.join(scratch, "press.cues")
        with open(cues, "w") as handle:
            handle.write(f"{press} Shake Now 1\n{press + 1} Shake Now 0\n")
        extra += ["--script", cues]
    proc = subprocess.Popen([binary, "--pipe", "--fps", "60", "--size", SIZE, *extra, *sets],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            env=dict(os.environ, SNOWGLOBE_NO_DEVICES="1"))

    def feed():
        try:
            for _ in range(count):
                proc.stdin.write(frame)
            proc.stdin.close()
        except BrokenPipeError:
            pass

    writer = threading.Thread(target=feed, daemon=True)
    writer.start()
    size = len(frame)
    last, got = None, 0
    while True:
        chunk = proc.stdout.read(size)
        if not chunk:
            break
        if len(chunk) < size:
            chunk += proc.stdout.read(size - len(chunk))
        if len(chunk) == size:
            last, got = chunk, got + 1
    writer.join()
    err = proc.stderr.read().decode()
    if proc.wait() != 0 or got != count:
        raise RuntimeError(f"sgtest {' '.join(extra + sets)}: {got} of {count} frames; {err[:400]}")
    return last


def compare(a, b):
    """|a - b| as premultiplied colour (codes, colour times alpha / 255) and as alpha."""
    a = a.astype(np.float64)
    b = b.astype(np.float64)
    rgb = np.abs(a[..., :3] * a[..., 3:] - b[..., :3] * b[..., 3:]) / 255.0
    return rgb, np.abs(a[..., 3] - b[..., 3])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--sgtest", required=True)
    ap.add_argument("--keep", help="write each case's two pictures and their difference (x40) here as PNG")
    ap.add_argument("--only", help="run only the cases whose name contains this")
    args = ap.parse_args()

    w, h = map(int, SIZE.split("x"))
    scratch = tempfile.mkdtemp(prefix="sg-crosscheck-")
    server, base = serve()
    page = Page()
    failures = 0
    try:
        for name, clip, audio, count, press, settings in CASES:
            if args.only and args.only not in name:
                continue
            page.load(f"{base}?size={SIZE}&clip={clip}")
            page.after("document.querySelector('.btn--play').click()")
            if page.run("import('./plugin.js').then(m => m.mounted.state.playing)"):
                raise RuntimeError("the page did not pause")
            if page.run("import('./plugin.js').then(m => m.host.bpm)") != BPM:
                raise RuntimeError(f"the page's tempo is not {BPM} BPM")
            kit = page.run(f"import('./plugin.js').then(m => m.kitValues({json.dumps(settings)}))")
            if kit:
                page.after(f"import('./plugin.js').then(m => m.mounted.params.apply({json.dumps(kit)}))")
            before = page.instances()
            page.after(CHOOSE % json.dumps(audio))
            if page.instances() != before + 1:
                raise RuntimeError("choosing the audio did not start a new instance")
            if press is None:
                stepped = page.run(STEP % count)
            else:
                page.run(STEP % press)
                pressed = page.run(PRESS)
                if pressed["held"] != 0 or pressed["shakes"] != 1:
                    raise RuntimeError(f"Shake Now was not one press: {pressed}")
                stepped = page.run(STEP % (count - press))
            if abs(stepped["time"] - count / 60) > 1e-9:
                raise RuntimeError(f"the page's clock reads {stepped['time']} after {count} steps")
            mix = page.run("import('./plugin.js').then(m => m.mounted.params.get('mix'))")
            page.after("import('./plugin.js').then(m => m.mounted.params.set('mix', 0))")
            cw, ch, frame = page.canvas()
            if (cw, ch) != (w, h):
                raise RuntimeError(f"the canvas is {cw}x{ch}, not {SIZE}")
            page.after(f"import('./plugin.js').then(m => m.mounted.params.set('mix', {mix}))")
            cw, ch, ours = page.canvas()
            host = page.run(HOST)
            theirs = sgtest(args.sgtest, host, frame, count + 1, audio, press, scratch)

            a = np.frombuffer(ours, np.uint8).reshape(h, w, 4)
            b = np.frombuffer(theirs, np.uint8).reshape(h, w, 4)
            rgb, alpha = compare(a, b)
            off = int((rgb > 0.5).sum()) + int((alpha > 0.5).sum())
            ok = rgb.mean() <= 0.02 and alpha.mean() <= 0.02 and off <= 0.01 * (rgb.size + alpha.size)
            failures += not ok
            s = stepped["stats"]
            ahead = s["clockUs"] / 1e6 - count / 60
            print(f"{'ok  ' if ok else 'FAIL'}  {name:<40} {clip:<6} mean {rgb.mean():.2e} codes, max {rgb.max():.0f},"
                  f" {off} of {rgb.size + alpha.size} values off; alpha mean {alpha.mean():.2e}, max {alpha.max():.0f}"
                  f" | page: {s['shakes']:.0f} shakes, {s['suspended']:.0f} not at rest, {s['drawn']:.0f} drawn,"
                  f" its clock {ahead * 1000:+.1f} ms on sgtest's")
            if name.startswith("defaults") or press is not None:
                if press is not None:
                    label = "negative: the same with no press"
                    slip_bytes = sgtest(args.sgtest, host, frame, count + 1, audio, None, scratch)
                else:
                    label = "negative: Shake one step (0.001) off"
                    slip_bytes = sgtest(args.sgtest, host, frame, count + 1, audio, None, scratch,
                                        {"Shake": dict(host)["Shake"] + 0.001})
                slip, _ = compare(a, np.frombuffer(slip_bytes, np.uint8).reshape(h, w, 4))
                caught = slip.mean() > rgb.mean()
                failures += not caught
                print(f"{'ok  ' if caught else 'FAIL'}  {label:<40} {'':<6} mean {slip.mean():.2e} codes, max {slip.max():.0f},"
                      f" against the case's {rgb.mean():.2e}; {int((slip > 0.5).sum())} values off")
            if args.keep:
                from PIL import Image
                os.makedirs(args.keep, exist_ok=True)
                stem = os.path.join(args.keep, "".join(c if c.isalnum() else "-" for c in name))
                Image.fromarray(a, "RGBA").save(f"{stem}-page.png")
                Image.fromarray(b, "RGBA").save(f"{stem}-sgtest.png")
                Image.fromarray(np.clip(rgb * 40, 0, 255).astype(np.uint8)).save(f"{stem}-diff-x40.png")
        problems = page.problems()
        for p in problems:
            print(f"FAIL  the page logged: {p}")
        failures += len(problems)
        renderer = page.run("(() => { const gl = document.createElement('canvas').getContext('webgl2'); const x = gl.getExtension('WEBGL_debug_renderer_info'); return x ? gl.getParameter(x.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER); })()")
        print(f"\nthe page rendered on: {renderer}")
    finally:
        page.close()
        server.shutdown()
    print(f"{'all cases within the mark' if failures == 0 else f'{failures} failure(s)'}")
    return 1 if failures else 0


if __name__ == "__main__":
    os.environ.setdefault("TMPDIR", tempfile.gettempdir())
    sys.exit(main())
