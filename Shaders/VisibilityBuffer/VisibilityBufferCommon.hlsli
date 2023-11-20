#ifndef VISIBILITY_BUFFER_COMMON_INCLUDED
#define VISIBILITY_BUFFER_COMMON_INCLUDED

#include "../MeshDefines.hlsli"
#include "../MaterialDefines.hlsli"
#include "../../Scenes/MaterialDefines.h"

#define MATERIAL_TEXTURE_NUM 5

#define USE_VIEW_UNIFORMS 1
#define PACK_UNORM 1

struct ViewUniformParameters
{
	float4x4 viewProjMat;
	float4x4 invViewProjMat;
	float4x4 viewMat;
	float4x4 projMat;
	float4 bufferSizeAndInvSize;
	float4 camPos;
	float4 cascadeSplits;
	float nearClip, farClip;
};

cbuffer CBConstants	: register(b0)
{
	float4 _Constants;
};

ConstantBuffer<ViewUniformParameters> _View 	: register(b1);

StructuredBuffer<GlobalMatrix> _MatrixBuffer	: register(t0, space1);
StructuredBuffer<MaterialData> _MaterialBuffer	: register(t1, space1);
StructuredBuffer<MeshDesc> _MeshBuffer 			: register(t2, space1);
StructuredBuffer<MeshInstanceData> _MeshInstanceBuffer 	: register(t3, space1);

SamplerState s_LinearClampSampler 	: register(s0);
SamplerState s_LinearRepeatSampler 	: register(s1);
SamplerState s_PointClampSampler 	: register(s2);

#if defined(SHADER_VS) || defined(SHADER_PS)

struct Attributes
{
	uint drawId	: DRAWID; // per instance data
};

struct Varyings
{
	float4 position : SV_POSITION;
	float2 uv0 		: TEXCOORD0;
	float3 normal 	: TEXCOORD1;
	nointerpolation uint drawId 	: DRAWID;
};

#endif

// Ref: UE - RayTracingDebug.usf
uint MurmurMix(uint Hash)
{
    Hash ^= Hash >> 16;
    Hash *= 0x85ebca6b;
    Hash ^= Hash >> 13;
    Hash *= 0xc2b2ae35;
    Hash ^= Hash >> 16;
    return Hash;
}

float3 IntToColor(uint Index)
{
    uint Hash  = MurmurMix(Index); 

    float3 Color = float3
    (
        (Hash >>  0) & 255,
        (Hash >>  8) & 255,
        (Hash >> 16) & 255
    );

    return Color * (1.0f / 255.0f);
}

// in visibiityPass.shd
uint CalcVBID(bool opaque, uint drawId, uint primitiveId)
{
	uint drawPrimitiveId = ((drawId << 23) & 0x7F800000) | (primitiveId & 0x007FFFFF);
	return opaque ? drawPrimitiveId : ((1 << 31) | drawPrimitiveId);
}

uint PackUnorm2x16(float2 v)
{
	uint2 UNorm = uint2(round(saturate(v) * 65535.0));
	return (0x0000FFFF & UNorm.x) | ((UNorm.y << 16) & 0xFFFF0000);
}

float2 UnpackUnorm2x16(uint p)
{
	float2 ret;
	ret.x = saturate((0x0000FFFF & p) / 65535.0);
	ret.y = saturate(((0xFFFF0000 & p) >> 16) / 65535.0);
	return ret;
}

uint PackUnorm4x8(float4 v)
{
	uint4 UNorm = uint4(round(saturate(v) * 255.0));
	return (0x000000FF & UNorm.x) | ((UNorm.y << 8) & 0x0000FF00) | ((UNorm.z << 16) & 0x00FF0000) | ((UNorm.w << 24) & 0xFF000000);
}

float4 UnpackUnorm4x8(uint p)
{
	return float4(
		float( p & 0x000000FF) 		  / 255.0,
		float((p & 0x0000FF00) >> 8 ) / 255.0,
		float((p & 0x00FF0000) >> 16) / 255.0, 
		float((p & 0xFF000000) >> 24) / 255.0);
}

void ComputePartialDerivatives(float2 v[3], out float3 db_dx, out float3 db_dy)
{
	float D = 1.0 / determinant(float2x2(v[2] - v[1], v[0] - v[1]));
	db_dx = float3(v[1].y - v[2].y, v[2].y - v[0].y, v[0].y - v[1].y) * D;
	db_dy = float3(v[2].x - v[1].x, v[0].x - v[2].x, v[1].x - v[0].x) * D;
}

#endif // VISIBILITY_BUFFER_COMMON_INCLUDED
