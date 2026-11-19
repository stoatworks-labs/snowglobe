/**
 * Snowglobe — browser demo.
 *
 * A snow globe as an FFGL effect (SW Snowglobe, SG01): the clip is the scene
 * standing at the centre of a sphere of water and glycerine, seen through one
 * refracting surface, and white flakes fall through the fill at their
 * low-Reynolds-number terminal speed, are carried by a decaying swirl after
 * each shake, and lie still on the glass once they land. Shakes come from
 * kicks in the music, the host's beat, or a button.
 *
 * ---------------------------------------------------------------------------
 * What runs here, and what does not
 * ---------------------------------------------------------------------------
 *
 * **This page does not port the plugin. It runs it.** `snowglobe-core.wasm`
 * is the plugin's own C++ compiled UNMODIFIED with emscripten
 * (demo/tools/build-wasm.sh): `source/Snowglobe.cpp` — the plugin class, with
 * its constructor, its clock and seconds-or-milliseconds vote, its kicks, beat
 * and button, the instance buffer it uploads every frame and every uniform it
 * sets — and everything it calls: the flakes and the swirl stepped by RK4 in
 * double on an 8 ms grid (Globe.cpp), the fill's tables (Fill.cpp), each flake
 * projected through the glass (Optics.cpp), the audio input (Audio.cpp) and,
 * under it, earshot's snapshot, built with no device backend at all
 * (EARSHOT_CAPTURE=OFF), with the FFGL SDK's CFFGLPlugin, its parameter
 * bookkeeping, FFGLShader and FFGLFBO. Its GL calls go to this page's WebGL2
 * context through emscripten's GL library.
 *
 * What is the page's and not the plugin's:
 *
 *   - **The host.** demo/wasm/glue.cpp constructs the plugin (what
 *     EffectPlugin.cpp's registration does in a bundle), reads its parameter
 *     declarations back through the SDK's host getters — the panel below is
 *     built from them, not from a list typed here — and forwards
 *     SetFloatParameter, SetBeatInfo, SetTime and ProcessOpenGL, as
 *     tools/sgtest's Rig does.
 *   - **The audio.** No device: the page never asks for a microphone and never
 *     lists an input. It feeds the plugin as its harness does, through
 *     earshot's feed() (AudioForTest().Feed, the path a device's samples take
 *     inside earshot): sgtest's own track, generated in the wasm by sgtest's
 *     own function (copied into glue.cpp, held to tools/sgtest/main.cpp by
 *     demo/tools/check_shaders.py), or a file the visitor chooses, decoded
 *     here. Or nothing at all, as `sgtest --silent`: the plugin's own default
 *     Audio Input, Resolume's FFT, on a buffer nobody writes.
 *   - **The beat.** The host row's tempo, and a bar phase that runs on the
 *     page's clock, handed to SetBeatInfo every frame as Resolume does.
 *   - **Four GL entry points** (demo/wasm/gl_shim.cpp). glShaderSource: the
 *     plugin hands GL desktop GLSL 4.10; `shaderSource()` below refuses any
 *     text that is not byte for byte one of this page's assemblies of its copy
 *     (shaders.js, held to source/Shaders.cpp by demo/tools/check_shaders.py),
 *     then applies the kit's `port()`. glEnable / glDisable / glIsEnabled:
 *     GL_PROGRAM_POINT_SIZE does not exist in WebGL2.
 *   - **The panel's read-outs.** The number beside a slider is Controls.cpp's
 *     conversion, compiled in (glue.cpp's sg_convert); the units, the tooltips
 *     and the presets are this page's words. The presets are written in
 *     glycerol fractions, millimetres and counts and go to the host's 0..1
 *     through the plugin's own inverse functions (sg_param_from).
 *
 * ---------------------------------------------------------------------------
 * Decisions this page made, and why
 * ---------------------------------------------------------------------------
 *
 * **Every audio choice is a new instance, from time 0.** earshot's feed, once
 * begun, is what its engine reads until a selectDevice, and any select ends
 * it; so the page never switches a running instance between sources. Changing
 * the audio restarts the clock, as Restart does.
 *
 * **Each new instance is primed** (mirrorball's rule, woofer's code): five
 * frames about a millisecond apart, so the plugin's seconds-or-milliseconds
 * vote settles before the first frame shown and a paused page stands still.
 * Nothing is fed during them and the bar phase stands still.
 *
 * **Shake Now is FF_TYPE_EVENT**, and the kit has no event control: here it is
 * a toggle the page lets go of after one frame (filings' rule), which is one
 * press, the rising edge the plugin counts. The host row's button presses the
 * same control.
 *
 * **Left off the panel**: Audio Input (no device exists here), the Audio
 * buffer (Resolume's FFT, which nothing in a browser writes) and the About
 * block, as on every page in the suite.
 *
 * **No photosensitivity note.** A shake lifts the snow; it does not flash the
 * frame, and the fastest trigger the panel offers (Beat at 174 BPM) is under
 * three shakes a second. No default is changed for the page.
 */

import { mountDemo } from './vendor/demo.js';
import { port, GLError } from './vendor/gl.js';
import * as S from './shaders.js';
import createSnowglobe from './snowglobe-core.js';

//===========================================================================
// FFGL.h's parameter types, by number: what GetParamType returns.
//===========================================================================

const FF_TYPE_BOOLEAN = 0;
const FF_TYPE_EVENT = 1;
const FF_TYPE_STANDARD = 10;
const FF_TYPE_OPTION = 11;
const FF_TYPE_BUFFER = 12;
const FF_TYPE_TEXT = 100;

//===========================================================================
// The GLSL: the page's checked copy, assembled as the plugin assembles it
// (ASSEMBLY is read out of AssembleVertex() and AssembleFragment() by
// check_shaders.py), one text per stage of each of the three programs.
//===========================================================================

const PAGE_SHADERS = Object.entries(S.ASSEMBLY).map(([name, pieces]) => ({
  name,
  stage: name.endsWith('_vertex') ? 'vertex' : 'fragment',
  text: pieces.map((p) => S[p]).join(''),
}));

/** Which stages the compiled plugin handed over, and whether each was ours. */
const shaderCheck = { matched: new Set(), refused: 0 };

/**
 * Called by gl_shim.cpp's glShaderSource with the text the compiled plugin
 * handed to GL. Anything but one of this page's own assemblies is refused (an
 * empty source, so the plugin's InitGL fails and says so) rather than compiled.
 */
function shaderSource(stage, text) {
  const ours = PAGE_SHADERS.filter((s) => s.stage === stage && s.text === text);
  if (!ours.length) {
    shaderCheck.refused += 1;
    return '';
  }
  for (const s of ours) shaderCheck.matched.add(s.name);
  return port(text, { stage });
}

//===========================================================================
// The module.
//===========================================================================

const printed = [];
let core;
try {
  core = await createSnowglobe({
    snowglobeShaderSource: shaderSource,
    print: (text) => { printed.push(text); console.log(text); },
    printErr: (text) => { printed.push(text); console.warn(text); },
  });
} catch (error) {
  const root = document.querySelector('#demo');
  root.textContent = `The demo could not start: this browser would not run its WebAssembly (${error.message}). The plugin, its source and its downloads are at https://github.com/stoatworks-labs/snowglobe.`;
  throw error;
}

const text = (pointer) => core.UTF8ToString(pointer);

// Before any instance: the plugin's log in the page's memory, and verify.sh's
// switch that keeps the plugin's AudioInput from asking for a device list.
core._sg_prepare();

//===========================================================================
// The declarations, read from the plugin's constructor through the SDK.
//===========================================================================

function declarations() {
  const h = core._sg_new();
  const list = [];
  const n = core._sg_param_count(h);
  for (let i = 0; i < n; i += 1) {
    const type = core._sg_param_type(h, i);
    const d = { index: i, name: text(core._sg_param_name(h, i)), type, group: text(core._sg_param_group(h, i)) };
    d.default = type === FF_TYPE_TEXT ? null : core._sg_param_default(h, i);
    if (type === FF_TYPE_OPTION) {
      d.elements = [];
      d.values = [];
      for (let e = 0; e < core._sg_param_element_count(h, i); e += 1) {
        d.elements.push(text(core._sg_param_element_name(h, i, e)));
        d.values.push(core._sg_param_element_value(h, i, e));
      }
    }
    list.push(d);
  }
  const inputs = [core._sg_min_inputs(h), core._sg_max_inputs(h)];
  // Kept, never given GL: the read-outs and the presets ask it for
  // Controls.cpp's conversions.
  return { list, inputs, converter: h };
}

const DECLARED = declarations();
if (DECLARED.inputs.join() !== '1,1') {
  throw new Error(`the plugin declares ${DECLARED.inputs.join('–')} inputs; this page hands it exactly one`);
}
const declared = (name) => {
  const d = DECLARED.list.find((x) => x.name === name);
  if (!d) throw new Error(`the plugin no longer declares ${name}; this page's notes rely on it`);
  return d;
};

//===========================================================================
// What the panel leaves out, by the plugin's own names: the device menu (no
// device exists here), Resolume's FFT buffer (nothing in a browser writes it)
// and the About block.
//===========================================================================

const LEFT_OUT = ['Audio Input', 'Audio'];
const onPanel = (d) => d.group !== 'About' && !LEFT_OUT.includes(d.name);
for (const name of LEFT_OUT) declared(name);

//===========================================================================
// The kit's parameters, from the declarations. The id is the name as Arena
// addresses it (lower case, spaces removed).
//===========================================================================

/** The value the host would send for a kit control, or null for none. */
function hostValue(d, v) {
  switch (d.type) {
    case FF_TYPE_OPTION: return Math.fround(d.values[Math.round(v)] ?? 0);
    case FF_TYPE_BOOLEAN:
    case FF_TYPE_EVENT: return v > 0.5 ? 1 : 0;
    case FF_TYPE_TEXT:
    case FF_TYPE_BUFFER: return null;
    default: return Math.fround(v);
  }
}

/** The kit reads these from the query string itself, and this page reads `audio` and `tempo`. */
const RESERVED_QUERY = new Set(['embed', 'size', 'clip', 'bg', 'audio', 'tempo']);
function addressOf(name) {
  const address = name.toLowerCase().replace(/\s+/g, '');
  return RESERVED_QUERY.has(address) ? `param-${address}` : address;
}

const SNOW = declared('Snow');
const TRIGGER = declared('Trigger On');
const SHAKE_NOW = declared('Shake Now');
if (SHAKE_NOW.type !== FF_TYPE_EVENT) throw new Error('Shake Now is no longer an event; this page lets go of it after one frame');
const SHAKE_NOW_ID = addressOf(SHAKE_NOW.name);
let liveParams = null; // the kit's Params, once mounted

/** Snow's host value now: Size's conversion depends on it. */
function snowHost(values = null) {
  if (values) return hostValue(SNOW, values[addressOf(SNOW.name)] ?? kitDefault(SNOW));
  return hostValue(SNOW, liveParams ? liveParams.get(addressOf(SNOW.name)) : kitDefault(SNOW));
}

/** Controls.cpp's conversion of a host value. */
const convert = (d, v, snow = snowHost()) => core._sg_convert(DECLARED.converter, d.index, Math.fround(v), snow);
/** And its inverse: the host's 0..1 for a value in the plugin's units. */
const paramFrom = (d, units, snow = snowHost()) => core._sg_param_from(DECLARED.converter, d.index, units, snow);

//===========================================================================
// The read-outs: Controls.cpp's number, this page's unit.
//===========================================================================

const fixed = (n, d) => n.toFixed(d);
// Short: the read-out column is about ten characters wide. The tooltips say
// what each is.
const UNITS = {
  Shake: (x) => `S ${fixed(x, 2)}`,
  Fill: (x) => `${fixed(x * 100, 1)} %`,
  Size: (x) => `${fixed(x * 1000, 2)} mm`,
  Range: (x) => `×${fixed(x, 2)}`,
  Count: (x) => `${x}`,
  Globe: (x) => `${fixed(x, 2)} H`,
};

function readout(d) {
  return (v) => {
    const x = convert(d, v);
    if (Number.isNaN(x)) return d.name === 'Mix' ? `${fixed(v * 100, 0)}%` : fixed(v, 2);
    return (UNITS[d.name] ?? ((y) => fixed(y, 3)))(x);
  };
}

/** Tooltips: this page's words, from the README's description of each control. */
const HINTS = {
  Shake: 'How hard a kick or a beat shakes the globe, the strength S from 0 to 1 (0.3 at the default). A shake spins the fill in the sphere’s slowest swirl and lifts the snow off the glass; S 1 is a full shake. Shake Now is always a full shake.',
  'Trigger On': 'What shakes the globe. Kick, the default: kick drums in the audio, earshot’s lowest band (40–160 Hz), kicks within 50 ms one shake; here the audio is the host row’s, fed as a device’s would be. Beat: each quarter of the host’s bar, at the host row’s tempo. Button: Shake Now only.',
  'Shake Now': 'One full shake, now, under every Trigger On. FF_TYPE_EVENT in the plugin; a toggle the page lets go of after one frame here. The host row under the picture has the same button.',
  Fill: 'The fill, by mass of glycerol in water, 60–80 %: with it the viscosity (10.9 to 60.9 mPa·s), the density and the refractive index (1.413 to 1.443) from the published tables. Thicker fill, slower snow, a swirl that dies sooner and, slightly, more magnification.',
  Snow: 'Flakes: thin PET discs, 48 µm thick, which fall broadside at a speed proportional to their radius and glide sideways at their tilt. Beads: PET spheres, which fall by Stokes’s law, as their radius squared.',
  Size: 'The largest flake’s radius: 0.30–1.25 mm for flakes, 0.08–0.30 mm for beads (the read-out follows Snow). The slider is geometric. Twice the radius, twice the speed for flakes; four times for beads.',
  Range: 'The smallest flake as a fraction of the largest, ×0.25–×1. At ×1 every flake is one size and they all fall together.',
  Count: '50–3000 flakes (600 at the default). The slider is geometric.',
  Globe: 'The globe’s diameter as a fraction of the frame’s height, 0.25–1 (0.9). Outside it the output is transparent.',
  Mix: 'The globe over the clip, premultiplied. 0 hands the clip straight back, bit for bit.',
};

function kitDefault(d) {
  switch (d.type) {
    case FF_TYPE_OPTION: {
      const i = d.values.findIndex((v) => v === d.default);
      return i >= 0 ? i : Math.round(d.default);
    }
    case FF_TYPE_BOOLEAN:
    case FF_TYPE_EVENT: return d.default > 0.5 ? 1 : 0;
    default: return d.default;
  }
}

const KIT_TYPES = {
  [FF_TYPE_STANDARD]: 'standard',
  [FF_TYPE_OPTION]: 'option',
  [FF_TYPE_BOOLEAN]: 'boolean',
  // The kit has no event control: a toggle let go of after one frame.
  [FF_TYPE_EVENT]: 'boolean',
};

const PARAMS = [];
for (const d of DECLARED.list.filter(onPanel)) {
  if (KIT_TYPES[d.type] === undefined) throw new Error(`no kit control for FFGL type ${d.type} (${d.name})`);
  const p = { id: addressOf(d.name), name: d.name, type: KIT_TYPES[d.type], group: d.group, hint: HINTS[d.name], ff: d, default: kitDefault(d) };
  if (d.type === FF_TYPE_OPTION) p.elements = d.elements;
  if (d.type === FF_TYPE_STANDARD) p.display = readout(d);
  PARAMS.push(p);
}
const DEFAULTS = Object.fromEntries(PARAMS.map((p) => [p.id, p.default]));
const EVENTS = PARAMS.filter((p) => p.ff.type === FF_TYPE_EVENT).map((p) => p.id);
const byName = (name) => PARAMS.find((p) => p.name === name);

const query = new URLSearchParams(window.location.search);
const embed = query.has('embed') && query.get('embed') !== '0';

//===========================================================================
// The audio: the stand-in for what reaches the plugin in Resolume.
//===========================================================================

const FEED_RATE = 48000;
const AUDIO = [
  { id: 'track', name: 'sgtest’s track (kicks and hats)',
    hint: 'The plugin’s harness’s own music, generated in the WebAssembly by sgtest’s own function: a kick on every beat at 120 BPM (a 55 Hz body with a pitch drop) and a hat on the off-beat. Fed through earshot’s feed() as a device’s samples would arrive, at 48 kHz, the same on both channels, as sgtest feeds it.' },
  { id: 'silence', name: 'Nothing (the Resolume FFT, empty)',
    hint: 'sgtest --silent: nothing is fed, and the plugin listens where its default Audio Input points, Resolume’s FFT buffer, which nothing in a browser writes. Kick then never fires: use Beat or Shake Now.' },
  { id: 'file', name: 'Your audio file',
    hint: 'A file you choose, decoded in this page by the browser, mixed to mono as sgtest --wav mixes a WAV and fed through feed(), looped. It is not uploaded and not played aloud.' },
];
const audio = {
  id: AUDIO.some((a) => a.id === query.get('audio') && a.id !== 'file') ? query.get('audio') : 'track',
  file: null, // { name, mono: Float32Array, seconds, channels }
  epoch: 0, // bumped on every change of audio: a new instance
};

/**
 * Decode a file the visitor chose. An OfflineAudioContext renders to memory,
 * not to an output device; decodeAudioData resamples to its 48 kHz. The first
 * two channels are averaged, as sgtest's readWav does. Nothing here opens a
 * device or asks for one.
 */
async function decodeFile(file) {
  const bytes = await file.arrayBuffer();
  const context = new OfflineAudioContext(2, 1, FEED_RATE);
  const buffer = await context.decodeAudioData(bytes);
  const left = buffer.getChannelData(0);
  if (!left.length) throw new Error('it holds no samples');
  const mono = new Float32Array(left.length);
  if (buffer.numberOfChannels > 1) {
    const right = buffer.getChannelData(1);
    for (let k = 0; k < mono.length; k += 1) mono[k] = (left[k] + right[k]) / 2;
  } else {
    mono.set(left);
  }
  return { name: file.name, mono, seconds: mono.length / FEED_RATE, channels: buffer.numberOfChannels };
}

//===========================================================================
// The host's tempo. Resolume sends its BPM and bar phase every frame; here the
// page is the host. The bar phase runs on the page's clock at the tempo in the
// host row, in 4/4, continuous across a tempo change, and stands still while
// paused. From time 0 at a fixed tempo it is sgtest --beat's phase exactly,
// fmod( t * bpm / 240, 1 ).
//===========================================================================

const TEMPOS = [60, 90, 100, 120, 128, 140, 150, 174];
const wantedTempo = Number(query.get('tempo'));
const host = {
  bpm: TEMPOS.includes(wantedTempo) ? wantedTempo : 120,
  // The phase is originPhase + (time - originTime) * bpm / 240, mod 1.
  originTime: 0,
  originPhase: 0,
  phaseAt(time) {
    const p = this.originPhase + (time - this.originTime) * this.bpm / 240;
    return p - Math.floor(p);
  },
  setTempo(bpm, time) {
    this.originPhase = this.phaseAt(time);
    this.originTime = time;
    this.bpm = bpm;
  },
  restart() {
    this.originTime = 0;
    this.originPhase = 0;
  },
};

//===========================================================================
// The presets: whole states on top of the constructor's defaults, by the
// plugin's own names. A number in { units } is in the plugin's units (a
// glycerol fraction, a radius in metres, a count, a fraction of the frame's
// height) and goes through the plugin's own inverse (sg_param_from); an
// option is its element's name; a bare number is the host's 0..1. They are
// the page's choices.
//===========================================================================

const u = (units) => ({ units });
const PRESET_UNITS = [
  ['The defaults: 600 flakes in 60 % glycerol, shaken by the kick', {}],
  ['On the beat: a shake every quarter at the host row’s tempo', { 'Trigger On': 'Beat' }],
  ['Button only: nothing shakes it but Shake Now', { 'Trigger On': 'Button' }],
  ['Beads: PET spheres up to 0.3 mm, falling by Stokes’s law', { Snow: 'Beads', Size: u(0.3e-3) }],
  ['Thick fill: 80 % glycerol, slow snow and a short swirl', { Fill: u(0.8) }],
  ['A blizzard: 3000 flakes and a hard shake', { Count: u(3000), Shake: 0.8 }],
  ['A few big flakes: 100 of them, all 1.25 mm', { Count: u(100), Size: u(1.25e-3), Range: u(1) }],
  ['A small globe: a third of the frame’s height', { Globe: u(1 / 3) }],
  ['Half mixed: the globe at 50 % over the clip', { Mix: 0.5 }],
];

/**
 * Settings by the plugin's own names as the kit's values: {units} goes
 * through the plugin's ParamFrom*, an option is its element's name, a bare
 * number the host's 0..1. Snow first, because Size's inverse depends on it.
 * (demo/tools/crosscheck.py uses it too.)
 */
function kitValues(spec, what = 'settings') {
  const values = {};
  const names = Object.keys(spec).sort((a, b) => (b === SNOW.name) - (a === SNOW.name));
  for (const name of names) {
    const value = spec[name];
    const p = byName(name);
    if (!p) throw new Error(`${what}: no control called ${name}`);
    if (p.type === 'option') {
      const slot = p.elements.indexOf(value);
      if (slot < 0) throw new Error(`${what}: ${name} has no element ${value}`);
      values[p.id] = slot;
    } else {
      const snow = snowHost({ ...DEFAULTS, ...values });
      const host = typeof value === 'object' ? paramFrom(p.ff, value.units, snow) : value;
      if (!Number.isFinite(host) || host < -1e-6 || host > 1 + 1e-6) throw new Error(`${what}: ${name} ${JSON.stringify(value)} is outside the control`);
      values[p.id] = Math.min(1, Math.max(0, host));
    }
  }
  return values;
}

function kitPresets() {
  const out = {};
  for (const [title, spec] of PRESET_UNITS) out[title] = kitValues(spec, `preset “${title}”`);
  return out;
}

//===========================================================================
// The renderer: one plugin instance, driven as a host drives it.
//===========================================================================

let renderer = null;
let mounted = null;

function createRenderer(gl) {
  // The plugin's target is RGBA32F and its scene pass blends into it (GL_ONE,
  // GL_ONE_MINUS_SRC_ALPHA). Rendering to a float target is the kit's
  // needFloat (EXT_color_buffer_float); blending into a 32-bit one is
  // EXT_float_blend in WebGL2, core in desktop GL. Without it the draw is an
  // error and the globe would come out wrong, so the page stops and says so.
  if (!gl.getExtension('EXT_float_blend')) {
    throw new GLError('This browser cannot blend into a 32-bit float target (EXT_float_blend is missing). SW Snowglobe draws its globe into an RGBA32F buffer with blending on, and without the extension that draw fails; the page will not show a different picture in its place.');
  }

  // emscripten's GL library, pointed at the kit's context. The kit has turned
  // on the two extensions the plugin needs; nothing else is turned on behind
  // its back.
  const handle = core.GL.registerContext(gl, { majorVersion: 2, minorVersion: 0, enableExtensionsByDefault: false });
  core.GL.makeContextCurrent(handle);

  let instance = 0;
  let pushed = new Map();
  let clipName = 0;
  let clipTexture = null;
  let frames = 0;
  let instances = 0;
  let lastTime = null;
  let epoch = -1;
  let source = null;
  // The visitor's file, fed as sgtest's Rig feeds a WAV: the absolute sample
  // clock at 48 kHz, samples [0, fileSent) gone to feed(), looped.
  let fileSent = 0;
  let scratch = 0;
  let scratchFrames = 0;

  /** A kit texture under a GL name the plugin can bind, in emscripten's tables. */
  function glName(texture) {
    if (texture === clipTexture) return clipName;
    if (clipName) core.GL.textures[clipName] = null;
    clipName = core.GL.getNewId(core.GL.textures);
    core.GL.textures[clipName] = texture;
    texture.name = clipName;
    clipTexture = texture;
    return clipName;
  }

  function start(width, height) {
    if (instance) core._sg_delete(instance);
    instance = core._sg_new();
    pushed = new Map();
    frames = 0;
    instances += 1;
    fileSent = 0;
    source = audio.file || audio.id !== 'file' ? audio.id : 'track';
    if (!core._sg_init_gl(instance, width, height)) {
      const why = shaderCheck.refused > 0
        ? 'the GLSL the compiled plugin handed to GL is not this page’s checked copy of source/Shaders.cpp, so it was not compiled'
        : `the plugin’s InitGL failed${printed.length ? `: ${printed.slice(-3).join(' ')}` : ''}`;
      throw new GLError(`SW Snowglobe could not start: ${why}.`);
    }
  }

  function push(params) {
    for (const p of PARAMS) {
      const value = hostValue(p.ff, params.get(p.id));
      if (value === null || pushed.get(p.id) === value) continue;
      pushed.set(p.id, value);
      core._sg_set_float(instance, p.ff.index, value);
    }
  }

  function feedFile(end) {
    const f = audio.file;
    const n = Math.min(Math.max(0, Math.round(end) - fileSent), 4 * FEED_RATE);
    if (n <= 0) return;
    if (n > scratchFrames) {
      if (scratch) core._free(scratch);
      scratchFrames = Math.max(n, FEED_RATE);
      scratch = core._malloc(scratchFrames * 2 * 4);
    }
    const out = core.HEAPF32.subarray(scratch >> 2, (scratch >> 2) + 2 * n);
    const length = f.mono.length;
    for (let k = 0; k < n; k += 1) {
      const sample = f.mono[(fileSent + k) % length];
      out[2 * k] = sample;
      out[2 * k + 1] = sample;
    }
    core._sg_feed(instance, scratch, n, FEED_RATE, 2);
    fileSent += n;
  }

  /** The audio for a frame at page time `time`, before the plugin renders it. */
  function feed(time) {
    if (source === 'file') feedFile(time * FEED_RATE);
    else if (source === 'track') core._sg_feed_track(instance, time);
    // 'silence': nothing, as sgtest --silent.
  }

  function process(time, input) {
    if (!core._sg_process(instance, time, glName(input.texture), input.width, input.height)) {
      throw new GLError('The plugin’s ProcessOpenGL failed.');
    }
  }

  /**
   * Five host frames about a millisecond apart, before the first one shown:
   * enough for the plugin's seconds-or-milliseconds vote (four frames that
   * agree against the wall clock) to settle, so a paused page stands still.
   * Nothing is fed during them and the bar phase stands still. Until the vote
   * settles the plugin steps on its own wall clock, so its clock ends a few
   * milliseconds ahead of the page's; the page's clock then steps back to
   * where it was, which the plugin takes as no time passing.
   */
  function prime(time, input) {
    const t0 = performance.now();
    const phase = host.phaseAt(time);
    for (let k = 0; k < 5; k += 1) {
      if (k > 0) {
        // The plugin needs 0.5 ms of wall clock between votes. Date.now() caps
        // the wait where performance.now() stands still (a test harness's
        // stepped clock).
        const until = performance.now() + 1.05;
        const cap = Date.now() + 20;
        while (performance.now() < until && Date.now() < cap) { /* wait */ }
      }
      core._sg_beat(instance, Math.fround(host.bpm), Math.fround(phase));
      process(time + (performance.now() - t0) / 1000, input);
    }
  }

  return {
    render({ input, params, width, height, time }) {
      core.GL.makeContextCurrent(handle);
      // A new instance for a change of audio, and for Restart (the page's
      // clock going back).
      const back = lastTime !== null && time < lastTime;
      const fresh = !instance || epoch !== audio.epoch || back;
      // Restart: the bar phase starts again from 0 with the clock.
      if (back) host.restart();
      lastTime = time;
      epoch = audio.epoch;
      if (fresh) start(width, height);
      push(params);
      if (fresh) prime(time, input);

      feed(time);
      core._sg_beat(instance, Math.fround(host.bpm), Math.fround(host.phaseAt(time)));
      process(time, input);

      // An event is one press: let go of it after this frame.
      for (const id of EVENTS) if (params.get(id) > 0.5) queueMicrotask(() => params.set(id, 0));

      // A setter WebGL2 refuses (a float sent to an int, say) fails silently
      // but for this: asked on an instance's first frames only, as a sync
      // point is not free. demo/tools/check_shaders.py checks the setters
      // statically.
      frames += 1;
      if (frames <= 3) {
        const error = gl.getError();
        if (error !== gl.NO_ERROR) {
          gl.canvas.dataset.glError = `0x${error.toString(16)}`;
          console.error(`WebGL reported error 0x${error.toString(16)} on frame ${frames} of SW Snowglobe`);
        }
      }
      // For anything checking the page from outside.
      gl.canvas.dataset.frames = String(frames);
      gl.canvas.dataset.instances = String(instances);
      gl.canvas.dataset.audio = source;
      gl.canvas.dataset.shakes = String(core._sg_shakes(instance, 0));
      gl.canvas.dataset.suspended = String(core._sg_frame(instance, 3));
    },
    stats() {
      if (!instance) return null;
      return {
        source,
        clockUs: core._sg_frame(instance, 0),
        drawn: core._sg_frame(instance, 1),
        behind: core._sg_frame(instance, 2),
        suspended: core._sg_frame(instance, 3),
        globePx: core._sg_frame(instance, 4),
        index: core._sg_frame(instance, 5),
        hash: core._sg_frame(instance, 6),
        shakes: core._sg_shakes(instance, 0),
        lastShakeUs: core._sg_shakes(instance, 1),
        lastStrength: core._sg_shakes(instance, 2),
        spin: core._sg_swirl(instance, 0),
        tau: core._sg_swirl(instance, 1),
        w: core._sg_physics(instance, 0),
        aMax: core._sg_physics(instance, 1),
        aMin: core._sg_physics(instance, 2),
        count: core._sg_physics(instance, 3),
        mu: core._sg_physics(instance, 4),
        n: core._sg_physics(instance, 5),
        earshot: ['none', 'feed()', 'the host FFT'][core._sg_audio(instance, 0)] ?? '?',
        audioTime: core._sg_audio(instance, 1),
        kickOnsets: core._sg_audio(instance, 2),
        fed: core._sg_fed(instance),
        bpm: host.bpm,
        frames,
        instances,
      };
    },
    log() {
      try {
        return core.FS.readFile(text(core._sg_log_path()), { encoding: 'utf8' });
      } catch {
        return '';
      }
    },
    shaders: shaderCheck,
  };
}

//===========================================================================
// What this page does not reproduce. The kit adds its four.
//===========================================================================

const DIFFERENCES = [
  'This page runs the plugin rather than a port of it. snowglobe-core.wasm is source/Snowglobe.cpp — the plugin class: its constructor, its clock and seconds-or-milliseconds vote, the kicks, the beat and the button, the instance buffer it uploads every frame, every uniform and the GL state it saves and puts back — and everything it calls (Globe, Fill, Optics, Audio, Controls, PassBuffer, Shaders, Diag), with earshot’s snapshot (8124191, unpatched) and the FFGL SDK’s CFFGLPlugin, parameter bookkeeping, FFGLShader and FFGLFBO, all compiled unmodified by emscripten (demo/tools/build-wasm.sh). The flakes and the swirl — RK4 in double on the 8 ms grid, the fill’s tables, the drag laws, the projection through the glass — are that C++. Only the registration (EffectPlugin.cpp) is left out; the page constructs the plugin itself, as its harness does. The panel is read back from the plugin’s own declarations through the SDK’s host getters, and the numbers beside the sliders are Controls.cpp’s conversions, compiled in; their units, every tooltip and the presets are this page’s.',
  'No audio device, and no microphone: the page never asks for one, never lists one and makes no sound. earshot is compiled here with no capture backend at all (EARSHOT_CAPTURE=OFF: its NoCapture.cpp, and miniaudio is not compiled), and the plugin is told not to ask for a device list (SNOWGLOBE_NO_DEVICES=1, its verification switch). So two of the plugin’s controls are not on this panel: Audio Input (the device menu: Resolume FFT, System default or a device by name) and the Audio buffer (Resolume’s FFT, which a host writes and nobody sets). The live-device path is not in the browser.',
  'The audio is the plugin’s harness’s, handed over as its harness hands it: through earshot’s feed() (AudioForTest().Feed), the path a device’s samples take inside earshot, so Kick reads the kicks off earshot’s own analysis of the waveform. The track is sgtest’s own (a kick on each beat at 120 BPM, a hat on the off-beat), generated in the WebAssembly by sgtest’s function, copied character for character and held to tools/sgtest by the repository’s verify script, and fed up to the page’s clock each frame at 48 kHz, the same on both channels, by the harness’s own lines. “Nothing” is sgtest --silent: nothing fed, and the plugin listens to Resolume’s FFT buffer, which is empty here, so Kick never fires. Your own audio file is decoded by this browser (Web Audio’s decodeAudioData, in an OfflineAudioContext, resampled to 48 kHz), its first two channels averaged as sgtest --wav averages a WAV’s, never uploaded, and fed the same way, looped; sgtest --wav plays a file once, at the file’s own rate, and then feeds silence. Nothing is played aloud.',
  'Resolume’s FFT, the plugin’s default Audio Input, is not reproduced: in Resolume the kicks would come from earshot’s host fallback on Resolume’s FFT bins, and in v0.1.0 they never come: the kick band, 40–160 Hz, lies below the lowest of Resolume’s 64 bins as earshot reads them (centred at 172 Hz), so Kick never fired in Arena. Here they come from the waveform through feed(), as they would from a capture device.',
  'Every change of audio is a new instance from time 0, as Restart is: a feed, once begun, is what earshot reads until a device is selected, and the page never selects one. The page’s default audio is the track, as if a device were chosen; the plugin’s default, with nothing routed, is Resolume’s FFT.',
  'The tempo and the bar phase are the page’s: Resolume sends its own every frame. The tempo is chosen in the host row under the picture (120 BPM at first); the bar phase runs on the page’s clock in 4/4, continuous across a tempo change, and stands still while paused, so a paused page shakes only on Shake Now. Trigger On Beat follows it.',
  'Shake Now is FF_TYPE_EVENT in the plugin, acted on at the press. The kit has no event control, so it is a toggle the page lets go of after one frame: one press, the rising edge the plugin counts (two presses inside one frame count once here). The Shake Now button in the host row presses the same control, the way a MIDI pad mapped to it would. Pressed while paused, the shake is taken at that instant and shows when the clock runs (Play or Step).',
  'The GLSL is the plugin’s: demo/shaders.js is spliced from source/Shaders.cpp, with the order AssembleVertex() and AssembleFragment() join the pieces in for each of its three programs (the scene, the flakes, the composite), and the repository’s verify script fails if a character differs, or if a uniform is set by a call its GLSL type does not take. At start-up the page also requires every stage the compiled plugin hands to glShaderSource to equal its copy, and refuses to compile anything else. The kit’s port() then changes the version line and adds the ES precision defaults. GL is emscripten’s WebGL2 library on this page’s context, not a GL 4.1 driver, and four entry points are the page’s (demo/wasm/gl_shim.cpp): glShaderSource, for that check and port, and glEnable, glDisable and glIsEnabled, because GL_PROGRAM_POINT_SIZE does not exist in WebGL2; it reads as off here, and the plugin draws no points.',
  'The plugin draws its globe into an RGBA32F buffer with blending on. WebGL2 renders to a float target only with EXT_color_buffer_float and blends into a 32-bit one only with EXT_float_blend; a browser without both gets a message here, not a substitute picture.',
  'The clock is the page’s: seconds from frame deltas, capped at a tenth of a second a frame, handed to SetTime, and the audio is fed up to it. The plugin votes against the wall clock on whether those are seconds or milliseconds; so that a paused page stands still, each new instance — page load, a change of audio and Restart — is handed five frames about a millisecond apart before the first one shown, which settles the vote, with nothing fed during them. Until it settles the plugin steps on its own wall clock, so its clock runs a few milliseconds ahead of the page’s from then on: every shake and every frame moves by the same few milliseconds, and only where the 8 ms integration grid falls between them changes. That shows in one place: a flake comes to rest on the glass at the end of the 8 ms step it crossed in, so where it lies can differ from the harness’s by a few hundredths of a pixel, and a flake the next shake lifts starts from there (demo/tools/crosscheck.py, against sgtest: a mean of about a hundredth of an 8-bit code or less, at flake edges). A host’s vote in Resolume moves the grid the same way. Paused, a moved control redraws the frame without feeding or moving the snow. Step advances the clock 1/60 s.',
  'A frame here is one of the display’s refreshes (60 or 120 a second, and whenever the browser gets round to it), not Resolume’s output frame. The snow does not depend on it: every shake is placed at its own instant inside the frame, and sgtest measures 24, 60 and 144 fps giving the same flurry, bit for bit.',
  'WebAssembly is not the Mac’s arm64. It has no fused multiply-add, and its sin, cos, exp and pow are musl’s, not Apple’s, so the flakes’ doubles can differ from the plugin built for a Mac in the last digits. What reaches the GPU is them as float, as in the plugin, rasterised by this browser’s GPU, not the Mac’s.',
  'The output carries alpha: outside the globe it is transparent, and so is the globe wherever the clip is. In Resolume you put a layer behind it; here the Behind menu puts the kit’s checkerboard, black or white there instead. The plugin writes straight colour and this page’s canvas composites what it gets as premultiplied, so a flake’s soft edge, or the globe’s, over a transparent part of the frame shows a little lighter here than in Resolume.',
  'The clip is the kit’s generated one, or your own image or video. The kit hands over premultiplied colour, and the plugin treats its input as straight colour and premultiplies it itself before the glass resamples it; the page’s clips are opaque but for the shape on transparency, where a half-transparent edge is weighted by alpha twice here. Which of the two Resolume hands an effect has not been measured. In Arena the effect gets the composition’s own frame; here its whole texture is used.',
  'The host row, the lines under the picture and the Presets menu are the page’s, not the plugin’s; the presets are whole states on top of the plugin’s defaults. The About block (a credit line and its buttons) is declared by the plugin and left off this panel.',
  'Nothing here is measured. The plugin has run in Resolume only briefly, where Beat and the button shake it but Kick on Resolume’s FFT never fired, and has never opened a live audio device. Its harness, sgtest, drives the same class headlessly on a synthetic clock: single flakes fall at the speed the glycerine tables and the drag of a disc (or Stokes’s law for a bead) give them, to within 2.3 %; the swirl dies at the rate the sphere’s slowest mode gives, to within 2.7 %; the clip lands where an independent ray trace puts it, to 1.2e-4 px; and 27 wrong models and 46 one-character mutants are caught. That harness, not this page, is the reason to believe the snow. The globe’s size, the shake’s strength and how high it lifts the snow are chosen, not measured.',
];

//===========================================================================
// The page.
//===========================================================================

mounted = mountDemo({
  name: 'Snowglobe',
  pluginId: 'SG01',
  kind: 'effect',
  tagline:
    'A snow globe for Resolume: your clip is the scene standing inside a glass globe of water and glycerine, and the music shakes it. A kick lifts the snow off the glass and sets the fill turning; the flakes fall back at the speed their size and the fill’s viscosity give them, big flakes faster, gliding at their tilts, and settle over tens of seconds, while the glass magnifies the middle of the clip and squeezes its rim.',
  repo: 'https://github.com/stoatworks-labs/snowglobe',

  blurb:
    'It is the plugin’s own C++ — the plugin class, the flakes and the swirl stepped in double, the fill’s tables, the optics, its audio input and earshot under it — compiled unmodified to WebAssembly, drawing with its own GLSL in WebGL2. The host is this page: the audio is the plugin’s harness’s own track, or a file of yours, fed through earshot’s feed() as a device’s samples would be, never a microphone; the beat is the page’s clock; the clip is a generated one.',

  needFloat: true,
  // Transparent outside the globe: in Resolume, the layers below show there.
  showBackdrop: true,
  sources: ['scene', 'bars', 'grid', 'detail', 'spot', 'ramp', 'alpha'],

  params: PARAMS,
  presets: kitPresets(),
  differences: DIFFERENCES,

  createRenderer: (gl) => {
    renderer = createRenderer(gl);
    return renderer;
  },
});

//===========================================================================
// The host row and the lines under the picture.
//===========================================================================

if (mounted) {
  const params = mounted.params;
  liveParams = params;
  // Size's read-out depends on Snow: redraw the read-outs when Snow changes,
  // and once now (the panel was built before liveParams was set).
  params.addEventListener('change', (e) => {
    if (e.detail?.id === addressOf(SNOW.name)) params.dispatchEvent(new CustomEvent('reset'));
  });
  params.dispatchEvent(new CustomEvent('reset'));

  // Copy link carries the audio choice and the tempo (a file cannot travel in a link).
  const toQuery = params.toQuery.bind(params);
  params.toQuery = () => {
    const q = toQuery();
    for (const id of EVENTS) q.delete(id);
    if (audio.id !== 'track' && audio.id !== 'file') q.set('audio', audio.id);
    if (host.bpm !== 120) q.set('tempo', String(host.bpm));
    return q;
  };

  if (!embed) {
    // The kit's note says every declared parameter is here; three are not.
    const note = document.querySelector('.inspector__note');
    if (note) {
      note.textContent += ' Left out here: Audio Input (no audio device exists in the browser), the Audio buffer (Resolume’s FFT; the host row under the picture stands in for both) and the About block. Shake Now is a press: the page lets go of it after one frame.';
    }
    buildHostRow();
    buildStatusLine();
  }
}

//---------------------------------------------------------------------------
// The host row: the stand-in for what Resolume would hand the plugin.
//---------------------------------------------------------------------------
function field(label, control, title) {
  const wrap = document.createElement('label');
  wrap.className = 'transport__field';
  if (title) wrap.title = title;
  const span = document.createElement('span');
  span.className = 'transport__label';
  span.textContent = label;
  // Narrow screens: the field may shrink, and the control with it.
  wrap.style.maxWidth = '100%';
  wrap.style.minWidth = '0';
  control.style.maxWidth = '100%';
  control.style.minWidth = '0';
  wrap.append(span, control);
  return wrap;
}

/** Restart the page's clock: a new instance from time 0, as Restart does. */
function restartClock() {
  audio.epoch += 1;
  mounted.state.time = 0;
  mounted.state.frameIndex = 0;
  host.restart();
  mounted.redraw();
}

function buildHostRow() {
  const transport = document.querySelector('.transport');
  if (!transport) return;
  const row = document.createElement('div');
  row.className = 'transport';
  row.id = 'snowglobe-host';
  const tag = document.createElement('span');
  tag.className = 'transport__label';
  tag.textContent = 'The host:';
  tag.title = 'The stand-in for what Resolume would hand the plugin. Not a plugin control.';

  const select = document.createElement('select');
  select.className = 'transport__select';
  select.id = 'snowglobe-audio';
  for (const a of AUDIO) {
    const option = new Option(a.name, a.id);
    option.title = a.hint;
    if (a.id === 'file') option.disabled = true;
    select.append(option);
  }
  select.value = audio.id;
  select.addEventListener('change', () => {
    audio.id = select.value;
    restartClock();
  });

  const pick = document.createElement('label');
  pick.className = 'btn transport__file';
  pick.id = 'snowglobe-audio-file-label';
  pick.textContent = 'Audio file…';
  pick.title = 'Choose a sound file on this machine. The browser decodes it here and the page feeds it to the plugin through feed(); it is not uploaded and not played aloud.';
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = 'audio/*';
  input.id = 'snowglobe-audio-file';
  input.hidden = true;
  pick.append(input);
  const note = document.createElement('span');
  note.className = 'transport__label';
  note.id = 'snowglobe-audio-note';
  note.setAttribute('role', 'status');
  input.addEventListener('change', async () => {
    const file = input.files?.[0];
    if (!file) return;
    note.textContent = `Decoding ${file.name}…`;
    try {
      audio.file = await decodeFile(file);
      const fileOption = [...select.options].find((o) => o.value === 'file');
      fileOption.disabled = false;
      fileOption.textContent = `Your file: ${file.name}`;
      audio.id = 'file';
      select.value = 'file';
      note.textContent = `${file.name}: ${audio.file.seconds.toFixed(1)} s, ${audio.file.channels === 1 ? 'mono' : `${audio.file.channels} channels, the first two averaged`}, at 48 kHz, looped. It stays in this page.`;
      restartClock();
    } catch (error) {
      note.textContent = `That file could not be decoded: ${error.message ?? error}`;
    }
  });

  const tempo = document.createElement('select');
  tempo.className = 'transport__select';
  tempo.id = 'snowglobe-tempo';
  for (const bpm of TEMPOS) tempo.append(new Option(`${bpm} BPM`, String(bpm)));
  tempo.value = String(host.bpm);
  tempo.addEventListener('change', () => {
    host.setTempo(Number(tempo.value), mounted.state.time);
    mounted.redraw();
  });

  const shake = document.createElement('button');
  shake.type = 'button';
  shake.className = 'btn';
  shake.id = 'snowglobe-shake';
  shake.textContent = 'Shake Now';
  shake.title = 'Presses the plugin’s own Shake Now: one full shake, under every Trigger On.';
  shake.addEventListener('click', () => mounted.params.set(SHAKE_NOW_ID, 1));

  row.append(
    tag,
    field('Audio', select, 'What reaches the plugin in place of an audio input. The plugin’s Audio Input menu and the live-device path are not in the browser: the page feeds earshot’s feed() as the plugin’s harness does. Changing it restarts the clock (a new instance).'),
    pick,
    field('Tempo', tempo, 'Resolume’s BPM. The bar phase runs at it, in 4/4, on the page’s clock, and Trigger On Beat shakes on each quarter.'),
    shake,
    note,
  );
  transport.after(row);
}

//---------------------------------------------------------------------------
// The lines under the picture: the plugin's own state, through the accessors
// its harness reads, and what the trigger will do. They report; they measure
// nothing.
//---------------------------------------------------------------------------
function buildStatusLine() {
  const stage = document.querySelector('.stage');
  if (!stage) return;
  const line = document.createElement('p');
  line.className = 'stage__status sg-status';
  line.id = 'snowglobe-status';
  line.setAttribute('aria-live', 'off');
  const trigger = document.createElement('p');
  trigger.className = 'stage__status sg-note';
  trigger.id = 'snowglobe-trigger';
  trigger.setAttribute('aria-live', 'polite');
  const alpha = document.createElement('p');
  alpha.className = 'stage__status sg-note';
  alpha.id = 'snowglobe-alpha';
  alpha.textContent = 'Outside the globe the output is transparent, and so is the globe wherever the clip is: in Resolume you would put a layer behind it. Here Behind (in the row above) chooses what shows there.';
  const log = document.createElement('p');
  log.className = 'stage__status sg-log';
  log.id = 'snowglobe-log';
  log.setAttribute('aria-live', 'off');
  stage.append(line, trigger, alpha, log);

  const params = mounted.params;
  const triggerName = () => TRIGGER.elements[Math.round(params.get(addressOf(TRIGGER.name)))];

  let lastLine = '';
  let lastTrigger = '';
  let lastLog = '';
  setInterval(() => {
    const s = renderer?.stats();
    if (!s) return;
    const mm = (m) => `${fixed(m * 1000, 2)} mm`;
    const last = s.lastShakeUs < 0 ? 'none yet' : `the last at ${fixed(s.lastShakeUs / 1e6, 2)} s, S ${fixed(s.lastStrength, 2)}`;
    const feedText = s.source === 'file' ? `your file through feed(), ${fixed(s.fed / FEED_RATE, 2)} s so far`
      : s.source === 'track' ? `sgtest’s track through feed(), ${fixed(s.fed / FEED_RATE, 2)} s so far`
        : 'nothing fed';
    const next = `${s.drawn.toLocaleString('en-GB')} flakes drawn (${s.behind.toLocaleString('en-GB')} behind the card), ${s.suspended.toLocaleString('en-GB')} not at rest`
      + ` · ${s.shakes.toLocaleString('en-GB')} shake${s.shakes === 1 ? '' : 's'} (${last}) · the swirl at ${fixed(s.spin, 3)} rad/s, dying as e^(−t/${fixed(s.tau, 1)} s)`
      + ` · ${fixed(s.w * 100, 1)} % glycerol: ${fixed(s.mu, 1)} mPa·s, n ${fixed(s.n, 4)}; flakes ${mm(s.aMin)}–${mm(s.aMax)}`
      + ` · ${feedText}, earshot reading ${s.earshot}, ${s.kickOnsets.toLocaleString('en-GB')} kick-band onsets`
      + ` · the plugin’s clock at ${fixed(s.clockUs / 1e6, 3)} s`;
    if (next !== lastLine) {
      lastLine = next;
      line.textContent = next;
    }

    const mode = triggerName();
    const paused = mounted.state.playing ? '' : ' Paused: the clock stands still, and a press of Shake Now shows when it runs again.';
    let says;
    if (mode === 'Kick') {
      says = s.source === 'silence'
        ? 'Trigger On is Kick, the plugin’s default, and nothing is fed: the plugin listens to Resolume’s FFT, which is empty here, so no kick ever shakes it. Choose the track or a file in the host row, set Trigger On to Beat, or press Shake Now.'
        : `Trigger On is Kick: each kick earshot finds in ${s.source === 'file' ? 'your file' : 'the track (one every half second)'} shakes the globe at S ${fixed(convert(declared('Shake'), params.get('shake')), 2)}; kicks within 50 ms are one shake.`;
    } else if (mode === 'Beat') {
      says = `Trigger On is Beat: a shake on every quarter of the bar at ${s.bpm} BPM, ${fixed(s.bpm / 60, 2)} a second, at S ${fixed(convert(declared('Shake'), params.get('shake')), 2)}.`;
    } else {
      says = 'Trigger On is Button: only Shake Now shakes the globe.';
    }
    says += paused;
    if (says !== lastTrigger) {
      lastTrigger = says;
      trigger.textContent = says;
    }

    const textLog = renderer.log();
    if (textLog === lastLog) return;
    lastLog = textLog;
    const lines = textLog.trim().split('\n').filter(Boolean).map((l) => l.replace(/^\S+ (INFO |WARN |ERROR) snowglobe: /, '$1 '));
    log.textContent = lines.length ? `The plugin’s own log (Diag.cpp, in this page’s memory):\n${lines.slice(-2).join('\n')}` : '';
  }, 250);
}

// For checking the page from outside (the crosscheck against sgtest reads the
// declarations and the inverse conversions through these).
export { renderer, mounted, PARAMS, DECLARED, DEFAULTS, core, hostValue, kitValues, audio, host, restartClock };
