#ifndef COMMON_INCLUDED
#define COMMON_INCLUDED

// Ref: UE - Common.ush

#define POSITIVE_INFINITY (asfloat(0x7F800000))
#define NEGATIVE_INFINITY (asfloat(0xFF800000))

const static float PI = 3.1415926535897932f;
const static float TWO_PI = 2.0f * PI;
const static float TAU = 2.0f * PI;
const static float ONE_OVER_PI = 1.0f / PI;
const static float ONE_OVER_TWO_PI = 1.0f / TWO_PI;

const static float GOLDEN_ANGLE = 2.39996323f;

const static float MaxHalfFloat = 65504.0f;
const static float Max11BitsFloat = 65024.0f;
const static float Max10BitsFloat = 64512.0f;
const static float3 Max111110BitsFloat3 = float3(Max11BitsFloat, Max11BitsFloat, Max10BitsFloat);

// DirectX MSAA subpixel offsets, with pixel center [0.5, 0.5]
const static float2 g_PixelOffsets1[] = { float2(0.5f, 0.5f) };
const static float2 g_PixelOffsets2[] = { float2(0.25f, 0.25f), float2(0.75f, 0.75f) };
const static float2 g_PixelOffsets4[] = 
{
	float2(0.375f, 0.125f), float2(0.875f, 0.375f), 
	float2(0.625f, 0.875f), float2(0.125f, 0.625f)
};
const static float2 g_PixelOffsets8[] = 
{
	float2(0.5625f, 0.6875f), float2(0.4375f, 0.3125f),
	float2(0.8125f, 0.4375f), float2(0.3125f, 0.8125f),
	float2(0.1875f, 0.1875f), float2(0.0625f, 0.5625f),
	float2(0.6875f, 0.0625f), float2(0.9375f, 0.9375f)
};
const static float2 g_PixelOffsets16[] = 
{
	float2(0.5625f, 0.4375f), float2(0.4375f, 0.6875f),
	float2(0.3125f, 0.375f ), float2(0.75f  , 0.5625f),
	float2(0.1875f, 0.625f ), float2(0.625f , 0.1875f),
	float2(0.1875f, 0.3125f), float2(0.6875f, 0.8125f),
	float2(0.375f , 0.125f ), float2(0.5f   , 0.9375f),
	float2(0.25f  , 0.875f ), float2(0.125f , 0.25f  ),
	float2(0.0f   , 0.5f   ), float2(0.9375f, 0.75f  ),
	float2(0.875f , 0.0625f), float2(0.0625f, 0.0f   )
};

// Control MIP level used for material texture fetches. By default only raytracing 
// shaders (i.e., !PIXELSHADER) use manual MIP level selection. A material shader 
// can opt. in to force a specific MIP level.
//
// * USE_FORCE_TEXTURE_MIP : enable/disable manual MIP level selection
// * FORCED_TEXTURE_MIP    : force a specific MIP level
//
#if !PIXELSHADER && !defined(USE_FORCE_TEXTURE_MIP)
	#define USE_FORCE_TEXTURE_MIP 1
#endif
#ifndef USE_FORCE_TEXTURE_MIP
	#define USE_FORCE_TEXTURE_MIP 0
#endif
#ifndef FORCED_TEXTURE_MIP
	#define FORCED_TEXTURE_MIP 0.0f
#endif

#include "UniformBuffers.hlsl"

// In HLSL, fmod is implemented as 'Lhs - trunc(Lhs / Rhs) * Rhs'
// In some cases, using floor rather than trunc is better
float FmodFloor(float Lhs, float Rhs)
{
	return Lhs - floor(Lhs / Rhs) * Rhs;
}

float2 FmodFloor(float2 Lhs, float2 Rhs)
{
	return Lhs - floor(Lhs / Rhs) * Rhs;
}

float3 FmodFloor(float3 Lhs, float3 Rhs)
{
	return Lhs - floor(Lhs / Rhs) * Rhs;
}

float4 FmodFloor(float4 Lhs, float4 Rhs)
{
	return Lhs - floor(Lhs / Rhs) * Rhs;
}

float3 SafeNormalize(float3 V)
{
	return V * rsqrt(max(dot(V, V), 0.0001));
}

#ifndef USE_RAYTRACED_TEXTURE_RAYCONE_LOD
#define USE_RAYTRACED_TEXTURE_RAYCONE_LOD (0)
#endif // USE_RAYTRACED_TEXTURE_RAYCONE_LOD

static float GlobalTextureMipBias = 0;
static float GlobalRayCone_TexArea = 0;
float ComputeRayConeLod(Texture2D Tex)
{
#if USE_RAYTRACED_TEXTURE_RAYCONE_LOD
	uint2 Dimensions;
	Tex.GetDimensions(Dimensions.x, Dimensions.y);
	int TexArea = Dimensions.x * Dimensions.y;
	return 0.5f * log2(GlobalRayCone_TexArea * TexArea);
#else
    return FORCED_TEXTURE_MIP;
#endif
}

float  ClampToHalfFloatRange(float  X) { return clamp(X, float(0), MaxHalfFloat); }
float2 ClampToHalfFloatRange(float2 X) { return clamp(X, float(0).xx, MaxHalfFloat.xx); }
float3 ClampToHalfFloatRange(float3 X) { return clamp(X, float(0).xxx, MaxHalfFloat.xxx); }
float4 ClampToHalfFloatRange(float4 X) { return clamp(X, float(0).xxxx, MaxHalfFloat.xxxx); }


float4 Texture2DSample(Texture2D Tex, SamplerState Sampler, float2 UV)
{
#if USE_FORCE_TEXTURE_MIP
	return Tex.SampleLevel(Sampler, UV, ComputeRayConeLod(Tex) + GlobalTextureMipBias);
#else
	return Tex.Sample(Sampler, UV);
#endif
}


//converts an input 1d to 2d position. Useful for locating z frames that have been laid out in a 2d grid like a flipbook.
float2 Tile1Dto2D(float xsize, float idx)
{
	float2 xyidx = 0;
	xyidx.y = floor(idx / xsize);
	xyidx.x = idx - xsize * xyidx.y;

	return xyidx;
}

// return a pseudovolume texture sample.
// useful for simulating 3D texturing with a 2D texture or as a texture flipbook with lerped transitions
// treats 2d layout of frames a 3d texture and performs bilinear filtering by blending with an offset Z frame.
// Wrap repeat mode along XY is not seamless. This is however enough for current sampling use cases all in [0,1].
// @param Tex          = Input Texture Object storing Volume Data
// @param inPos        = Input float3 for Position, 0-1
// @param xysize       = Input float for num frames in x,y directions
// @param numframes    = Input float for num total frames
// @param mipmode      = Sampling mode: 0 = use miplevel, 1 = use UV computed gradients, 2 = Use gradients (default=0)
// @param miplevel     = MIP level to use in mipmode=0 (default 0)
// @param InDDX, InDDY = Texture gradients in mipmode=2
float4 PseudoVolumeTexture(Texture2D Tex, SamplerState TexSampler, float3 inPos, float2 xysize, float numframes,
	uint mipmode = 0, float miplevel = 0, float2 InDDX = 0, float2 InDDY = 0)
{
	float z = inPos.z - 0.5f / numframes;	// This offset is needed to have a behavior consistent with hardware sampling (voxel value is at their center)
	float zframe = floor(z * numframes);
	float zphase = frac(z * numframes);

	float2 uv = frac(inPos.xy) / xysize;

	float2 curframe = Tile1Dto2D(xysize.x, zframe) / xysize;
	float2 nextframe = Tile1Dto2D(xysize.x, zframe + 1) / xysize;

	float2 uvCurFrame = uv + curframe;
	float2 uvNextFrame = uv + nextframe;

	#if COMPILER_GLSL_ES3_1
	uvCurFrame.y = 1.0 - uvCurFrame.y;
	uvNextFrame.y = 1.0 - uvNextFrame.y;
	#endif

	float4 sampleA = 0, sampleB = 0;
	switch (mipmode)
	{
	case 0: // Mip level
		sampleA = Tex.SampleLevel(TexSampler, uvCurFrame, miplevel);
		sampleB = Tex.SampleLevel(TexSampler, uvNextFrame, miplevel);
		break;
	case 1: // Gradients automatic from UV
		sampleA = Texture2DSample(Tex, TexSampler, uvCurFrame);
		sampleB = Texture2DSample(Tex, TexSampler, uvNextFrame);
		break;
	case 2: // Deriviatives provided
		sampleA = Tex.SampleGrad(TexSampler, uvCurFrame,  InDDX, InDDY);
		sampleB = Tex.SampleGrad(TexSampler, uvNextFrame, InDDX, InDDY);
		break;
	default:
		break;
	}

	return lerp(sampleA, sampleB, zphase);
}

float Luminance( float3 LinearColor )
{
	return dot( LinearColor, float3( 0.3, 0.59, 0.11 ) );
}

float Length2(float2 v)
{
	return dot(v, v);
}
float Length2(float3 v)
{
	return dot(v, v);
}
float Length2(float4 v)
{
	return dot(v, v);
}

float Square( float x )
{
	return x*x;
}

float2 Square( float2 x )
{
	return x*x;
}

float3 Square( float3 x )
{
	return x*x;
}

float4 Square( float4 x )
{
	return x*x;
}

float Pow2( float x )
{
	return x*x;
}

float2 Pow2( float2 x )
{
	return x*x;
}

float3 Pow2( float3 x )
{
	return x*x;
}

float4 Pow2( float4 x )
{
	return x*x;
}

float Pow3( float x )
{
	return x*x*x;
}

float2 Pow3( float2 x )
{
	return x*x*x;
}

float3 Pow3( float3 x )
{
	return x*x*x;
}

float4 Pow3( float4 x )
{
	return x*x*x;
}

float Pow4( float x )
{
	float xx = x*x;
	return xx * xx;
}

float2 Pow4( float2 x )
{
	float2 xx = x*x;
	return xx * xx;
}

float3 Pow4( float3 x )
{
	float3 xx = x*x;
	return xx * xx;
}

float4 Pow4( float4 x )
{
	float4 xx = x*x;
	return xx * xx;
}

float Pow5( float x )
{
	float xx = x*x;
	return xx * xx * x;
}

float2 Pow5( float2 x )
{
	float2 xx = x*x;
	return xx * xx * x;
}

float3 Pow5( float3 x )
{
	float3 xx = x*x;
	return xx * xx * x;
}

float4 Pow5( float4 x )
{
	float4 xx = x*x;
	return xx * xx * x;
}

float Pow6( float x )
{
	float xx = x*x;
	return xx * xx * xx;
}

float2 Pow6( float2 x )
{
	float2 xx = x*x;
	return xx * xx * xx;
}

float3 Pow6( float3 x )
{
	float3 xx = x*x;
	return xx * xx * xx;
}

float4 Pow6( float4 x )
{
	float4 xx = x*x;
	return xx * xx * xx;
}

// Only valid for x >= 0
float AtanFast( float x )
{
	// Minimax 3 approximation
	float3 A = x < 1 ? float3( x, 0, 1 ) : float3( 1/x, 0.5 * PI, -1 );
	return A.y + A.z * ( ( ( -0.130234 * A.x - 0.0954105 ) * A.x + 1.00712 ) * A.x - 0.00001203333 );
}

// DeviceZ - value that is stored in the depth buffer (Z/W)
// SceneDepth - linear in world units, W
float ConvertFromDeviceZ(float DeviceZ)
{
	// Supports ortho and perspective, see CreateInvDeviceZToWorldZTransform() in Camera.cpp
	// FIXME: array index not work float4[2] ???
	return DeviceZ * _View.InvDeviceZToWorldZTransform.x + _View.InvDeviceZToWorldZTransform.y +
		1.0f / (DeviceZ * _View.InvDeviceZToWorldZTransform.z + _View.InvDeviceZToWorldZTransform.w); // original `-`, here Z axis is out
}

// Inverse operation of ConvertFromDeviceZ()
float ConvertToDeviceZ(float SceneDepth)
{
	SceneDepth = -abs(SceneDepth); // Z axis is out
	[flatten]
	if (_View.ProjMatrix[3][3] < 1.0f)
	{
		// Perspective
		// 1.0f / ((SceneDepth + _View.InvDeviceZToWorldZTransform.w) * _View.InvDeviceZToWorldZTransform.z);
		return (1.0f / SceneDepth - _View.InvDeviceZToWorldZTransform.w) / _View.InvDeviceZToWorldZTransform.z;
	}
	else
	{
		// 
		return SceneDepth * _View.ProjMatrix[2][2] + _View.ProjMatrix[3][2];
	}
}

// ZMagic = (zFar - zNear) / zNear
// N*F / (N + (F-N)*z')
float LinearEyeDepth01(float DeviceZ)
{
	return 1.0f / (1.0f + DeviceZ * _View.ZMagic);
}

float LinearEyeDepth(float DeviceZ)
{
	return _View.ZFar / (1.0f + DeviceZ * _View.ZMagic);
}

#endif // COMMON_INCLUDED
