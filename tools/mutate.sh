#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders would pass every GLSL mutant here.
#
# One copy of the tree in a temporary directory (the FFGL SDK and earshot
# symlinked, not copied), built arm64-only once. For each mutant: the last
# mutant's file put back, this one's file changed, the changed file's object
# AND the matest binary deleted (an object make thinks is current, or a binary
# left from the last link, would test the wrong code), matest rebuilt, and the
# mutant proved to be in the build -- the object must differ from the
# unmutated one, and a GLSL mutant's text must be in the binary -- before only
# the named check runs, at 320x180, under a time limit (a mutant can hang:
# honeydew's lesson; a timeout counts as caught, and is said).
#
# Before any mutant, every named check must PASS on the unmutated build: a
# check that fails anyway would "catch" every mutant.
#
#     tools/mutate.sh            every mutant
#     tools/mutate.sh TEXT       only those whose description contains TEXT
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/mamutate.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
export MARBLING_NO_DEVICES=1
LIMIT="${MUTATE_LIMIT:-300}"

# file | the exact text | its one-character mutant | the check that must fail | what it is
# The fields are split on '|', so neither a target nor a mutant may contain one
# (conway's trap). Only .cpp files: the object that must change is the file's.
MUTANTS=(
	"source/Shaders.cpp|float q = ( rho2 - r2 ) / rho2;|float q = ( rho2 + r2 ) / rho2;|--drop|GLSL: a drop's backward map with 1 + r^2/rho^2 under the root, - -> +"
	"source/Shaders.cpp|if( rho2 < r2 )|if( rho2 > r2 )|--drop|GLSL: inside the drop taken for outside, < -> >"
	"source/Shaders.cpp|d       = 0.5 * s - abs( mod( d, s ) - 0.5 * s );|d       = 0.5 * s + abs( mod( d, s ) - 0.5 * s );|--undo|GLSL: the comb's fold to the nearest tine turned outward, - -> +"
	"source/Shaders.cpp|float g = b.y * b.z / ( d + b.z );|float g = b.y * b.z / ( d - b.z );|--undo|GLSL: the tine's falloff alpha lambda / (d - lambda), + -> -"
	"source/Shaders.cpp|P -= g * M;|P += g * M;|--undo|GLSL: the comb pulling against its own direction, - -> +"
	"source/Shaders.cpp|P -= c.x * sinSeries(|P += c.x * sinSeries(|--undo|GLSL: the wavy comb's conjugation undone the wrong way, - -> +"
	"source/Shaders.cpp|abs( rho - a.w )|abs( rho + a.w )|--undo|GLSL: the circular stroke's distance from its circle as rho + R, - -> +"
	"source/Shaders.cpp|cs * d.x - sn * d.y|cs * d.x + sn * d.y|--undo|GLSL: the circular stroke's rotation not a rotation, - -> +"
	"source/Shaders.cpp|-1.0 / 6.0 + y2|-1.0 / 7.0 + y2|--undo|GLSL: the sine series' cubic term 1/7, 6 -> 7"
	"source/Shaders.cpp|f.y ), 0.0, 1.0 );|f.x ), 0.0, 1.0 );|--resample|GLSL: the baked map's vertical weight read from x, y -> x"
	"source/Shaders.cpp|vec4( m.xy * Rescale, 0.0, 1.0 )|vec4( m.xy / Rescale, 0.0, 1.0 )|--resize|GLSL: a resize scaling the map's positions the wrong way, * -> /"
	"source/Shaders.cpp|* ( 1.0 / 16.0 );|* ( 1.0 / 17.0 );|--ink|GLSL: a drop's ink the mean of 16 samples over 17, 6 -> 7"
	"source/Shaders.cpp|fragColour = vec4( ink.rgb, -1.0 - ink.a );|fragColour = vec4( ink.rgb, -1.0 + ink.a );|--ink|GLSL: the bake's ink marker -1 + a, - -> +"
	"source/Shaders.cpp|vec4( s.rgb * s.a, s.a )|vec4( s.rgb + s.a, s.a )|--alpha|GLSL: the clip premultiplied by adding its alpha, * -> +"
	"source/Shaders.cpp|if( MixAmount <= 0.0 )|if( MixAmount <= 1.0 )|--alpha|GLSL: Mix 1 returning the clip untouched, 0 -> 1"
	"source/Bath.cpp|r.push_back( comb( 1, 0, 1, 0 ) );|r.push_back( comb( 1, 0, 2, 0 ) );|--model|C++: Nonpareil's finishing comb the 1/2\" one, not the 1/4\", 1 -> 2"
	"source/Bath.cpp|o.alpha = alpha * p;|o.alpha = alpha / p;|--model|C++: a stroke in progress travelling alpha / p, * -> /"
	"source/Bath.cpp|if( o.Progress( time ) < 1.0 )|if( o.Progress( time ) > 1.0 )|--paper|C++: Auto Paper laying the sheet over a stroke still moving, < -> >"
	"source/Bath.cpp|n - kBakeKeep ), n - kBakeAbove );|n - kBakeKeep ), n + kBakeAbove );|--model|C++: the forced bake n + 48, - -> +"
	"source/Marbling.cpp|static_cast< float >( o.offset * H );|static_cast< float >( o.offset / H );|--undo|C++: a comb's offset uploaded divided by the height, * -> /"
	"source/Marbling.cpp|nowUs = std::llround( seconds * 1e6 );|nowUs = std::llround( seconds * 1e5 );|--triggers|C++: the clock in tenths of a millisecond, 6 -> 5"
	"source/Marbling.cpp|1.0 : ( boundary - p0 ) / ( p1 - p0 );|1.0 : ( boundary + p0 ) / ( p1 - p0 );|--triggers|C++: a beat placed in the frame by boundary + p0, - -> +"
	"source/Marbling.cpp|if( t - lastOnsetUs >= std::llround|if( t - lastOnsetUs <= std::llround|--triggers|C++: onsets kept only inside the merge window, > -> <"
	"source/Marbling.cpp|const bool down = value >= 0.5f;|const bool down = value >= 1.5f;|--triggers|C++: a button that can never be pressed, 0 -> 1"
	"source/Marbling.cpp|static_cast< double >( height ) / static_cast< double >( mapH )|static_cast< double >( height ) * static_cast< double >( mapH )|--resize|C++: the resize's scale height times the old height, / -> *"
	"source/Marbling.cpp|if( source == TriggerOn::Onsets )|if( source != TriggerOn::Onsets )|--coreaudio|C++: the audio device opened when Trigger On is not Onsets, = -> !"
	"source/Audio.cpp|std::strcmp( v, \"0\" ) != 0;|std::strcmp( v, \"1\" ) != 0;|--coreaudio|C++: MARBLING_NO_DEVICES=1 read as off, 0 -> 1"
)

if [[ $# -gt 0 ]]; then
	chosen=()
	for entry in "${MUTANTS[@]}"; do
		IFS='|' read -r _ _ _ _ what <<<"$entry"
		[[ "$what" == *"$1"* ]] && chosen+=( "$entry" )
	done
	[[ ${#chosen[@]} -gt 0 ]] || { echo "no mutant's description contains '$1'"; exit 1; }
	MUTANTS=( "${chosen[@]}" )
fi

limit() {
	# macOS has no timeout(1): perl's alarm is enough. SIGALRM is exit 142.
	perl -e 'alarm shift; exec @ARGV' "$@"
}

# One-character check and the edit, without a heredoc (a heredoc hung on this
# Mac with the system's TMPDIR: the fleet's trap).
EDIT='
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: {original!r}")
if len(original) != len(mutant) or sum(a != b for a, b in zip(original, mutant)) != 1:
    sys.exit(f"a mutant must differ by exactly one character: {original!r} -> {mutant!r}")
path.write_text(text.replace(original, mutant))
'

tree="$WORK/tree"
mkdir -p "$tree/external"
cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
ln -s "$REPO/external/ffgl" "$tree/external/ffgl"
ln -s "$REPO/external/earshot" "$tree/external/earshot"
build="$tree/build"
objects="$build/CMakeFiles/marbling_core.dir/source"
matest="$build/matest"

printf '== the unmutated build (arm64)\n'
cmake -S "$tree" -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >"$WORK/log" 2>&1 || { tail -20 "$WORK/log"; exit 1; }
cmake --build "$build" --target matest audiospy -j6 >"$WORK/log" 2>&1 || { tail -20 "$WORK/log"; exit 1; }
mkdir -p "$WORK/base"
cp "$objects"/*.o "$WORK/base/"

checks="$( for entry in "${MUTANTS[@]}"; do IFS='|' read -r _ _ _ check _ <<<"$entry"; printf '%s\n' "$check"; done | sort -u )"
for check in $checks; do
	status=0
	limit "$LIMIT" "$matest" "$check" --size 320x180 >"$WORK/log" 2>&1 || status=$?
	if [[ "$status" -ne 0 ]]; then
		{ grep -E '^  FAIL' "$WORK/log" || true; } | head -3 | cut -c1-160 | sed 's/^/        /'
		printf '   FAIL  %s fails (exit %d) on the unmutated build: it would catch every mutant\n' "$check" "$status"
		exit 1
	fi
	printf '   ok    %s passes unmutated\n' "$check"
done

caught=0 timeouts=0 previous=""
for entry in "${MUTANTS[@]}"; do
	IFS='|' read -r file original mutant check what <<<"$entry"
	printf '\n== mutant: %s\n' "$what"
	if [[ -n "$previous" ]]; then
		cp "$REPO/$previous" "$tree/$previous"
		rm -f "$objects/$( basename "$previous" ).o"
	fi
	previous="$file"
	python3 -c "$EDIT" "$tree/$file" "$original" "$mutant"
	object="$objects/$( basename "$file" ).o"
	rm -f "$object" "$matest"
	if ! cmake --build "$build" --target matest -j6 >"$WORK/log" 2>&1; then
		# A mutant that does not compile tests nothing.
		printf '   FAIL  the mutant does not build:\n'
		{ grep -E 'error' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
		continue
	fi
	if [[ ! -f "$object" || ! -x "$matest" ]] || cmp -s "$object" "$WORK/base/$( basename "$object" )"; then
		printf '   FAIL  the mutant is not in the build (%s unchanged or missing)\n' "$( basename "$object" )"
		continue
	fi
	if [[ "$file" == source/Shaders.cpp ]] && ! grep -qF -- "$mutant" "$matest"; then
		printf '   FAIL  the mutated GLSL is not in the matest binary\n'
		continue
	fi
	status=0
	limit "$LIMIT" "$matest" "$check" --size 320x180 >"$WORK/log" 2>&1 || status=$?
	if [[ "$status" -eq 0 ]]; then
		printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
	elif [[ "$status" -eq 142 ]]; then
		printf '   ok    %s ran past %d s against the mutant (a timeout counts as caught)\n' "$check" "$LIMIT"
		caught=$(( caught + 1 ))
		timeouts=$(( timeouts + 1 ))
	else
		printf '   ok    %s fails against the mutant (exit %d):\n' "$check" "$status"
		{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
		caught=$(( caught + 1 ))
	fi
done

printf '\nmutants: %d, caught: %d (%d by timeout); each proved in the build first\n' "${#MUTANTS[@]}" "$caught" "$timeouts"
[[ "$caught" -eq "${#MUTANTS[@]}" ]]
