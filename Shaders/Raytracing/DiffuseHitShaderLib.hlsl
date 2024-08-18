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
#include "../../Game/ModelViewerRaytracing.h"
#include "../../Core/RayTracing/RayTracingHlslCompat.h"

cbuffer CBMaterial : register(b3, space1)
{
	uint _MaterialID;
}

RaytracingAccelerationStructure g_Accel	: register(t0);
StructuredBuffer<RayTraceMeshInfo> _MeshInfo : register(t1);
ByteAddressBuffer _Indices		: register(t2);
ByteAddressBuffer _Attributes	: register(t3);

Texture2D<float> _TexShadow		: register(t6);
Texture2D<float> _TexSSAO		: register(t7);

Texture2D<float4> _LocalTexture : register(t6, space1);
Texture2D<float4> _LocalNormal	: register(t7, space1);

Texture2D<float4> _TexNormal	: register(t13);

SamplerState _S0 : register(s0);
SamplerState SamplerLinearClamp : register(s1);
SamplerState SamplerPointClamp	: register(s2);
SamplerComparisonState SamplerShadow : register(s1);

uint3 Load3x16BitIndices(uint offsetBytes)
{
	const uint dwordAlignedOffset = offsetBytes & ~3;
	const uint2 four16BitIndices = _Indices.Load2(dwordAlignedOffset);

	uint3 indices;

	if (dwordAlignedOffset == offsetBytes)
	{
		indices.x = four16BitIndices.x & 0xFFFF;
		indices.y = (four16BitIndices.x >> 16) & 0xFFFF;
		indices.z = four16BitIndices.y & 0xFFFF;
	}
	else
	{
		indices.x = (four16BitIndices.x >> 16) & 0xFFFF;
		indices.y = four16BitIndices.y & 0xFFFF;
		indices.z = (four16BitIndices.y >> 16) & 0xFFFF;
	}

	return indices;
}

float GetShadow(float3 ShadowCoord)
{
#ifdef SINGLE_SAMPLE
	float result = _TexShadow.SampleCmpLevelZero(SamplerShadow, ShadowCoord.xy, ShadowCoord.z);
#else
	const float dilation = 2.0;
	float2 d1 = dilation * _ShadowTexelSize.xy * 0.125;
	float2 d2 = dilation * _ShadowTexelSize.xy * 0.875;
	float2 d3 = dilation * _ShadowTexelSize.xy * 0.625;
	float2 d4 = dilation * _ShadowTexelSize.xy * 0.375;
	float result = (
		2.0 * _TexShadow.SampleCmpLevelZero(SamplerShadow, ShadowCoord.xy, ShadowCoord.z) +
		_TexShadow.SampleCmpLevelZero(SamplerShadow, ShadowCoord.xy + float2(-d2.x,  d1.y), ShadowCoord.z) +
		_TexShadow.SampleCmpLevelZero(SamplerShadow, ShadowCoord.xy + float2(-d1.x, -d2.y), ShadowCoord.z) +
		_TexShadow.SampleCmpLevelZero(SamplerShadow, ShadowCoord.xy + float2( d2.x, -d1.y), ShadowCoord.z) +
		_TexShadow.SampleCmpLevelZero(SamplerShadow, ShadowCoord.xy + float2( d1.x,  d2.y), ShadowCoord.z) +
		_TexShadow.SampleCmpLevelZero(SamplerShadow, ShadowCoord.xy + float2(-d4.x,  d3.y), ShadowCoord.z) +
		_TexShadow.SampleCmpLevelZero(SamplerShadow, ShadowCoord.xy + float2(-d3.x, -d4.y), ShadowCoord.z) +
		_TexShadow.SampleCmpLevelZero(SamplerShadow, ShadowCoord.xy + float2( d4.x, -d3.y), ShadowCoord.z) +
		_TexShadow.SampleCmpLevelZero(SamplerShadow, ShadowCoord.xy + float2( d3.x,  d4.y), ShadowCoord.z)
		) / 10.0;
#endif	// SINGLE_SAMPLE
	return result * result;
}

float2 GetUVAttribute(uint byteOffset)
{
	return asfloat(_Attributes.Load2(byteOffset));
}

void AntiAliasSpecular(inout float3 texNormal, inout float gloss)
{
	float normalLenSq = dot(texNormal, texNormal);
	float invNormalLen = rsqrt(normalLenSq);
	texNormal *= invNormalLen;
#if 0
	float normalLen = normalLenSq * invNormalLen;
	float flatness = saturate(1 - abs(ddx(normalLen)) - abs(ddy(normalLen)));
	gloss = exp2(lerp(0, log2(gloss), flatness));
#else
	gloss = lerp(1, gloss, rcp(invNormalLen));
#endif
}

// Apply Fresnel to modulate the specular albedo
void FSchlick(inout float3 specular, inout float3 diffuse, float3 lightDir, float3 halfVec)
{
	float fresnel = pow(1.0 - saturate(dot(lightDir, halfVec)), 5.0);
	specular = lerp(specular, 1.0, fresnel);
	diffuse = lerp(diffuse, 0.0, fresnel);
}

float3 ApplyLightCommon(
	float3 diffuseColor,	// diffuse albedo
	float3 specularColor,	// specular color
	float  specularMask,	// where is it shiny or dingy?
	float  gloss,			// specular power
	float3 normal,			// world-space normal
	float3 viewDir,			// world-space vector from eye to point
	float3 lightDir,		// world-space vector from point to light
	float3 lightColor		// radiance of directional light
	)
{
	float3 halfVec = normalize(lightDir - viewDir);
	float NdotH = saturate(dot(halfVec, normal));

	FSchlick(diffuseColor, specularColor, lightDir, halfVec);

	float specularFactor = specularMask * pow(NdotH, gloss) * (gloss + 2) / 8;

	float NdotL = saturate(dot(normal, lightDir));

	return NdotL * lightColor * (diffuseColor + specularFactor * specularColor);
}

float3 RayPlaneIntersection(float3 planeOrigin, float3 planeNormal, float3 rayOrigin, float3 rayDirection)
{
	float t = dot(-planeNormal, rayOrigin - planeOrigin) / (dot(planeNormal, rayDirection) + EPS);
	return rayOrigin + t * rayDirection;
}

/*
    REF: https://gamedev.stackexchange.com/questions/23743/whats-the-most-efficient-way-to-find-barycentric-coordinates
    From "Real-Time Collision Detection" by Christer Ericson
*/
float3 BarycentricCoordinates(float3 pt, float3 v0, float3 v1, float3 v2)
{
	float3 e0 = v1 - v0;
	float3 e1 = v2 - v0;
	float3 e2 = pt - v0;
	float d00 = dot(e0, e0);
	float d01 = dot(e0, e1);
	float d11 = dot(e1, e1);
	float d20 = dot(e2, e0);
	float d21 = dot(e2, e1);
	float denom = 1.0 / (d00 * d11 - d01 * d01);
	float v = (d11 * d20 - d01 * d21) * denom;
	float w = (d00 * d21 - d01 * d20) * denom;
	float u = 1.0 - v - w;
	return float3(u, v, w);
}

[shader("closesthit")]
void Hit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attrib)
{
	payload.RayHitT = RayTCurrent();
	if (payload.bSkipShading)
		return;

	uint materialID = _MaterialID;
	uint triangleID = PrimitiveIndex();

	RayTraceMeshInfo info = _MeshInfo[materialID];

	const uint3 ii = Load3x16BitIndices(info.IndexOffsetBytes + triangleID * 3 * 2);
	const float2 uv0 = GetUVAttribute(info.UVAttributeOffsetBytes + ii.x * info.AttributeStrideBytes);
	const float2 uv1 = GetUVAttribute(info.UVAttributeOffsetBytes + ii.y * info.AttributeStrideBytes);
	const float2 uv2 = GetUVAttribute(info.UVAttributeOffsetBytes + ii.z * info.AttributeStrideBytes);

	float3 bary = float3(1.0 - attrib.barycentrics.x - attrib.barycentrics.y, attrib.barycentrics.x, attrib.barycentrics.y);
	float2 uv = bary.x * uv0 + bary.y * uv1 + bary.z * uv2;

	const float3 normal0 = asfloat(_Attributes.Load3(info.NormalAttributeOffsetBytes + ii.x * info.AttributeStrideBytes));
	const float3 normal1 = asfloat(_Attributes.Load3(info.NormalAttributeOffsetBytes + ii.y * info.AttributeStrideBytes));
	const float3 normal2 = asfloat(_Attributes.Load3(info.NormalAttributeOffsetBytes + ii.z * info.AttributeStrideBytes));
	float3 vsNormal = normalize(bary.x * normal0 + bary.y * normal1 + bary.z * normal2);

	const float3 tangent0 = asfloat(_Attributes.Load3(info.TangentAttributeOffsetBytes + ii.x * info.AttributeStrideBytes));
	const float3 tangent1 = asfloat(_Attributes.Load3(info.TangentAttributeOffsetBytes + ii.y * info.AttributeStrideBytes));
	const float3 tangent2 = asfloat(_Attributes.Load3(info.TangentAttributeOffsetBytes + ii.z * info.AttributeStrideBytes));
	float3 vsTangent = normalize(bary.x * tangent0 + bary.y * tangent1 + bary.z * tangent2);

	// Reintroduced the bitangent because we aren't storing the handedness of the tangent from anywhere. Assuming the space 
	// is right-handed causes normal maps to invert for some surfaces. The Sponza mesh has all 3 axes of the tangent frame.
	// float3 vsBitangent = normalize(cross(vsNormal, vsTangent)) * (isRightHanded ? 1.0 : -1.0);
	const float3 bitangent0 = asfloat(_Attributes.Load3(info.BitangentAttributeOffsetBytes + ii.x * info.AttributeStrideBytes));
	const float3 bitangent1 = asfloat(_Attributes.Load3(info.BitangentAttributeOffsetBytes + ii.y * info.AttributeStrideBytes));
	const float3 bitangent2 = asfloat(_Attributes.Load3(info.BitangentAttributeOffsetBytes + ii.z * info.AttributeStrideBytes));
	float3 vsBitangent = normalize(bary.z * bitangent0 + bary.y * bitangent1 + bary.z * bitangent2);

	// TODO: Should just store uv partial derivatives in here rather than loading position and calculating it per pixel
	const float3 p0 = asfloat(_Attributes.Load3(info.PositionAttributeOffsetBytes + ii.x * info.AttributeStrideBytes));
	const float3 p1 = asfloat(_Attributes.Load3(info.PositionAttributeOffsetBytes + ii.y * info.AttributeStrideBytes));
	const float3 p2 = asfloat(_Attributes.Load3(info.PositionAttributeOffsetBytes + ii.z * info.AttributeStrideBytes));
	
	float3 worldPosition = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

	/**
	 * Compute partial derivatives of UV coordinates:
	 *  1) Construct a plane from the hit triangle
	 *  2) Intersect 2 helper rays with the plane: one to the right and one down
	 *  3) Compute barycentric coordinates of the 2 hit points
	 *  4) Reconstruct the UV coordinates at the hit points
	 *  5) Take the difference in UV coordinates as the partial derivatives X and Y
	*/

	// Normal for the plane
	float3 triangleNormal = normalize(cross(p2 - p0, p1 - p0));

	// Helper rays
	uint2 threadID = DispatchRaysIndex().xy;
	float3 ddxOrigin = 0, ddxDir = 0, ddyOrigin = 0, ddyDir = 0;
	GenerateCameraRay(uint2(threadID.x + 1, threadID.y), ddxOrigin, ddxDir);
	GenerateCameraRay(uint2(threadID.x, threadID.y + 1), ddxOrigin, ddxDir);

	// Intersect helper rays 
	float3 xOffsetPoint = RayPlaneIntersection(worldPosition, triangleNormal, ddxOrigin, ddxDir);
	float3 yOffsetPoint = RayPlaneIntersection(worldPosition, triangleNormal, ddyOrigin, ddyDir);

	// Compute barycentrics
	float3 baryX = BarycentricCoordinates(xOffsetPoint, p0, p1, p2);
	float3 baryY = BarycentricCoordinates(yOffsetPoint, p0, p1, p2);

	// Compute UVs and take the difference
	float3x2 uvMat = float3x2(uv0, uv1, uv2);
	float2 ddxUV = mul(baryX, uvMat) - uv;
	float2 ddyUV = mul(baryY, uvMat) - uv;

	// 
	const float3 diffuseColor = _LocalTexture.SampleGrad(_S0, uv, ddxUV, ddyUV).rgb;
	float3 normal;
	float3 specularAlbedo = float3(0.56, 0.56, 0.56);
	float specularMask = 0; // TODO: read the txture
	float gloss = 128.0;
	{
		normal = _LocalNormal.SampleGrad(_S0, uv, ddxUV, ddyUV).rgb * 2.0 - 1.0;
		AntiAliasSpecular(normal, gloss);
		float3x3 tbn = float3x3(vsTangent, vsBitangent, vsNormal);
		normal = normalize(mul(normal, tbn));
	}

	float3 outColor = _AmbientColor * diffuseColor + _TexSSAO[DispatchRaysIndex().xy];
	float shadow = 1.0;
	if (_UseShadowRays)
	{
		float3 shadowDirection = _SunDirection;
		float3 shadowOrigin = worldPosition;
		RayDesc rayDesc = 
		{
			shadowOrigin, 0.1f, shadowDirection, FLT_MAX
		};
		RayPayload shadowPayload;
		shadowPayload.RayHitT = FLT_MAX;
		shadowPayload.bSkipShading = true;
		TraceRay(g_Accel, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, ~0, 0, 1, 0, rayDesc, shadowPayload);
		if (shadowPayload.RayHitT < FLT_MAX)
		{
			shadow = 0.0;
		}
	}
	else
	{
		// TODO: This could be pre-calculated once per vertex if this mul per pixel was a conern
		float4 shadowCoord = mul(float4(worldPosition, 1.0f), _ModelToShadow);
		shadowCoord.xyz /= shadowCoord.w;
		shadow = GetShadow(shadowCoord.xyz);
	}

	const float3 viewDir = normalize(-WorldRayDirection());

	outColor += shadow * ApplyLightCommon(
		diffuseColor,
		specularAlbedo,
		specularMask,
		gloss,
		normal,
		viewDir,
		_SunDirection,
		_SunColor);

	// TODO: Should be passed in via material info
	if (_IsReflection)
	{
		float reflectivity = _TexNormal[DispatchRaysIndex().xy].w;
		outColor += g_ScreenOutput[DispatchRaysIndex().xy].rgb + reflectivity * outColor;
	}

	g_ScreenOutput[DispatchRaysIndex().xy] = float4(outColor, 1.0);
}

[shader("anyhit")]
void AnyHit(inout RayPayload paylod, BuiltInTriangleIntersectionAttributes attributeData)
{
	uint triangleID = PrimitiveIndex();
	uint geometryID = _MaterialID;
	const RayTraceMeshInfo info = _MeshInfo[geometryID];

	const uint3 ii = Load3x16BitIndices(info.IndexOffsetBytes + triangleID * 3 * 2);
	const float2 uv0 = GetUVAttribute(info.UVAttributeOffsetBytes + ii.x * info.AttributeStrideBytes);
	const float2 uv1 = GetUVAttribute(info.UVAttributeOffsetBytes + ii.y * info.AttributeStrideBytes);
	const float2 uv2 = GetUVAttribute(info.UVAttributeOffsetBytes + ii.z * info.AttributeStrideBytes);

	float3 bary = float3(1.0 - attributeData.barycentrics.x - attributeData.barycentrics.y, attributeData.barycentrics.x, attributeData.barycentrics.y);
	float2 uv = bary.x * uv0 + bary.y * uv1 + bary.z * uv2;

	// Load material properties at the hit point
	const float alpha = _LocalTexture.SampleLevel(_S0, uv, 0).a;
	if (alpha < 0.5f)
	{
		IgnoreHit();
	}
}
