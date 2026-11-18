#!/usr/bin/env bash
#
# Everything, in the order that fails fastest.
#
# The build is universal on purpose. An arm64-only bundle builds and tests
# perfectly well here and then fails to load in an Intel Resolume, and the
# build log calls it a success either way -- so the architecture is checked
# with lipo, never with the log.
#
#     tools/verify.sh [BUILD_DIR]
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$REPO/build-verify}"

cd "$REPO"

step() { printf '\n\033[1m== %s\033[0m\n' "$1"; }
fail() { printf '\033[31mFAIL\033[0m %s\n' "$1"; exit 1; }

# No step here may open an audio device. The plugin, oxbow's copy of it and
# sgtest all read this: the plugin never lists the OS's devices and never
# opens or even looks one up (the lookup itself starts CoreAudio); sgtest
# feeds its audio through earshot's feed(). sgtest --coreaudio unsets it in
# its own children, under an interposer that refuses every audio call.
export SNOWGLOBE_NO_DEVICES=1

# Our own temporary directory, never the machine's shared one: other builds
# run alongside, and a heredoc hung on this Mac with the system's TMPDIR.
[[ -n "${TMPDIR:-}" && -d "$TMPDIR" && -w "$TMPDIR" ]] || fail "set TMPDIR to a writable directory of this run's own"

# A host has to read the bundle, so oxbow is required, not optional: a run
# without it would print "skipped" and call itself green.
OXBOW="${OXBOW:-}"
[[ -n "$OXBOW" && -x "$OXBOW" ]] || fail "set OXBOW to a built oxbow binary (cmake -S ~/Projects/resolume/oxbow -B <scratch>/oxbow-build, never inside ~/Projects)"

#---------------------------------------------------------------------------
# GLSL reserved words, declared as identifiers.
#
# glslc catches these, but glslc is optional -- so the trap most likely to be
# walked into gets its own grep that always runs: GLSL 4.10's words reserved
# for future use, plus the ones already bitten (`packed` failed only Mesa,
# atrac; noise1..noise4 shadow built-ins, dice; `half`).
#---------------------------------------------------------------------------
reserved_words() {
	local words="common partition active asm class union enum typedef template this resource goto inline noinline
	             public static extern external interface long short half fixed unsigned superp input output filter
	             sizeof cast namespace using packed sample patch layout flat smooth noperspective precise
	             noise1 noise2 noise3 noise4"
	local bad=0 word
	for word in $words; do
		if command grep -nE "(float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|mat[234])[[:space:]]+$word[[:space:]]*[;=,)(]" \
		            source/Shaders.cpp >/dev/null 2>&1; then
			printf '   "%s" is declared as an identifier and is a GLSL reserved word\n' "$word"
			bad=$(( bad + 1 ))
		fi
	done
	[ "$bad" -eq 0 ] && printf '   none of the reserved words is used as an identifier\n'
	return "$bad"
}

step "GLSL reserved words"
reserved_words || fail "a GLSL reserved word is used as an identifier"

#---------------------------------------------------------------------------
step "GLSL: nothing WebGL2 lacks"
#---------------------------------------------------------------------------
# The fleet's browser demos run the shaders unedited in WebGL2 (GLSL ES 3.00).
# Snowglobe has no demo yet; the rule is kept so one can be made without
# rewriting a shader.
if command grep -nE '(^|[^A-Za-z_])(findMSB|findLSB|bitCount|bitfieldExtract|bitfieldInsert|bitfieldReverse|uaddCarry|usubBorrow|umulExtended|imulExtended|frexp|ldexp|fma|textureGather|textureQueryLevels|packUnorm4x8|unpackUnorm4x8)[[:space:]]*\(' source/Shaders.cpp | command grep -vE '^[0-9]+:[[:space:]]*//' ; then
	fail "a built-in GLSL ES 3.00 does not have is in the GLSL"
fi
echo "   none"

#---------------------------------------------------------------------------
step "Shaders"
#---------------------------------------------------------------------------
tools/glslc.sh || fail "a shader does not compile"

#---------------------------------------------------------------------------
step "Submodule and snapshot"
#---------------------------------------------------------------------------
if [[ ! -f external/ffgl/CMakeLists.txt ]]; then
	fail "FFGL SDK missing -- run: git submodule update --init --recursive"
fi
pin="$(git -C external/ffgl rev-parse --short=7 HEAD)"
[[ "$pin" == "b1afaf9" ]] || fail "FFGL SDK at $pin, not the fleet's b1afaf9"
echo "ok   FFGL SDK pinned at $pin"
# earshot is a snapshot, not a submodule: the commit it came from and that it
# is unpatched are written in it, and AGENTS.md repeats both.
command grep -q '^Commit:   8124191 ' external/earshot/SNAPSHOT || fail "external/earshot/SNAPSHOT is not earshot 8124191"
command grep -q '^Patches:  none' external/earshot/SNAPSHOT || fail "external/earshot/SNAPSHOT records a patch: AGENTS.md must say what and why"
echo "ok   earshot snapshot 8124191, unpatched"
# The workflows: the repo is public (github.com/stoatworks-labs/snowglobe), so
# it carries CI and the release build. Both must load the DLL the way a host
# does, with the parameter count the plugin really declares (wincheck's
# -Params moves with every control and with the About block).
for wf in ci release; do
	[[ -f ".github/workflows/$wf.yml" ]] || fail ".github/workflows/$wf.yml is missing"
done
echo "ok   ci.yml and release.yml present"

#---------------------------------------------------------------------------
step "Build (universal)"
#---------------------------------------------------------------------------
# Fresh unless KEEP_BUILD=1: an incremental build recompiles only what changed,
# so the warning check below would see nothing from the files it skipped.
if [[ "${KEEP_BUILD:-0}" != 1 ]]; then
	[[ -n "$BUILD" && "$BUILD" != "$REPO" && "$BUILD" != "/" ]] || fail "refusing to clear build directory '$BUILD'"
	rm -rf "$BUILD"
fi
log="$( mktemp )"
cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >"$log" 2>&1 || { cat "$log"; fail "configure"; }
cmake --build "$BUILD" -j8 >"$log" 2>&1 || { tail -40 "$log"; fail "build"; }
# The SDK's own FFGLUtilities.cpp warns about RAND_MAX, and earshot's
# miniaudio is earshot's; anything else is ours.
if command grep -E 'warning:' "$log" | command grep -v 'external/' | command grep -q .; then
	command grep -E 'warning:' "$log" | command grep -v 'external/' | sed 's/^/   /'
	fail "a warning in this repo's code"
fi
rm -f "$log"
echo "ok   built, no warnings outside external/"
SGTEST="$BUILD/sgtest"

#---------------------------------------------------------------------------
step "Bundle"
#---------------------------------------------------------------------------
declared="$( sed -n 's/^[[:space:]]*VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | /usr/bin/head -1 )"
command grep -q "versionFallback = \"v$declared\"" source/StoatworksAbout.h || fail "StoatworksAbout.h's versionFallback is not v$declared"
check_bundle() {
	local name="$1" id="$2" display="$3" ffid="$4" kind="$5"
	local bundle="$BUILD/$name.bundle" binary="$BUILD/$name.bundle/Contents/MacOS/$name"
	[[ -f "$binary" ]] || fail "no binary at $binary"
	local arches; arches="$(lipo -archs "$binary")"
	[[ "$arches" == *arm64* ]]  || fail "$name: no arm64 slice (got: $arches)"
	[[ "$arches" == *x86_64* ]] || fail "$name: no x86_64 slice (got: $arches)"
	# Captured, then matched from a herestring -- never `nm ... | grep -q`: under
	# pipefail the grep's early exit SIGPIPEs nm and fails the pipeline.
	local symbols; symbols=$( nm -gU "$binary" 2>/dev/null || true )
	command grep -q '_plugMain' <<<"$symbols" || fail "$name: plugMain not exported"
	local plist="$bundle/Contents/Info.plist"
	[[ -f "$plist" ]] || fail "$name: no Info.plist"
	read_plist() { /usr/libexec/PlistBuddy -c "Print :$1" "$plist" 2>/dev/null || true; }
	[[ "$( read_plist CFBundleIdentifier )" == "$id" ]] || fail "$name: bundle id is '$( read_plist CFBundleIdentifier )'"
	[[ "$( read_plist CFBundleExecutable )" == "$name" ]] || fail "$name: CFBundleExecutable is wrong"
	[[ "$( read_plist CFBundlePackageType )" == "BNDL" ]] || fail "$name: CFBundlePackageType is not BNDL"
	[[ "$( read_plist CFBundleVersion )" == "$declared" ]] || fail "$name: plist version != CMakeLists $declared"
	codesign --force --sign - --timestamp=none "$bundle" >/dev/null 2>&1 || fail "$name: could not be ad-hoc signed"
	codesign --verify --deep --strict "$bundle" >/dev/null 2>&1 || fail "$name: the ad-hoc signature does not verify"
	echo "ok   $name: $arches, plugMain exported, $id, BNDL, v$declared, ad-hoc signed and verified"
	# What a host reads: id, name, type. The FFGL name field is char[16] and not
	# null-terminated, so only something reading it back as a host does notices
	# a truncation.
	local probe; probe="$( "$OXBOW" probe "$bundle" 2>&1 || true )"
	printf '%s\n' "$probe" | sed -n '1,3p' | sed 's/^/   /'
	command grep -q "id:          $ffid" <<<"$probe" || fail "oxbow did not read the id $ffid"
	command grep -q "name:        $display\$" <<<"$probe" || fail "oxbow did not read the name '$display'"
	command grep -q "type:        $kind" <<<"$probe" || fail "oxbow did not read the type as $kind"
	# Its selftest renders faster than real time, so a plugin that votes on
	# its host clock's units sees a fast clock here (AGENTS.md); it must still
	# render.
	local selftest; selftest="$( "$OXBOW" selftest "$bundle" 2>&1 || true )"
	command grep -q "selftest:    PASS" <<<"$selftest" || { printf '%s\n' "$selftest" | tail -5; fail "oxbow selftest failed on $name"; }
	echo "ok   a host reads $display $ffid $kind, and oxbow selftest renders through it"
}
check_bundle "Snowglobe" "com.stoatworks.ffgl.snowglobe" "SW Snowglobe" "SG01" "effect"
# The Windows DLL is checked on GitHub's runner by tools/wincheck.ps1 against a
# parameter count written into both workflows: it must be the count the plugin
# declares (the About block's buttons change it), and the first name Shake.
count=$( "$SGTEST" --list | tail -n +2 | wc -l | tr -d ' ' )
first=$( "$SGTEST" --list | sed -n '2p' | awk '{print $2}' )
for wf in ci release; do
	command grep -q -- "-Params $count -First \"$first\"" ".github/workflows/$wf.yml" \
		|| fail "$wf.yml's wincheck does not ask for -Params $count -First \"$first\""
done
echo "ok   both workflows' wincheck ask for the $count parameters the plugin declares, $first first"

#---------------------------------------------------------------------------
step "Checks (this Mac's GPU, 1280x720 and 320x180)"
#---------------------------------------------------------------------------
# Every claim the README makes. The picture checks run at both rasters
# themselves; rate, clock, triggers and coreaudio measure time and the audio
# path, and run at 320x180; model, names and cues need no GL context.
CHECKS="fall settle swirl still grid magnify flakes rate clock resize alpha triggers coreaudio glstate robust model names cues"
for check in $CHECKS; do
	if out=$( "$SGTEST" --$check 2>&1 ); then
		printf '   ok   sgtest --%s: %s checks\n' "$check" "$( printf '%s\n' "$out" | command grep -c '^  ok' )"
	else
		printf '%s\n' "$out" | command grep -E '\bFAIL\b' | cut -c1-220 | sed 's/^/      /'
		fail "sgtest --$check"
	fi
done
# The flags --help lists are the checks this script runs: a check added to
# the harness and not here is a check nobody runs.
listed="$( "$SGTEST" --help | sed -n 's/^  checks, one flag each: //p' | tr ' ' '\n' | sed 's/^--//' | sort | tr '\n' ' ' )"
wanted="$( printf '%s\n' $CHECKS | sort | tr '\n' ' ' )"
[[ "$listed" == "$wanted" ]] || fail "sgtest --help lists '$listed', this script runs '$wanted'"
echo "ok   every check sgtest lists is run here"

#---------------------------------------------------------------------------
step "Software renderer (Apple's, at 320x180)"
#---------------------------------------------------------------------------
# The picture checks on Apple's software renderer: would the claims hold on
# another rasteriser? Its sin is off by up to 1.1e-3 (the fleet's trap: the
# shader's own series is used; this shader calls neither sin nor atan), and
# it is slow: every picture check runs on it, about six minutes. coreaudio
# measures the audio path, not pictures, and runs on the GPU only.
SOFTWARE="fall settle swirl still grid magnify flakes rate clock resize alpha triggers glstate robust"
for check in $SOFTWARE; do
	if out=$( SGTEST_RENDERER=software "$SGTEST" --$check --size 320x180 2>&1 ); then
		printf '   ok   sgtest --%s (software): %s checks\n' "$check" "$( printf '%s\n' "$out" | command grep -c '^  ok' )"
	else
		printf '%s\n' "$out" | command grep -E '\bFAIL\b' | cut -c1-220 | sed 's/^/      /'
		fail "sgtest --$check on the software renderer -- run: SGTEST_RENDERER=software $SGTEST --$check --size 320x180"
	fi
done

#---------------------------------------------------------------------------
step "Offline (no GL context)"
#---------------------------------------------------------------------------
"$SGTEST" --offline >/dev/null || fail "sgtest --offline"
echo "ok   sgtest --offline"

#---------------------------------------------------------------------------
step "The flakes' tolerances"
#---------------------------------------------------------------------------
# --flakes' area and position tolerances are the box rule's worst error over
# 6,000 ellipses, rounded up; read here from the harness, never retyped.
area_tol="$( sed -n 's/^constexpr double kFlakeAreaTolerance *= *\([0-9.]*\);$/\1/p' tools/sgtest/checks_optics.inc )"
pos_tol="$( sed -n 's/^constexpr double kFlakePositionTolerance *= *\([0-9.]*\);$/\1/p' tools/sgtest/checks_optics.inc )"
[[ -n "$area_tol" && -n "$pos_tol" ]] || fail "could not read the flakes' tolerances from checks_optics.inc"
python3 tools/coverage.py --within "$area_tol" "$pos_tol" | sed 's/^/   /' || fail "the box rule's error is not inside --flakes' tolerances"

#---------------------------------------------------------------------------
step "Resolume's demo clips"
#---------------------------------------------------------------------------
python3 tools/clips.py alpha --build "$BUILD" | command grep -E '^  (ok|FAIL)' | cut -c1-200 || fail "the alpha check on the demo clips"
# The defaults are chosen; this is what they measure against the clips.
python3 tools/clips.py defaults --build "$BUILD" | cut -c1-240 || fail "the defaults' measurement on the demo clips"

#---------------------------------------------------------------------------
step "Pipe"
#---------------------------------------------------------------------------
# The fleet's --pipe frame format. Two and a half frames in must be exactly
# two out and a clean exit -- a partial frame is the end of the stream, never
# a frame -- --film N must be N frames, a cue naming no parameter, a --set
# that is not a number or an option and a --set of nan must be refused, an
# option by name accepted, and a reader that hangs up must end the run with
# exit 1, not SIGPIPE's silent 141. Under bash: zsh has no PIPESTATUS.
frame=$(( 64 * 36 * 4 ))
raw=$( mktemp ); cues=$( mktemp ); out=$( mktemp )
/usr/bin/head -c $(( frame * 5 / 2 )) /dev/zero > "$raw"
status=0
"$SGTEST" --pipe --size 64x36 < "$raw" > "$out" 2>/dev/null || status=$?
got=$( wc -c < "$out" | tr -d ' ' )
[[ "$status" -eq 0 && "$got" == "$(( frame * 2 ))" ]] || fail "2.5 frames in gave $got bytes out (want $(( frame * 2 ))), exit $status"
status=0
"$SGTEST" --film 3 --size 64x36 > "$out" 2>/dev/null || status=$?
got=$( wc -c < "$out" | tr -d ' ' )
[[ "$status" -eq 0 && "$got" == "$(( frame * 3 ))" ]] || fail "--film 3 gave $got bytes, exit $status"
# Read from a file, not a pipe: a writer killed by SIGPIPE would fail the
# pipeline whatever sgtest did, and the refusal would pass for the wrong reason.
printf '0 No Such Control 0.5\n' > "$cues"
status=0
"$SGTEST" --pipe --size 64x36 --script "$cues" < "$raw" >/dev/null 2>&1 || status=$?
[[ "$status" -eq 2 ]] || fail "a cue naming no parameter gave exit $status, not 2"
for bad in "Snow=Snowy" "Fill=deep" "Fill=nan" "Fill=2" "No Such=1"; do
	status=0
	"$SGTEST" --film 1 --size 64x36 --set "$bad" >/dev/null 2>&1 || status=$?
	[[ "$status" -eq 2 ]] || fail "--set '$bad' gave exit $status, not 2"
done
for good in "Snow=Beads" "Trigger On=Beat" "Audio Input=Resolume FFT" "Fill=0.5"; do
	status=0
	"$SGTEST" --film 1 --size 64x36 --set "$good" >/dev/null 2>&1 || status=$?
	[[ "$status" -eq 0 ]] || fail "--set '$good' gave exit $status"
done
/usr/bin/head -c $(( frame * 20 )) /dev/zero > "$raw"
set +e
"$SGTEST" --pipe --size 64x36 < "$raw" 2>/dev/null | /usr/bin/head -c 1 >/dev/null
s1=${PIPESTATUS[0]}
"$SGTEST" --film 20 --size 64x36 2>/dev/null | /usr/bin/head -c 1 >/dev/null
s2=${PIPESTATUS[0]}
set -e
[[ "$s1" -eq 1 && "$s2" -eq 1 ]] || fail "a closed stdout gave exit $s1 (--pipe), $s2 (--film), not 1"
echo "ok   2.5 frames in, exactly 2 out; --film 3 is 3; a bad cue, an unknown option, a word, nan, 2 and an unknown name refused (exit 2); options by name accepted; | head -c 1 exits 1, not 141"
# --wav (the video's and the flash survey's music): a file that is not a WAV
# is refused, and a WAV of kicks shakes the globe where a WAV of silence
# leaves it still -- so the file really reaches earshot's feed().
kicks=$( mktemp ); quiet=$( mktemp ); a=$( mktemp ); b=$( mktemp )
python3 - "$kicks" "$quiet" <<'PY'
import math, struct, sys, wave
rate = 44100
for path, loud in ((sys.argv[1], True), (sys.argv[2], False)):
    frames = bytearray()
    for n in range(rate * 3):
        t = n / rate
        beat = math.fmod(t, 0.5)
        s = 0.6 * math.exp(-beat / 0.06) * math.sin(2 * math.pi * (55 + 60 * math.exp(-beat / 0.02)) * beat) if loud else 0.0
        frames += struct.pack('<hh', int(s * 32767), int(s * 32767))
    with wave.open(path, 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate); w.writeframes(bytes(frames))
PY
status=0
"$SGTEST" --film 1 --size 64x36 --wav "$cues" >/dev/null 2>&1 || status=$?
[[ "$status" -eq 2 ]] || fail "--wav of a text file gave exit $status, not 2"
"$SGTEST" --film 180 --size 64x36 --wav "$kicks" > "$a" 2>/dev/null || fail "--film with a WAV of kicks"
"$SGTEST" --film 180 --size 64x36 --wav "$quiet" > "$b" 2>/dev/null || fail "--film with a WAV of silence"
! cmp -s "$a" "$b" || fail "--wav: a WAV of kicks gave the same frames as a WAV of silence"
echo "ok   --wav: a text file refused (exit 2); a WAV of kicks shakes the globe, a WAV of silence does not"
rm -f "$raw" "$cues" "$out" "$kicks" "$quiet" "$a" "$b"

#---------------------------------------------------------------------------
step "Negative controls"
#---------------------------------------------------------------------------
# Every check above, against a model that is deliberately wrong, required to
# FAIL; a check with no wrong model is itself a failure. A check that cannot
# fail is not a check.
if out=$( "$SGTEST" --negative 2>&1 ); then
	printf '%s\n' "$out" | command grep -E '^negative controls' | sed 's/^/   /'
else
	printf '%s\n' "$out" | command grep -E 'PASSED against|no negative control' | sed 's/^/      /'
	fail "a negative control went undetected"
fi

#---------------------------------------------------------------------------
step "Mutants"
#---------------------------------------------------------------------------
# The indented lines are the check's own FAIL lines: they show a mutant was
# caught by the check failing, not by a shader that no longer compiles.
tools/mutate.sh | command grep -E '^== mutant|^   (ok|FAIL)|^        |^mutants' || fail "a mutant survived"

#---------------------------------------------------------------------------
step "Dead controls"
#---------------------------------------------------------------------------
# The only thing that catches a uniform whose name does not match the C++.
python3 tools/sweep.py --build "$BUILD" || fail "a dead control"
# Once more with every per-control context emptied: what goes dead there is a
# control that acts only with another one set (AGENTS.md lists them; today
# none does, so this must pass too).
python3 tools/sweep.py --build "$BUILD" --bare || fail "a control dead without its context"

#---------------------------------------------------------------------------
step "Cost"
#---------------------------------------------------------------------------
"$SGTEST" --bench

printf '\n\033[32mall green\033[0m\n'
