#pragma once

#ifndef HLSL
#include "Core/HlslCompat.h"
#endif

static const float EPS = 1.0e-4f;

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
	uint _IsReflection;
	uint _UseShadowRays;
}

cbuffer CB1 : register(b1)
{
	DynamicCB _Dynamics;
}

RaytracingAccelerationStructure g_Accel : register(t0);

RWTexture2D<float4> g_ScreenOutput : register(u2);

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

#endif // HLSL
