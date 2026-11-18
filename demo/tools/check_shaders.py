"""The demo's copies of the plugin must be the plugin, character for character.

    python3 demo/tools/check_shaders.py           compare; exit 1 on any drift
    python3 demo/tools/check_shaders.py --write   regenerate demo/shaders.js

Called from `tools/verify.sh`. Exit code 1 means a copy has drifted.
(woofer's script, which is mirrorball's, pinhole's and fountain's, with
snowglobe's six pieces, its three programs, its `uniform( shader, "Name", v )`
and `glUniform*( where( shader, "Name" ), ... )` calls, sgtest's track and
earshot's sources.)

------------------------------------------------------------------- why

The page at snowglobe-demo.stoatworks-labs.com carries copies of this
repository, and copies drift quietly: a globe refracted with the wrong sign,
or a track whose kick sits an octave up, still looks like a snow globe.

1. **The GLSL.** `demo/shaders.js` holds the six GLSL pieces of
   `source/Shaders.cpp` (kQuadVertex, kCommon, kSceneFragment, kFlakeVertex,
   kFlakeFragment, kCompositeFragment) plus kVersion, AND the order the plugin
   joins them in for each of its three programs: Shaders.cpp's
   AssembleVertex( Pass ) and AssembleFragment( Pass ), for every pass in
   Snowglobe.cpp's InitGL stage table. This script reads that order out of the
   C++ and writes it into shaders.js as `ASSEMBLY`. The page builds its
   expected text for every stage from that table, and at run time REQUIRES the
   text the compiled plugin hands to glShaderSource to equal one of them before
   the kit's port() is applied (demo/wasm/gl_shim.cpp). The pieces are compared
   exactly -- no whitespace normalisation, no comment stripping: a comment
   updated on one side only is drift worth catching. Every
   `const char* const k... = R"(` piece in Shaders.cpp must be one this script
   knows, and shaders.js must be byte for byte what `--write` produces, so
   nothing else can be typed into it.

   The one transformation is a decode: a backtick cannot sit raw in a template
   literal, so shaders.js would escape it as \\`. Any other backslash, or a
   `${`, on the JS side is rejected; the C++ bodies hold neither.

2. **Every uniform's setter.** The page runs the plugin's own glUniform calls,
   and WebGL2 is stricter about them than a desktop driver is allowed to be
   lenient: a float sent to an `int` or a sampler is INVALID_OPERATION and the
   uniform stays 0, silently, and a sampler nobody sets sits on unit 0.
   Snowglobe.cpp sets every uniform either as
   `glUniformNx( where( shader, "Name" ), ... )` or through its two
   `uniform( shader, "Name", v )` overloads (int -> glUniform1i, float ->
   glUniform1f; the overload is read off the argument, and an argument this
   script cannot type fails it). Each uniform a program declares must be set
   on that program by the call its type takes, and nothing set that the
   program does not declare.

3. **sgtest's track.** The page feeds the plugin sgtest's own music through
   earshot's feed(). demo/wasm/glue.cpp carries tools/sgtest/main.cpp's
   `musicAt` and its `kRate`, which must be sgtest's text character for
   character, and the Rig's Render lines that feed it (from `const int from`
   to `fedUntil = t;`), which must appear in the glue in the same order, each
   line the same once its indentation is stripped.

4. **No device.** No file the page serves may name getUserMedia,
   enumerateDevices or mediaDevices: the page never asks for a microphone or
   lists an audio device, and this keeps it so.

5. **The WebAssembly.** `demo/snowglobe-core.wasm` is the plugin's C++ and
   earshot's compiled by `demo/tools/build-wasm.sh`, and committed, because the
   deploy has no build step. Building it needs emscripten, which verify.sh
   cannot assume, so this checks the next best thing: `demo/wasm/inputs.sha256`,
   written by the build, records the hash of every file that went in (every
   source/*.h, the nine plugin .cpp files, earshot's sources and headers, the
   SDK files, the glue, the shim and the build script), of both outputs, and the
   SDK commit. A source file changed since the last build -- a regenerated
   StoatworksAbout.h or a new earshot snapshot included -- or an output that is
   not the one the build wrote fails here, and the fix is to rerun
   build-wasm.sh.

------------------------------------------------------------------- what it cannot

It cannot tell whether the .wasm really is what those inputs compile to (only
rebuilding can), and it says nothing about demo/plugin.js beyond rule 4: the
panel is built from the plugin's own declarations at run time, but its read-out
units, its tooltips, its presets and its layout are the page's.
"""
import hashlib
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.dont_write_bytecode = True

# JS constant, C++ symbol (all in source/Shaders.cpp), in the order they are
# declared there. kVersion is a plain string and is handled on its own.
SHADERS = [
    ("QUAD_VERTEX", "kQuadVertex"),
    ("COMMON", "kCommon"),
    ("SCENE_FRAGMENT", "kSceneFragment"),
    ("FLAKE_VERTEX", "kFlakeVertex"),
    ("FLAKE_FRAGMENT", "kFlakeFragment"),
    ("COMPOSITE_FRAGMENT", "kCompositeFragment"),
]
JS_NAME = dict((symbol, name) for name, symbol in SHADERS)
JS_NAME["kVersion"] = "VERSION"

# The setters each GLSL uniform type takes, as Snowglobe.cpp spells them.
SETTER = {
    "sampler2D": {"glUniform1i"},
    "int": {"glUniform1i"},
    "bool": {"glUniform1i"},
    "ivec2": {"glUniform2i"},
    "ivec4": {"glUniform4i"},
    "float": {"glUniform1f"},
    "vec2": {"glUniform2f", "glUniform2fv"},
    "vec3": {"glUniform3f", "glUniform3fv"},
    "vec4": {"glUniform4f", "glUniform4fv"},
}

# The two uniform() overloads, as Snowglobe.cpp defines them.
OVERLOADS = [
    ("void SnowglobePlugin::uniform( const FFGLShader& shader, const char* name, int v )",
     "glUniform1i( where( shader, name ), v );", "glUniform1i"),
    ("void SnowglobePlugin::uniform( const FFGLShader& shader, const char* name, float v )",
     "glUniform1f( where( shader, name ), v );", "glUniform1f"),
]

# sgtest's track, copied into the glue.
HARNESS = ["musicAt"]
HARNESS_LINES = ["constexpr int kRate = 48000;"]
# The Rig's Render: the lines that feed the track, first and last.
FEED_FIRST = "const int from = static_cast< int >( std::llround( fedUntil * audioRate ) );"
FEED_LAST = "fedUntil = t;"

# What a page that never touches a device must never name.
DEVICE_APIS = ("getUserMedia", "enumerateDevices", "mediaDevices")

HEADER = """// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape is \\` for a backtick inside a comment.
"""

MANIFEST = os.path.join("demo", "wasm", "inputs.sha256")
OUTPUTS = ("demo/snowglobe-core.js", "demo/snowglobe-core.wasm")
REGISTRATION = ("EffectPlugin.cpp",)
# earshot's device backend, which build-wasm.sh leaves out (EARSHOT_CAPTURE=OFF).
NOT_COMPILED = ("Miniaudio.cpp", "MiniaudioImpl.cpp")


class Stale(Exception):
    """The C++ is not shaped the way this script reads it: fail, never guess."""


def read(*parts):
    with open(os.path.join(REPO, *parts)) as handle:
        return handle.read()


def cpp_version(source):
    match = re.search(r'const char\* const kVersion = "(.*?)";', source)
    return None if match is None else match.group(1)


def from_cpp(source, symbol):
    match = re.search(r'const char\* const ' + symbol + r' = R"\((.*?)\)";', source, re.S)
    return None if match is None else match.group(1)


def from_js(source, name):
    match = re.search(r'^export const ' + name + r' = `(.*?)`;$', source, re.S | re.M)
    if match is None:
        return None, None
    body = match.group(1)
    stray = re.search(r"\\(?!`)", body)
    if stray is not None:
        line = body[: stray.start()].count("\n") + 1
        return None, f"backslash that is not an escaped backtick, at line {line}"
    if "${" in body:
        return None, "template substitution"
    return body.replace("\\`", "`"), None


def unknown_pieces(cpp):
    """Raw-string pieces in Shaders.cpp that this script does not copy."""
    declared = re.findall(r'const char\* const (k\w+) = R"\(', cpp)
    return [symbol for symbol in declared if symbol not in JS_NAME]


def pieces_of(expression, where):
    """`std::string( kVersion ) + kA + kB` -> [kVersion, kA, kB]."""
    pieces = []
    for term in (t.strip() for t in expression.split("+")):
        m = re.fullmatch(r"(?:std::string\(\s*)?(?:shaders::)?(k\w+)(?:\s*\))?", term)
        if m is None:
            raise Stale(f"{where} joins `{term}`, which is not a piece")
        if m.group(1) not in JS_NAME:
            raise Stale(f"{where} uses {m.group(1)}, a piece this script does not copy")
        pieces.append(m.group(1))
    return pieces


def function_body(cpp, signature):
    body = re.search(re.escape(signature) + r"\s*\{(.*?)\n\}", cpp, re.S)
    if body is None:
        raise Stale(f"no `{signature}` in source/Shaders.cpp")
    return body.group(1)


def passes_of(header):
    """The enum class Pass's members, in order, from source/Shaders.h."""
    m = re.search(r"enum class Pass\s*\{(.*?)\};", header, re.S)
    if m is None:
        raise Stale("no enum class Pass in source/Shaders.h")
    return [p.strip() for p in m.group(1).split(",") if p.strip()]


def stages_of(plugin_cpp):
    """InitGL's stage table: [(shader member, Pass, name)], checked to compile
    each stage once as AssembleVertex( stage.pass ) + AssembleFragment( stage.pass )."""
    for needed in ("const std::string vertex   = shaders::AssembleVertex( stage.pass );",
                   "const std::string fragment = shaders::AssembleFragment( stage.pass );",
                   "if( stage.shader->Compile( vertex.c_str(), fragment.c_str() ) )",
                   "for( const Stage& stage : stages )"):
        if needed not in plugin_cpp:
            raise Stale(f"Snowglobe.cpp's InitGL no longer has `{needed}`")
    if len(re.findall(r"\.Compile\(|->Compile\(", plugin_cpp)) != 1:
        raise Stale("Snowglobe.cpp compiles shaders outside its stage table; this script reads only the table")
    table = re.search(r"const Stage stages\[\] = \{(.*?)\n\t\};", plugin_cpp, re.S)
    if table is None:
        raise Stale("no `const Stage stages[] = { ... };` in Snowglobe.cpp's InitGL")
    rows = re.findall(r'\{\s*&(\w+),\s*shaders::Pass::(\w+),\s*"(\w+)"\s*\}', table.group(1))
    if len(rows) != table.group(1).count("{"):
        raise Stale("a row of InitGL's stage table is not `{ &shader, shaders::Pass::P, \"name\" }`")
    return rows


def assembly(cpp, header, plugin_cpp):
    """[(stage name, [JS pieces])] for every program InitGL compiles, read
    from AssembleFragment( Pass ) (a prefix, then one `case` per pass) and
    AssembleVertex( Pass ) (one pass's piece, or the other)."""
    passes = passes_of(header)
    stages = stages_of(plugin_cpp)
    if sorted(p for _, p, _ in stages) != sorted(passes):
        raise Stale(f"InitGL compiles passes {[p for _, p, _ in stages]}, Shaders.h declares {passes}")

    frag = function_body(cpp, "std::string AssembleFragment( Pass pass )")
    m = re.fullmatch(r"\s*const std::string text = (.*?);\s*switch\( pass \)\s*\{(.*?)\}\s*return text;\s*", frag, re.S)
    if m is None:
        raise Stale("AssembleFragment() is not `text = <pieces>; switch( pass ) { case ...: return text + <pieces>; } return text;`")
    prefix = pieces_of(m.group(1), "AssembleFragment()")
    cases = re.findall(r"case Pass::(\w+): return text \+ (.*?);", m.group(2))
    if len(cases) != m.group(2).count("case "):
        raise Stale("a case of AssembleFragment() is not `case Pass::P: return text + <pieces>;`")
    fragment = {p: prefix + pieces_of(e, f"AssembleFragment() for {p}") for p, e in cases}

    vert = function_body(cpp, "std::string AssembleVertex( Pass pass )")
    m = re.fullmatch(r"\s*return (.*?) \+ \( pass == Pass::(\w+) \? (k\w+) : (k\w+) \);\s*", vert, re.S)
    if m is None:
        raise Stale("AssembleVertex() is not `return <pieces> + ( pass == Pass::P ? kA : kB );`")
    head = pieces_of(m.group(1), "AssembleVertex()")
    vertex = {p: head + pieces_of(m.group(3) if p == m.group(2) else m.group(4), f"AssembleVertex() for {p}") for p in passes}

    out = []
    for _, p, name in stages:
        if p not in fragment:
            raise Stale(f"AssembleFragment() has no case for Pass::{p}, which InitGL compiles")
        out.append((f"{name}_vertex", [JS_NAME[s] for s in vertex[p]]))
        out.append((f"{name}_fragment", [JS_NAME[s] for s in fragment[p]]))
    return out


def render(cpp, header, plugin_cpp):
    """shaders.js as --write produces it."""
    version = cpp_version(cpp)
    if version is None:
        raise Stale("kVersion not found in source/Shaders.cpp")
    out = [HEADER]
    out.append(f"export const VERSION = '{version.replace(chr(92) + 'n', chr(92) + 'n')}';\n")
    for name, symbol in SHADERS:
        body = from_cpp(cpp, symbol)
        if body is None:
            raise Stale(f"{symbol} not found in source/Shaders.cpp")
        if "\\" in body or "${" in body:
            raise Stale(f"{symbol} holds a backslash or ${{; the escape scheme cannot carry it")
        out.append(f"\n// {symbol}, source/Shaders.cpp\nexport const {name} = `{body.replace('`', chr(92) + '`')}`;\n")
    out.append(
        "\n// Each stage's source, piece by piece, in the order the plugin joins them:\n"
        "// Shaders.cpp's AssembleVertex( Pass ) and AssembleFragment( Pass ), for\n"
        "// each of the three programs Snowglobe.cpp's InitGL compiles.\n"
        "export const ASSEMBLY = {\n"
    )
    for name, pieces in assembly(cpp, header, plugin_cpp):
        out.append(f"  {name}: [{', '.join(repr(p) for p in pieces)}],\n")
    out.append("};\n")
    return "".join(out)


def js_version(js):
    match = re.search(r"^export const VERSION = '(.*?)';$", js, re.M)
    return None if match is None else match.group(1)


def first_difference(a, b):
    left, right = a.splitlines(), b.splitlines()
    for i in range(max(len(left), len(right))):
        x = left[i] if i < len(left) else "<missing>"
        y = right[i] if i < len(right) else "<missing>"
        if x != y:
            return i + 1, x, y
    return None


def check_shaders(cpp, header, plugin_cpp, js):
    problems = 0

    for symbol in unknown_pieces(cpp):
        print(f"FAIL  {symbol} is a piece of source/Shaders.cpp this script does not copy -- add it to SHADERS")
        problems += 1

    version_cpp = cpp_version(cpp)
    if version_cpp is None or js_version(js) != version_cpp:
        print("FAIL  VERSION does not match kVersion")
        problems += 1
    else:
        print(f"ok    {'VERSION':<26} matches kVersion")

    for name, symbol in SHADERS:
        cpp_text = from_cpp(cpp, symbol)
        js_text, complaint = from_js(js, name)
        if cpp_text is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp")
            problems += 1
        elif complaint is not None:
            print(f"FAIL  {name} in demo/shaders.js has a {complaint}")
            problems += 1
        elif js_text is None:
            print(f"FAIL  {name} not found in demo/shaders.js")
            problems += 1
        elif cpp_text == js_text:
            print(f"ok    {name:<26} matches {symbol} ({len(cpp_text)} chars)")
        else:
            problems += 1
            print(f"FAIL  {name} has drifted from {symbol}")
            where = first_difference(cpp_text, js_text)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          C++: {where[1]}")
                print(f"          js : {where[2]}")

    try:
        table = assembly(cpp, header, plugin_cpp)
    except Stale as stale:
        print(f"FAIL  the plugin's assembly could not be read: {stale}")
        return problems + 1
    block = re.search(r"^export const ASSEMBLY = \{\n(.*?)^\};$", js, re.S | re.M)
    rows = {} if block is None else dict(re.findall(r"^  (\w+): \[(.*)\],$", block.group(1), re.M))
    for name, pieces in table:
        want = ", ".join(repr(p) for p in pieces)
        if rows.get(name) == want:
            print(f"ok    {'ASSEMBLY.' + name:<26} {' + '.join(pieces)}")
        else:
            print(f"FAIL  ASSEMBLY.{name} is [{rows.get(name, '<missing>')}], the plugin's is [{want}]")
            problems += 1
    for name in sorted(set(rows) - set(n for n, _ in table)):
        print(f"FAIL  ASSEMBLY.{name} is no stage of the plugin's")
        problems += 1

    try:
        expected = render(cpp, header, plugin_cpp)
    except Stale as stale:
        print(f"FAIL  {stale}")
        return problems + 1
    if expected != js:
        where = first_difference(expected, js)
        print("FAIL  demo/shaders.js is not exactly what --write produces")
        if where:
            print(f"        first difference at line {where[0]}")
            print(f"          want: {where[1]}")
            print(f"          have: {where[2]}")
        problems += 1
    return problems


#---------------------------------------------------------------------------
# The uniforms.
#---------------------------------------------------------------------------

def declared_uniforms(text):
    """{name: type} of every `uniform <type> <Name>;` in a program's source."""
    out = {}
    for kind, name in re.findall(r"^\s*uniform\s+(\w+)\s+(\w+)\s*;", text, re.M):
        if out.get(name, kind) != kind:
            raise Stale(f"uniform {name} is declared as {out[name]} and as {kind} in one program")
        out[name] = kind
    return out


def overload_of(argument, header):
    """Which uniform() overload a call's third argument picks."""
    argument = argument.strip()
    if re.fullmatch(r"\d+", argument):
        return "glUniform1i"
    if re.fullmatch(r"\w+", argument) and re.search(r"^\s*int " + argument + r"\s*=", header, re.M):
        return "glUniform1i"
    if argument.startswith("static_cast< float >("):
        return "glUniform1f"
    if re.fullmatch(r"std::clamp\( params\[ \w+ \], [0-9.]+f, [0-9.]+f \)", argument):
        return "glUniform1f"  # params is float[]
    raise Stale(f"cannot tell which uniform() overload `{argument}` picks")


def uniform_calls(plugin_cpp, plugin_h):
    """[(shader member, uniform, setter)] for every literal-named set."""
    for signature, body, _ in OVERLOADS:
        if signature not in plugin_cpp or body not in plugin_cpp:
            raise Stale(f"Snowglobe.cpp no longer defines `{signature}` as `{body}`")
    calls = []
    for setter, shader, name in re.findall(r"(glUniform\w+)\(\s*where\(\s*(\w+),\s*\"(\w+)\"\s*\)", plugin_cpp):
        calls.append((shader, name, setter))
    for shader, name, argument in re.findall(r"^\s*uniform\(\s*(\w+),\s*\"(\w+)\",\s*(.*?)\s*\);\s*$", plugin_cpp, re.M):
        calls.append((shader, name, overload_of(argument, plugin_h)))
    literal = len(re.findall(r"\bwhere\(\s*\w+,\s*\"", plugin_cpp)) + len(re.findall(r"\buniform\(\s*\w+,\s*\"", plugin_cpp))
    if literal != len(calls):
        raise Stale(f"Snowglobe.cpp names {literal} uniforms by literal and this script reads {len(calls)} of them")
    return calls


def check_uniforms(cpp, header, plugin_cpp, plugin_h):
    problems = 0
    try:
        table = dict(assembly(cpp, header, plugin_cpp))
        stages = stages_of(plugin_cpp)
        calls = uniform_calls(plugin_cpp, plugin_h)
    except Stale as stale:
        print(f"FAIL  the uniforms could not be read: {stale}")
        return 1
    pieces = {name: from_cpp(cpp, symbol) or "" for name, symbol in SHADERS}
    pieces["VERSION"] = cpp_version(cpp) or ""
    members = {member for member, _, _ in stages}
    for member, _, _ in calls:
        if member not in members:
            print(f"FAIL  a uniform is set on {member}, which is no program of InitGL's")
            problems += 1
    total = 0
    for member, _, name in stages:
        text = "".join(pieces[p] for p in table[f"{name}_vertex"] + table[f"{name}_fragment"])
        try:
            declared = declared_uniforms(text)
        except Stale as stale:
            print(f"FAIL  {stale}")
            problems += 1
            continue
        mine = [(n, s) for m, n, s in calls if m == member]
        names = [n for n, _ in mine]
        bad = 0
        for uniform in sorted(set(declared) - set(names)):
            print(f"FAIL  {name}: uniform {declared[uniform]} {uniform} is never set (a sampler would sit on unit 0)")
            bad += 1
        for uniform, setter in mine:
            if uniform not in declared:
                print(f"FAIL  {name}: {setter} sets {uniform}, which the program does not declare")
                bad += 1
            elif setter not in SETTER.get(declared[uniform], set()):
                print(f"FAIL  {name}: {uniform} is {declared[uniform]} and is set with {setter}, not {' or '.join(sorted(SETTER.get(declared[uniform], {'?'})))}")
                bad += 1
        for uniform in sorted({n for n in names if names.count(n) > 1}):
            print(f"FAIL  {name}: {uniform} is set in more than one place")
            bad += 1
        if bad == 0:
            print(f"ok    {'UNIFORMS ' + name:<26} all {len(declared)} set, each by its type's setter")
        problems += bad
        total += len(declared)
    if problems == 0:
        print(f"ok    {'UNIFORMS':<26} {total} across {len(stages)} programs ({len(calls)} calls)")
    return problems


#---------------------------------------------------------------------------
# sgtest's track, and no device.
#---------------------------------------------------------------------------

def function_text(source, name):
    """A top-level C++ function: from the line that declares `name(` at the
    start of a line (its return type first) to the first `}` at the start of
    a line after it."""
    match = re.search(r"^[A-Za-z_][\w:<>&* ]*\b" + name + r"\(.*?^\}$", source, re.S | re.M)
    return None if match is None else match.group(0)


def code_lines(text):
    """Stripped lines, braces and blank lines dropped."""
    return [line.strip() for line in text.splitlines() if line.strip() not in ("", "{", "}")]


def feed_block(lines, where):
    try:
        first = lines.index(FEED_FIRST)
        last = lines.index(FEED_LAST, first)
    except ValueError:
        raise Stale(f"{where} has no feed block from `{FEED_FIRST}` to `{FEED_LAST}`")
    return lines[first:last + 1]


def check_harness(main_cpp, glue):
    problems = 0
    for name in HARNESS:
        theirs = function_text(main_cpp, name)
        ours = function_text(glue, name)
        if theirs is None:
            print(f"FAIL  {name} not found in tools/sgtest/main.cpp")
            problems += 1
        elif ours is None:
            print(f"FAIL  {name} not found in demo/wasm/glue.cpp")
            problems += 1
        elif ours == theirs:
            print(f"ok    {name:<26} matches tools/sgtest/main.cpp's ({len(ours)} chars)")
        else:
            print(f"FAIL  {name} in demo/wasm/glue.cpp has drifted from tools/sgtest/main.cpp's")
            where = first_difference(theirs, ours)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          sgtest: {where[1]}")
                print(f"          glue  : {where[2]}")
            problems += 1
    sgtest_lines = {line.strip() for line in main_cpp.splitlines()}
    glue_lines = {line.strip() for line in glue.splitlines()}
    for line in HARNESS_LINES:
        missing = [where for where, lines in (("tools/sgtest/main.cpp", sgtest_lines), ("demo/wasm/glue.cpp", glue_lines)) if line not in lines]
        if missing:
            print(f"FAIL  `{line}` is not in {' or '.join(missing)}")
            problems += 1
    try:
        theirs = feed_block(code_lines(main_cpp), "tools/sgtest/main.cpp")
        ours = feed_block(code_lines(glue), "demo/wasm/glue.cpp")
    except Stale as stale:
        print(f"FAIL  {stale}")
        return problems + 1
    if theirs == ours:
        print(f"ok    {'FEED':<26} the Rig's {len(theirs)} feeding lines, in order, in the glue")
    else:
        for i, (a, b) in enumerate(zip(theirs + ["<missing>"] * len(ours), ours + ["<missing>"] * len(theirs))):
            if a != b:
                print(f"FAIL  the glue's feed differs from the Rig's at its line {i + 1}")
                print(f"          sgtest: {a}")
                print(f"          glue  : {b}")
                break
        problems += 1
    if problems == 0:
        print(f"ok    {'HARNESS LINES':<26} all {len(HARNESS_LINES)} in both (kRate)")
    return problems


def check_no_devices():
    problems = 0
    demo = os.path.join(REPO, "demo")
    served = []
    for root, dirs, files in os.walk(demo):
        rel = os.path.relpath(root, demo)
        if rel.split(os.sep)[0] in ("tools", "wasm"):
            continue  # not served (.assetsignore), and this file names them
        for name in files:
            if name.endswith((".js", ".html")):
                served.append(os.path.join(root, name))
    for path in sorted(served):
        with open(path, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        for api in DEVICE_APIS:
            if api in text:
                print(f"FAIL  {os.path.relpath(path, REPO)} names {api}: the page must never ask for or list an audio device")
                problems += 1
    if problems == 0:
        print(f"ok    {'NO DEVICES':<26} none of the {len(served)} served scripts and pages names {', '.join(DEVICE_APIS)}")
    return problems


#---------------------------------------------------------------------------
# The WebAssembly.
#---------------------------------------------------------------------------

def sha256(path):
    digest = hashlib.sha256()
    with open(os.path.join(REPO, path), "rb") as handle:
        for block in iter(lambda: handle.read(1 << 16), b""):
            digest.update(block)
    return digest.hexdigest()


def check_wasm():
    try:
        lines = read(MANIFEST).splitlines()
    except FileNotFoundError:
        print(f"FAIL  {MANIFEST} is missing -- run demo/tools/build-wasm.sh")
        return 1

    problems = 0
    entries = 0
    recorded = set()
    for line in lines:
        pin = re.match(r"^# FFGL SDK ([0-9a-f]{40})$", line)
        if pin:
            try:
                head = subprocess.run(["git", "-C", os.path.join(REPO, "external", "ffgl"), "rev-parse", "HEAD"],
                                      capture_output=True, text=True, check=True).stdout.strip()
            except (OSError, subprocess.CalledProcessError):
                print("FAIL  cannot read the FFGL SDK's commit -- is the submodule checked out?")
                problems += 1
                continue
            if head != pin.group(1):
                print(f"FAIL  the .wasm was built against FFGL SDK {pin.group(1)[:7]}, the submodule is at {head[:7]}")
                problems += 1
            continue
        if not line or line.startswith("#"):
            continue
        want, path = line.split(None, 1)
        recorded.add(path)
        entries += 1
        if not os.path.exists(os.path.join(REPO, path)):
            print(f"FAIL  {path} went into the .wasm and no longer exists")
            problems += 1
        elif sha256(path) != want:
            print(f"FAIL  {path} has changed since demo/snowglobe-core.wasm was built -- rerun demo/tools/build-wasm.sh")
            problems += 1

    # A source file added since the build is an input the build never saw.
    trees = [("source", REGISTRATION), (os.path.join("external", "earshot", "src"), NOT_COMPILED),
             (os.path.join("external", "earshot", "include", "earshot"), ())]
    for tree, skipped in trees:
        for name in sorted(os.listdir(os.path.join(REPO, tree))):
            if name in skipped:
                continue
            rel = f"{tree}/{name}".replace(os.sep, "/")
            if (name.endswith(".h") or name.endswith(".cpp")) and rel not in recorded:
                print(f"FAIL  {rel} is newer than the .wasm's manifest -- rebuild")
                problems += 1

    for output in OUTPUTS:
        if output not in recorded:
            print(f"FAIL  {MANIFEST} does not record {output}")
            problems += 1
        elif os.stat(os.path.join(REPO, output)).st_mode & 0o111:
            print(f"FAIL  {output} is executable: build-wasm.sh sets 644, and a served file needs no x bit")
            problems += 1
    if problems == 0:
        print(f"ok    {'WASM':<26} built from these sources ({entries} files hashed, SDK pin agrees)")
    return problems


def main(argv):
    cpp = read("source", "Shaders.cpp")
    header = read("source", "Shaders.h")
    plugin_cpp = read("source", "Snowglobe.cpp")
    plugin_h = read("source", "Snowglobe.h")
    if "--write" in argv:
        unknown = unknown_pieces(cpp)
        if unknown:
            print(f"FAIL  {', '.join(unknown)}: piece(s) this script does not copy -- add them to SHADERS")
            return 1
        try:
            text = render(cpp, header, plugin_cpp)
        except Stale as stale:
            print(f"FAIL  {stale}")
            return 1
        with open(os.path.join(REPO, "demo", "shaders.js"), "w") as handle:
            handle.write(text)
        print("wrote demo/shaders.js")
        return 0
    try:
        js = read("demo", "shaders.js")
    except FileNotFoundError:
        print("FAIL  demo/shaders.js is missing -- run demo/tools/check_shaders.py --write")
        return 1
    problems = check_shaders(cpp, header, plugin_cpp, js)
    problems += check_uniforms(cpp, header, plugin_cpp, plugin_h)
    problems += check_harness(read("tools", "sgtest", "main.cpp"), read("demo", "wasm", "glue.cpp"))
    problems += check_no_devices()
    problems += check_wasm()
    print()
    if problems:
        print(f"{problems} problem(s) -- rerun demo/tools/check_shaders.py --write for the GLSL and"
              " demo/tools/build-wasm.sh for the .wasm; never edit either output by hand")
        return 1
    print(f"all {len(SHADERS) + 1} shader pieces and the order all three programs join them in are the plugin's,"
          " every uniform is set by its type's setter, the glue's track and feed are sgtest's,"
          " no served file names a device API, and the .wasm is built from the current sources")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
