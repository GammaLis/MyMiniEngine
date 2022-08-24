#ifndef RAYTRACING_COMMON_INCLUDED
#define RAYTRACING_COMMON_INCLUDED

//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author(s):    James Stanard, Christopher Wallis
//
#define HLSL
#include "../Random.hlsl"
#include "../MonteCarlo.hlsl"
#include "../BSDF.hlsl"
#include "../../ModelViewerRaytracing.h"
#include "../../Core/RayTracing/RayTracingHlslCompat.h"

cbuffer CBMaterial : register(b3, space1)
{
	uint _MaterialID;
}

StructuredBuffer<RayTraceMeshInfo> _MeshInfo : register(t1);
ByteAddressBuffer _Indices		: register(t2);
ByteAddressBuffer _Attributes	: register(t3);

StructuredBuffer<LightData> _LightBuffer : register(t4);

Texture2D<float> _TexShadow		: register(t5);
Texture2D<float> _TexSSAO		: register(t6);

Texture2D<float4> _LocalTexture : register(t6, space1);
Texture2D<float4> _LocalNormal	: register(t7, space1);

Texture2D<float4> _TexNormal	: register(t13);

SamplerState _S0 : register(s0);
SamplerComparisonState SamplerShadow : register(s1);


/// Ray

// Raycone structure that define that state of the ray
struct FRayCone
{
	float Width;
	float SpreadAngle;
};

// Structure that defines the current state of the intersection
struct FRayIntersection
{
	// Origin of the current ray -- can be obtained by WorldRayPosition()
	// float3 Origin;
	// Distance of the intersection
	float T;
	// Value that holds the color of the ray
	float3 Color;
	// Cone representation of the ray
	FRayCone Cone;
	// The remaining available depth for the current ray
	uint RemainingDepth;
	// Current sample index
	uint SampleIndex;
	// Ray counter (used for multibounce)
	uint RayCount;
	// Pixel coordinate from which the ray was launched
	uint2 PixelCoord;
	// Velocity for the intersection point
	float Velocity;
};

// For path tracing
// Structure that defines the current state of the intersection
struct FPathIntersection
{
    float T;
    // Resulting value (often color) of the ray
    float3 Value;
    // Cone representation of the ray
    FRayCone Cone;
    // The remaining available depth for the current ray
    uint RemainingDepth;
    // Pixel coordinate from which the initial ray was launched
    uint2 PixelCoord;
    // Max roughness encountered along the path
    float MaxRoughness;
};

// Simple path tracing
struct FHitInfo
{
	float4 encodedNormals;

	float3 hitPosition;
	uint materialID;

	float3 color;
	float T;
	uint uvs;

	bool HasHit()
	{
		return T > 0;
	}
};

struct FAttributeData
{
	// Barycentric value of the intersection
	float2 Barycentrics;
};

// Clever offset_ray function from <<Ray Tracing Gems>> Chapter 6
// Offsets the ray origin from current position p, along normal n (which must be geometric normal)
// so that no self-intersection can occur.
float3 OffsetRay(const float3 p, const float3 n)
{
	static const float origin = 1.0f / 32.0f;
	static const float float_scale = 1.0f / 65536.0f;
	static const float int_scale = 256.0f;

	int3 of_i = int3(int_scale * n);
	float3 p_i = float3(asfloat( asint(p) + ((p < 0) ? -of_i : of_i) ));
	return (abs(p) < origin ? p + float_scale * n : p_i);
}

// Casts a shadow ray and returns true if it is unoccluded
bool CastShadowRay(float3 position, float3 normal, float3 direction, float tMax)
{
	RayDesc ray;
	ray.Origin = OffsetRay(position, normal);
	ray.Direction = direction;
	ray.TMin = 0.1f; // TODO: 0.1f
	ray.TMax = tMax;

	FHitInfo payload = (FHitInfo)0;
	payload.T = 1.0f; // Initialize T to 1, it will be set to -1 on a miss

	TraceRay(
		g_Accel,
		RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
		0xFF,
		0, 1, 0,
		ray, payload);

	return !payload.HasHit();
}


/// Helpers to convert between linear and sRGB color spaces

// Conversion between linear and sRGB color spaces
inline float LinearToSrgb(float linearColor)
{
	if (linearColor < 0.0031308f) return linearColor * 12.92f;
	else return 1.055f * float(pow(linearColor, 1.0f / 2.4f)) - 0.055f;
}

inline float SrgbToLinear(float srgbColor)
{
	if (srgbColor < 0.04045f) return srgbColor / 12.92f;
	else return float(pow((srgbColor + 0.055f) / 1.055f, 2.4f));
}

float3 LinearToSrgb(float3 linearColor)
{
	return float3(LinearToSrgb(linearColor.x), LinearToSrgb(linearColor.y), LinearToSrgb(linearColor.z));
}

float3 SrgbToLinear(float3 srgbColor)
{
	return float3(SrgbToLinear(srgbColor.x), SrgbToLinear(srgbColor.y), SrgbToLinear(srgbColor.z));
}

/// Helpers for octahedron encoding of normals
float2 OctWrap(float2 v)
{
	return float2((1.0f - abs(v.y)) * (v.x >= 0.0f ? 1.0f : -1.0f), (1.0f - abs(v.x)) * (v.y >= 0.0f ? 1.0f : -1.0f));
}

float2 EncodeNormalOctahedron(float3 n)
{
	float2 p = float2(n.x, n.y) * (1.0f / (abs(n.x) + abs(n.y) + abs(n.z)));
	p = (n.z < 0.0f) ? OctWrap(p) : p;
	return p;
}

float3 DecodeNormalOctahedron(float2 p)
{
	float3 n = float3(p.x, p.y, 1.0f - abs(p.x) - abs(p.y));
	float2 tmp = (n.z < 0.0f) ? OctWrap(float2(n.x, n.y)) : float2(n.x, n.y);
	n.x = tmp.x;
	n.y = tmp.y;
	return normalize(n);
}

#endif // RAYTRACING_COMMON_INCLUDED
