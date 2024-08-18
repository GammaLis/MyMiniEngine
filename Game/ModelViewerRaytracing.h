#pragma once

#ifndef HLSL
#include "Core/HlslCompat.h"

#define OUT_PARAMETER(X) X&

#else

#define OUT_PARAMETER(X) out X

#endif

static const float EPS = 1.0e-4f;

#define STANDARD_RAY_INDEX 0
#define SHADOW_RAY_INDEX 1

#define DIRECTIONAL_LIGHT 0
#define POINT_LIGHT 1

#define ALPHA_MODE_OPAQUE 0
#define ALPHA_MODE_BLEND 1
#define ALPHA_MODE_MASK 2

#define INVALID_ID (-1)

#define ATTRIBUTES_NEED_TANGENT 1
#define ATTRIBUTES_NEED_TEXCOORD0 1
// Bitangent
#define ATTRIBUTES_NEED_COLOR 1

#ifdef HLSL
struct RayPayload
{
	bool bSkipShading;
	float RayHitT;
};
#endif 

// Volatile part (can be split into its own CBV).
struct DynamicCB
{
	float4x4 cameraToWorld;
	float4 worldCameraPosition; // xyz - camPos, w - unused
	float4 backgroundColor;
	float2 resolution;
	int frameIndex;
	int accumulationIndex;
};

struct LightData
{
	float3 pos;
	float radiusSq;

	float3 color;
	uint type;

	float3 coneDir;
	float2 coneAngles;	// x = 1.0f / (cos(coneInner) - cos(coneOuter)), y = cos(coneOuter)
	float4x4 shadowTextureMatrix; // unused now, just keep with ForwardPlusLighting.h
};

#ifdef HLSL
#ifndef SINGLE
static const float FLT_MAX = 3.402823466e+38; //  asfloat(0x7F7FFFFF);
#endif

cbuffer HitConstants : register(b0)
{
	float3 _SunDirection;
	float3 _SunColor;
	float3 _AmbientColor;
	float4 _ShadowTexelSize;
	float4x4 _ModelToShadow;
	uint _MaxBounces;
	uint _IsReflection;
	uint _UseShadowRays;
}

cbuffer CB1 : register(b1)
{
	DynamicCB _Dynamics;
}

// RaytracingAccelerationStructure g_Accel		: register(t0);

// Output buffer with accumulated and tonemapped image
RWTexture2D<float4> g_ScreenOutput			: register(u2);
RWTexture2D<float4> g_AccumulationOutput	: register(u3);

inline void GenerateCameraRay(uint2 index, out float3 origin, out float3 direction)
{
	float2 xy = index + 0.5; // center in the middle of the pixel
	float2 screenPos = xy / _Dynamics.resolution * 2.0 - 1.0;

	// Invert Y for DirectX-style coordinates
	screenPos.y = -screenPos.y;

	// Unproject into a ray
#if 0
	float4 unprojected = mul(_Dynamics.cameraToWorld, float4(screenPos, 0, 1));
#else
	float4 unprojected = mul(float4(screenPos, 0, 1), _Dynamics.cameraToWorld);
#endif
	float3 world = unprojected.xyz / unprojected.w;
	origin = _Dynamics.worldCameraPosition.xyz;
	direction = normalize(world - origin);
}

inline float3 DirectionalColor(float3 rd)
{
	float t = 0.5f * (rd.y + 1.0f);
	return lerp(float3(1, 1, 1), float3(0.5, 0.7, 1.0), t);
}

float2 STtoUV(float2 st)
{
	return (st + 0.5) * rcp(_Dynamics.resolution);
}

bool IsValidScreenSample(float2 pixel)
{
	return all(pixel >= 0) && all(pixel < _Dynamics.resolution);
}

#endif // HLSL

// Functions for encoding/decoding material and geometry ID into single integer
inline uint PackInstanceID(uint materialID, uint geometryID)
{
	return ((geometryID & 0x3FFF) << 10) | (materialID & 0x3FF);
}

inline void UnpackInstanceID(uint instanceID, OUT_PARAMETER(uint) materialID, OUT_PARAMETER(uint) geometryID)
{
	materialID = instanceID & 0x3FF;
	geometryID = (instanceID >> 10) & 0x3FFF;
}
