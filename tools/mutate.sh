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
# AND the sgtest binary deleted (an object make thinks is current, or a binary
# left from the last link, would test the wrong code), sgtest rebuilt, and the
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
WORK="$(mktemp -d "${TMPDIR:-/tmp}/sgmutate.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
export SNOWGLOBE_NO_DEVICES=1
LIMIT="${MUTATE_LIMIT:-300}"

# file | the exact text | its one-character mutant | the check that must fail | what it is
# The fields are split on '|', so neither a target nor a mutant may contain one
# (conway's trap). Only .cpp files: the object that must change is the file's.
MUTANTS=(
	"source/Shaders.cpp|float disc = 1.0 - D * D * x2 / L2;|float disc = 1.0 + D * D * x2 / L2;|--grid|GLSL: the silhouette's discriminant 1 + D^2 x^2 / L^2, - -> +"
	"source/Shaders.cpp|float s    = D / L - root;|float s    = D / L + root;|--grid|GLSL: the ray meeting the glass where it leaves, not where it enters, - -> +"
	"source/Shaders.cpp|D * x2 / L2 + root / L );|D * x2 / L2 - root / L );|--grid|GLSL: the entry point's depth with the root subtracted, + -> -"
	"source/Shaders.cpp|float eta = 1.0 / Index;|float eta = 2.0 / Index;|--magnify|GLSL: the index ratio 2 / n, 1 -> 2"
	"source/Shaders.cpp|float k   = 1.0 - eta * eta|float k   = 1.0 + eta * eta|--grid|GLSL: Snell's cosine term 1 + eta^2 (1 - c^2), - -> +"
	"source/Shaders.cpp|( eta * c1 - sqrt( max( k, 0.0 ) ) )|( eta * c1 + sqrt( max( k, 0.0 ) ) )|--grid|GLSL: the refracted ray's normal part with the wrong sign, - -> +"
	"source/Shaders.cpp|vec2 Q = S.xy + ( -S.z / t.z ) * t.xy;|vec2 Q = S.xy - ( -S.z / t.z ) * t.xy;|--grid|GLSL: the ray walked away from the card, + -> -"
	"source/Shaders.cpp|0.5 + Q.x / ( 2.0 * ClipAspect )|0.5 + Q.x / ( 3.0 * ClipAspect )|--grid|GLSL: the card's width read as 3 aspect, 2 -> 3"
	"source/Shaders.cpp|vec2 p  = tc * vec2( InputSize ) - 0.5;|vec2 p  = tc * vec2( InputSize ) + 0.5;|--grid|GLSL: the card read a texel off, - -> +"
	"source/Shaders.cpp|texel( i + ivec2( 1, 1 ) ), w.x );|texel( i + ivec2( 1, 1 ) ), w.y );|--grid|GLSL: the card's upper row weighted by y, x -> y"
	"source/Shaders.cpp|vec4( c.rgb * c.a, c.a )|vec4( c.rgb + c.a, c.a )|--alpha|GLSL: the card premultiplied by adding its alpha, * -> +"
	"source/Shaders.cpp|vec4( s.rgb * s.a, s.a )|vec4( s.rgb + s.a, s.a )|--alpha|GLSL: the composite's clip premultiplied by adding its alpha, * -> +"
	"source/Shaders.cpp|if( MixAmount <= 0.0 )|if( MixAmount <= 1.0 )|--alpha|GLSL: Mix 1 handing the clip through untouched, 0 -> 1"
	"source/Shaders.cpp|sceneAt( P + vec2( -0.25, -0.25 ) )|sceneAt( P + vec2( -0.75, -0.25 ) )|--grid|GLSL: one of the four subsamples half a pixel out, 2 -> 7"
	"source/Shaders.cpp|- vec2( Viewport.xy ) - 0.5 * Size;|- vec2( Viewport.xy ) - 0.6 * Size;|--grid|GLSL: the centre of the raster off by a tenth of it, 5 -> 6"
	"source/Shaders.cpp|return clamp( 0.5 - d, 0.0, 1.0 );|return clamp( 1.5 - d, 0.0, 1.0 );|--flakes|GLSL: the flake's edge ramp a pixel outside its edge, 0 -> 1"
	"source/Shaders.cpp|axisB.z / 16.0|axisB.z / 15.0|--flakes|GLSL: the box rule's sixteen subsamples divided by 15, 6 -> 5"
	"source/Shaders.cpp|vec2( float( i ) - 1.5, float( j ) - 1.5 ) * 0.25;|vec2( float( i ) - 2.5, float( j ) - 1.5 ) * 0.25;|--flakes|GLSL: the box rule's subsamples a quarter pixel left, 1 -> 2"
	"source/Shaders.cpp|if( dot( P + o, P + o ) <= GlobePx * GlobePx )|if( dot( P + o, P + o ) >= GlobePx * GlobePx )|--flakes|GLSL: flakes drawn only outside the glass, < -> >"
	"source/Shaders.cpp|float p1 = 1.0 + 1.5 / max( length( a1 ), 1e-3 );|float p1 = 1.0 - 1.5 / max( length( a1 ), 1e-3 );|--flakes|GLSL: the flake's quad cut inside its ellipse, + -> -"
	"source/Shaders.cpp|gl_Position = vec4( p / ( 0.5 * Size ), 0.0, 1.0 );|gl_Position = vec4( p / ( 0.6 * Size ), 0.0, 1.0 );|--flakes|GLSL: flakes placed for a raster a fifth larger, 5 -> 6"
	"source/Globe.cpp|return ( c / 16.0 ) * along|return ( c / 17.0 ) * along|--fall|C++: a disc's broadside mobility 1/17, 6 -> 7"
	"source/Globe.cpp|( 3.0 * c / 32.0 ) * inPlane|( 3.0 * c / 22.0 ) * inPlane|--fall|C++: a disc's edgewise mobility 3/22, 3 -> 2"
	"source/Globe.cpp|rs * rs / ( 9.0 * mu )|rs * rs / ( 8.0 * mu )|--fall|C++: Stokes's law over 8, 9 -> 8"
	"source/Globe.cpp|return p.fill.nu * k * k;|return p.fill.nu * k / k;|--swirl|C++: the swirl's decay nu, not nu k^2, * -> /"
	"source/Globe.cpp|3.0 * ( std::sin( z ) - z * std::cos( z ) )|3.0 * ( std::sin( z ) + z * std::cos( z ) )|--swirl|C++: the swirl's profile with j1's sign flipped, - -> +"
	"source/Globe.cpp|const double amp = S * kOmegaMax * swirlGain;|const double amp = S / kOmegaMax * swirlGain;|--swirl|C++: a full shake spinning the fill at 1/2 rad/s, not 2, * -> /"
	"source/Globe.cpp|const double limit = kRadius - radius;|const double limit = kRadius + radius;|--robust|C++: flakes landing a radius outside the glass, - -> +"
	"source/Globe.cpp|std::min( f.x.y + up,|std::min( f.x.y - up,|--settle|C++: a shake pushing flakes down into the glass, + -> -"
	"source/Globe.cpp|f.x         = x + ( dt / 6.0 )|f.x         = x + ( dt / 5.0 )|--fall|C++: RK4's step weighted 1/5, 6 -> 5"
	"source/Fill.cpp|const double rhoG        = 1273.0 - 0.612 * t;|const double rhoG        = 1373.0 - 0.612 * t;|--model|C++: glycerol's density 100 kg/m^3 high, 2 -> 3"
	"source/Fill.cpp|return 0.00149 * x + 1.32359;|return 0.00148 * x + 1.32359;|--model|C++: Hoyt's slope 0.00148, 9 -> 8"
	"source/Fill.cpp|const double a     = 0.705 - 0.0017 * t;|const double a     = 0.805 - 0.0017 * t;|--model|C++: Cheng's a 0.805, 7 -> 8"
	"source/Optics.cpp|const double eta = 1.0 / n;|const double eta = 2.0 / n;|--flakes|C++: the flakes' index ratio 2 / n, 1 -> 2"
	"source/Optics.cpp|constant( 0.0 ) - b - dsqrt( disc );|constant( 0.0 ) - b + dsqrt( disc );|--flakes|C++: the flakes' ray meeting the glass where it leaves, - -> +"
	"source/Optics.cpp|kViewDistance * kViewDistance - 1.0 );|kViewDistance * kViewDistance + 1.0 );|--grid|C++: the silhouette's tangent 1 / sqrt(D^2 + 1), - -> +"
	"source/Snowglobe.cpp|nowUs = std::llround( seconds * 1e6 );|nowUs = std::llround( seconds * 1e5 );|--triggers|C++: the clock in tenths of a millisecond, 6 -> 5"
	"source/Snowglobe.cpp|1.0 : ( boundary - p0 ) / ( p1 - p0 );|1.0 : ( boundary + p0 ) / ( p1 - p0 );|--triggers|C++: a beat placed in the frame by boundary + p0, - -> +"
	"source/Snowglobe.cpp|if( t - lastKickUs >= kKickMergeUs )|if( t - lastKickUs <= kKickMergeUs )|--triggers|C++: kicks kept only inside the merge window, > -> <"
	"source/Snowglobe.cpp|const bool down = value >= 0.5f;|const bool down = value >= 1.5f;|--triggers|C++: a button that can never be pressed, 0 -> 1"
	"source/Snowglobe.cpp|band.level >= kKickDominance * loudest|band.level <= kKickDominance * loudest|--triggers|C++: a kick only when band 0 is quiet, > -> <"
	"source/Snowglobe.cpp|stepUs = std::clamp< int64_t >( nowUs - lastNowUs,|stepUs = std::clamp< int64_t >( nowUs + lastNowUs,|--triggers|C++: the frame's step the sum of two clock readings, - -> +"
	"source/Snowglobe.cpp|if( source == TriggerOn::Kick )|if( source != TriggerOn::Kick )|--coreaudio|C++: the audio device opened when Trigger On is not Kick, = -> !"
	"source/Snowglobe.cpp|( w1 / w2 + w2 / w1 ) / 24.0|( w1 / w2 + w2 / w1 ) / 34.0|--flakes|C++: the ramp's integral term over 34, 2 -> 3"
	"source/Snowglobe.cpp|const double globePx = 0.5 * GlobeFromParam|const double globePx = 0.6 * GlobeFromParam|--alpha|C++: the globe a fifth larger than Globe says, 5 -> 6"
	"source/Audio.cpp|std::strcmp( v, \"0\" ) != 0;|std::strcmp( v, \"1\" ) != 0;|--coreaudio|C++: SNOWGLOBE_NO_DEVICES=1 read as off, 0 -> 1"
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
objects="$build/CMakeFiles/snowglobe_core.dir/source"
sgtest="$build/sgtest"

printf '== the unmutated build (arm64)\n'
cmake -S "$tree" -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >"$WORK/log" 2>&1 || { tail -20 "$WORK/log"; exit 1; }
cmake --build "$build" --target sgtest audiospy -j6 >"$WORK/log" 2>&1 || { tail -20 "$WORK/log"; exit 1; }
mkdir -p "$WORK/base"
cp "$objects"/*.o "$WORK/base/"

checks="$( for entry in "${MUTANTS[@]}"; do IFS='|' read -r _ _ _ check _ <<<"$entry"; printf '%s\n' "$check"; done | sort -u )"
for check in $checks; do
	status=0
	limit "$LIMIT" "$sgtest" "$check" --size 320x180 >"$WORK/log" 2>&1 || status=$?
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
	rm -f "$object" "$sgtest"
	if ! cmake --build "$build" --target sgtest -j6 >"$WORK/log" 2>&1; then
		# A mutant that does not compile tests nothing.
		printf '   FAIL  the mutant does not build:\n'
		{ grep -E 'error' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
		continue
	fi
	if [[ ! -f "$object" || ! -x "$sgtest" ]] || cmp -s "$object" "$WORK/base/$( basename "$object" )"; then
		printf '   FAIL  the mutant is not in the build (%s unchanged or missing)\n' "$( basename "$object" )"
		continue
	fi
	if [[ "$file" == source/Shaders.cpp ]] && ! grep -qF -- "$mutant" "$sgtest"; then
		printf '   FAIL  the mutated GLSL is not in the sgtest binary\n'
		continue
	fi
	status=0
	limit "$LIMIT" "$sgtest" "$check" --size 320x180 >"$WORK/log" 2>&1 || status=$?
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
