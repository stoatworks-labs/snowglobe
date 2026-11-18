// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape is \` for a backtick inside a comment.
export const VERSION = '#version 410 core\n';

// kQuadVertex, source/Shaders.cpp
export const QUAD_VERTEX = `
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
`;

// kCommon, source/Shaders.cpp
export const COMMON = `
const int kHookNoPremultiply = 1;
const int kHookNoRefract     = 2;
const int kHookHardEdge      = 4;

uniform int Hooks;
uniform ivec4 Viewport;//the target: x, y, width, height
uniform vec2 Size;     //the output raster, px

out vec4 fragColour;

bool hook( int bit )
{
	return ( Hooks & bit ) != 0;
}

//This pixel in centred px of the output raster, y up.
vec2 centred()
{
	return gl_FragCoord.xy - vec2( Viewport.xy ) - 0.5 * Size;
}
`;

// kSceneFragment, source/Shaders.cpp
export const SCENE_FRAGMENT = `
uniform sampler2D InputTexture;
uniform ivec2 InputSize;
uniform float Index;       //n of the fill
uniform float GlobePx;     //the silhouette's radius, px
uniform float XiSil;       //tan of the silhouette's half-angle, 1 / sqrt(D^2 - 1)
uniform float ViewDistance;//D / R
uniform float ClipAspect;  //the clip's width / height

//One texel, clamped to the clip, premultiplied.
vec4 texel( ivec2 p )
{
	vec4 c = clamp( texelFetch( InputTexture, clamp( p, ivec2( 0 ), InputSize - 1 ), 0 ), 0.0, 1.0 );
	return hook( kHookNoPremultiply ) ? c : vec4( c.rgb * c.a, c.a );
}

//The clip at uv (0..1, y up), filtered by hand: premultiplied before the
//weights, so a transparent texel's colour never bleeds.
vec4 clipAt( vec2 tc )
{
	if( tc.x < 0.0 || tc.y < 0.0 || tc.x > 1.0 || tc.y > 1.0 )
		return vec4( 0.0 );
	vec2 p  = tc * vec2( InputSize ) - 0.5;
	vec2 f  = floor( p );
	vec2 w  = p - f;
	ivec2 i = ivec2( f );
	vec4 a  = mix( texel( i ), texel( i + ivec2( 1, 0 ) ), w.x );
	vec4 b  = mix( texel( i + ivec2( 0, 1 ) ), texel( i + ivec2( 1, 1 ) ), w.x );
	return mix( a, b, w.y );
}

//The ray at screen tangent xi from the pinhole (0, 0, D), units of R:
//where it meets the card z = 0 inside the sphere. The discriminant and the
//entry depth are written without the cancellation of D^2 against D^2 - 1.
vec4 sceneAt( vec2 P )
{
	vec2 xi    = P / GlobePx * XiSil;
	float x2   = dot( xi, xi );
	float L2   = 1.0 + x2;
	float L    = sqrt( L2 );
	float D    = ViewDistance;
	float disc = 1.0 - D * D * x2 / L2;
	if( disc < 0.0 )
		return vec4( 0.0 );
	float root = sqrt( disc );
	vec3 d     = vec3( xi, -1.0 ) / L;
	float s    = D / L - root;
	vec3 S     = vec3( s * d.xy, D * x2 / L2 + root / L );
	vec3 t     = d;
	if( !hook( kHookNoRefract ) )
	{
		float eta = 1.0 / Index;
		float c1  = -dot( d, S );
		float k   = 1.0 - eta * eta * ( 1.0 - c1 * c1 );
		t         = eta * d + ( eta * c1 - sqrt( max( k, 0.0 ) ) ) * S;
	}
	if( t.z >= 0.0 )
		return vec4( 0.0 );
	vec2 Q = S.xy + ( -S.z / t.z ) * t.xy;
	return clipAt( vec2( 0.5 + Q.x / ( 2.0 * ClipAspect ), 0.5 + 0.5 * Q.y ) );
}

void main()
{
	vec2 P = centred();
	vec4 sum = sceneAt( P + vec2( -0.25, -0.25 ) ) + sceneAt( P + vec2( 0.25, -0.25 ) )
	         + sceneAt( P + vec2( -0.25, 0.25 ) ) + sceneAt( P + vec2( 0.25, 0.25 ) );
	fragColour = 0.25 * sum;
}
`;

// kFlakeVertex, source/Shaders.cpp
export const FLAKE_VERTEX = `
layout( location = 0 ) in vec4 FlakeA;//centre (centred px, y up), first semi-axis (px)
layout( location = 1 ) in vec4 FlakeB;//second semi-axis (px), coverage scale, -

uniform vec2 Size;

flat out vec4 axisA;
flat out vec4 axisB;

const int kCorner[ 6 ] = int[ 6 ]( 0, 1, 2, 2, 1, 3 );

void main()
{
	int k    = kCorner[ gl_VertexID % 6 ];
	vec2 c   = vec2( float( k & 1 ), float( ( k >> 1 ) & 1 ) ) * 2.0 - 1.0;
	vec2 a1  = FlakeA.zw;
	vec2 a2  = FlakeB.xy;
	float p1 = 1.0 + 1.5 / max( length( a1 ), 1e-3 );
	float p2 = 1.0 + 1.5 / max( length( a2 ), 1e-3 );
	vec2 p   = FlakeA.xy + c.x * p1 * a1 + c.y * p2 * a2;
	gl_Position = vec4( p / ( 0.5 * Size ), 0.0, 1.0 );
	axisA       = FlakeA;
	axisB       = FlakeB;
}
`;

// kFlakeFragment, source/Shaders.cpp
export const FLAKE_FRAGMENT = `
flat in vec4 axisA;
flat in vec4 axisB;

uniform float GlobePx;//the silhouette's radius, px: nothing inside the glass shows outside it

//The coverage at a point: one-pixel ramp across the ellipse's edge, the
//distance taken as (r - 1) / |grad r| with r the ellipse's own radius.
float coverAt( vec2 rel, vec2 a1, vec2 a2, float l1, float l2 )
{
	vec2 q  = vec2( dot( rel, a1 ) / l1, dot( rel, a2 ) / l2 );
	float r = length( q );
	if( hook( kHookHardEdge ) )
		return step( r, 1.0 );
	vec2 g  = ( q.x / max( r, 1e-6 ) ) * a1 / l1 + ( q.y / max( r, 1e-6 ) ) * a2 / l2;
	float d = ( r - 1.0 ) / max( length( g ), 1e-6 );
	return clamp( 0.5 - d, 0.0, 1.0 );
}

void main()
{
	vec2 a1  = axisA.zw;
	vec2 a2  = axisB.xy;
	//The semi-axes are orthogonal (the CPU's SVD).
	vec2 P   = centred();
	vec2 rel = P - axisA.xy;
	float l1 = max( dot( a1, a1 ), 1e-12 );
	float l2 = max( dot( a2, a2 ), 1e-12 );
	//Box-filtered over the pixel by a 4 x 4 midpoint rule, so the pixels sum
	//to the coverage's integral, wherever the flake sits on the lattice; a
	//subsample past the silhouette sees no flake (the ellipse is the
	//projection's first order, and at the rim it overhangs).
	float cover = 0.0;
	for( int j = 0; j < 4; ++j )
		for( int i = 0; i < 4; ++i )
		{
			vec2 o = vec2( float( i ) - 1.5, float( j ) - 1.5 ) * 0.25;
			if( dot( P + o, P + o ) <= GlobePx * GlobePx )
				cover += coverAt( rel + o, a1, a2, l1, l2 );
		}
	fragColour = vec4( cover * ( axisB.z / 16.0 ) );
}
`;

// kCompositeFragment, source/Shaders.cpp
export const COMPOSITE_FRAGMENT = `
uniform sampler2D InputTexture;
uniform ivec2 InputSize;
uniform sampler2D Globe;
uniform float MixAmount;

void main()
{
	ivec2 here = ivec2( gl_FragCoord.xy ) - Viewport.xy;
	ivec2 at   = ivec2( ( vec2( here ) + 0.5 ) * ( vec2( InputSize ) / Size ) );
	vec4 raw   = texelFetch( InputTexture, at, 0 );
	if( MixAmount <= 0.0 )
	{
		fragColour = raw;//Mix 0 hands the clip through, bit for bit
		return;
	}
	vec4 g     = texelFetch( Globe, here, 0 );
	vec4 s     = clamp( raw, 0.0, 1.0 );
	vec4 srcP  = hook( kHookNoPremultiply ) ? s : vec4( s.rgb * s.a, s.a );
	vec4 o     = mix( srcP, clamp( g, 0.0, 1.0 ), clamp( MixAmount, 0.0, 1.0 ) );
	//Clamped: the blend's rounding can leave a colour an ulp above its alpha.
	fragColour = o.a > 0.0 ? clamp( vec4( o.rgb / o.a, o.a ), 0.0, 1.0 ) : vec4( 0.0 );
}
`;

// Each stage's source, piece by piece, in the order the plugin joins them:
// Shaders.cpp's AssembleVertex( Pass ) and AssembleFragment( Pass ), for
// each of the three programs Snowglobe.cpp's InitGL compiles.
export const ASSEMBLY = {
  scene_vertex: ['VERSION', 'QUAD_VERTEX'],
  scene_fragment: ['VERSION', 'COMMON', 'SCENE_FRAGMENT'],
  flake_vertex: ['VERSION', 'FLAKE_VERTEX'],
  flake_fragment: ['VERSION', 'COMMON', 'FLAKE_FRAGMENT'],
  composite_vertex: ['VERSION', 'QUAD_VERTEX'],
  composite_fragment: ['VERSION', 'COMMON', 'COMPOSITE_FRAGMENT'],
};
