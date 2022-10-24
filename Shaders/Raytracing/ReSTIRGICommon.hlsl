#ifndef RESTIRGI_COMMON_INCLUDED
#define RESTIRGI_COMMON_INCLUDED

#ifndef RTRT
#define RTRT 0
#endif

#include "ModelViewerRTInputs.hlsl"
#if RTRT
#include "RayTracingCommon.hlsl"
#include "RayTracingIntersection.hlsl"
#include "RayTracingLightingCommon.hlsl"
#include "ShaderPassPathTracing.hlsl"

#else
#include "../Common.hlsl"
#include "../Random.hlsl"
#include "../MonteCarlo.hlsl"
#include "../BSDF.hlsl"
#include "ModelViewerRTInputs.hlsl"
#endif

#define GROUP_SIZE 8
#define ROOT_SPACE space1

/// Settings
#define HALF_RESOLUTION 1
#define RESTIR_TEMPORAL_M_CLAMP 20
// Reduces fireflies, but causes darkening in corners
#define RESTIR_RESERVOIR_W_CLAMP 10.0
#define ROUGHNESS_BIAS 0.5
// Should be 1, but rarely matters for the diffuse bounce, so might as well save a few cycles
#define USE_SOFT_SHADOWS 0
#define USE_IRRADIANCE_CACHE 0
#define USE_WORLD_RADIANCE_CACHE 0

#define TEMPORAL_USE_PERMUTATIONS 0

#define USE_JACOBIAN_BASED_REJECTION 0
#define JACOBIAN_BASED_REJECTION_VALUE 8

#define USE_SSGI_REPROJECTION 0
#define USE_EMISSIVE 1
#define USE_LOCAL_LIGHTS 0
#define USE_SKY_CUBEMAP 0
#define USE_RESOLVE_SPATIAL_FILTER 1

#define MAX_RESOLVE_SAMPLE_COUNT 5

#ifndef RESERVOIR_EPS 
#define RESERVOIR_EPS 1e-4
#endif

/// Inputs
RWTexture2D<float3> RWSampleRadiance	: register(u4, ROOT_SPACE);
RWTexture2D<float4> RWSampleNormal		: register(u5, ROOT_SPACE);
RWTexture2D<float4> RWSampleHitInfo 	: register(u6, ROOT_SPACE);
RWTexture2D<float3> RWRayOrigin 		: register(u7, ROOT_SPACE);
RWTexture2D<uint2>  RWReservoir 		: register(u8, ROOT_SPACE);

// VelocityBuffer : register(t14)
Texture2D<float3> _SampleRadiance		: register(t15, ROOT_SPACE);
Texture2D<float3> _SampleNormal			: register(t16, ROOT_SPACE);
Texture2D<float4> _SampleHitInfo		: register(t17, ROOT_SPACE);

#if 0
Texture2D<float3> _ReservoirPosition	: register(t18, ROOT_SPACE);
Texture2D<float3> _ReservoirNormal		: register(t19, ROOT_SPACE);
#else
Texture2D<float4> _TemporalSampleRadiance	: register(t18, ROOT_SPACE);
Texture2D<float4> _TemporalSampleNormal	: register(t19, ROOT_SPACE);
Texture2D<float4> _TemporalHitInfo		: register(t20, ROOT_SPACE);
Texture2D<float3> _TemporalRayOrigin	: register(t21, ROOT_SPACE);
Texture2D<uint2>  _TemporalReservoir	: register(t22, ROOT_SPACE);
#endif

/// ReSTIR
struct FReservoir
{
	uint payload;
	float targetPdf; // not saved
	float M;
	float W;

	uint2 Pack()
	{
		uint2 packed = 0;
		packed.x = payload;
		packed.y = f32tof16(M) | (f32tof16(W) << 16);
		return packed;
	}
};
	
FReservoir CreateGIReservoir()
{
	FReservoir r = (FReservoir)0;
	r.payload = 0;
	r.targetPdf = 1.0;
	r.M = 0;
	r.W = 0;
	return r;
}

FReservoir CreateGIReservoir(uint2 packed)
{
	FReservoir r = (FReservoir)0;
	r.payload = packed.x;
	r.targetPdf = 1.0;
	r.M = f16tof32(  packed.y & 0xFFFF );
	r.W = f16tof32( (packed.y >> 16) & 0xFFFF );
	return r;
}

bool UpdateReservoir(inout FReservoir r, uint payload, float targetPdf, float weight, float random)
{
	r.W += weight;
	r.M += 1;
	if (random * r.W < weight)
	{
		r.payload = payload;
		r.targetPdf = targetPdf;
		return true;
	}

	return false;
}

bool CombineReservoirs(inout FReservoir r, FReservoir rq, float random, float weight = 1.0, float targetPdf = 1.0)
{
	r.M += rq.M;
	float risWeight = targetPdf * weight * (rq.W * rq.M);
	r.W += risWeight;
	if (random * r.W < risWeight)
	{
		r.payload = rq.payload;
		r.targetPdf = targetPdf;
		return true;
	}

	return false;
}

// Performs normalization of the reservoir after streaming. Equation (6) from the ReSTIR paper.
void FinalizeReservoir(inout FReservoir r)
{
	float denominator = r.targetPdf * r.M;
	r.W = denominator < RESERVOIR_EPS ? 0.0 : r.W / denominator;
}

uint2 ReservoirPayloadToPixel(uint payload)
{
	return uint2(payload & 0xFFFF, payload >> 16);
}

/// Functions

static const uint2 s_PixelOffsets[] =
{
	uint2(0, 0), uint2(1, 0), uint2(0, 1), uint2(1, 1)
};

uint2 GetFramePixel(uint2 launchIndex)
{
#if HALF_RESOLUTION
	return 2 * launchIndex + s_PixelOffsets[_View.FrameIndex & 3];
#else
	return launchIndex;
#endif
}

float InverseDepthRelativeDiff(float d0, float d1)
{
	return abs(max(1e-5, d0) / max(1e-5, d1) - 1.0);
}

#if 0
// Ref: Falcor - ScreenSpaceReSTIR
struct PackedGIReservoir
{
	uint4 CreationGeometry; // visible point's position and normal
	uint4 HitGeometry; // hit point's position and normal
	uint4 LightInfo; // reservoior information
};

struct GIReservoir 
{
	float3 CreationPoint; 	// visible point's position
	float3 CreationNormal; 	// visible point's normal
	float3 HitPosition;		// hit point's position
	float3 HitNormal;		// hit point's normal
	float3 HitRadiance;		// chosen sample's radiance
	int M;		// input sample count
	float W;	// weight for chosen sample
	uint Age;	// number of frames the sample has survived

	PackedGIReservoir Pack()
	{
		PackedGIReservoir packed;
		packed.CreationGeometry.xyz = asuint(CreationPoint);
		packed.CreationGeometry.w = EncodeNormal2x16(CreationNormal);
		packed.HitGeometry.xyz = asuint(HitPosition);
		packed.HitGeometry.w = EncodeNormal2x16(HitNormal);
		packed.LightInfo.x = f32tof16(HitRadiance.x) | (f32tof16(HitRadiance.y) << 16);
		packed.LightInfo.y = f32tof16(HitRadiance.y) | (M << 16);
		packed.LightInfo.z = asuint(W);
		packed.LightInfo.w = Age;
		return packed;
	}
};
#endif

#endif // RESTIRGI_COMMON_INCLUDED
