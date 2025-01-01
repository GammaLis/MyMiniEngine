#ifndef USE_DESCRIPTOR_HEAP_INDEX
#define USE_DESCRIPTOR_HEAP_INDEX 1
#endif

#ifndef USE_SIMPLE_VERTEX
#define USE_SIMPLE_VERTEX 1
#endif

#if USE_DESCRIPTOR_HEAP_INDEX
#include "DynDescRS.hlsli"
#endif

#if USE_SIMPLE_VERTEX

struct VSInput
{
	float3 position : POSITION;
	float2 uv0		: TEXCOORD0;
	float3 normal 	: NORMAL;
};

// TODO: optimization
struct VSOutput
{
	float4 pos 	: SV_POSITION;
	float2 uv0 	: TEXCOORD0;
	float2 uv1	: TEXCOORD1;
	float3 worldPos	: TEXCOORD2;
	float3 normal 	: NORMAL;
	float3 tangent 	: TANGENT;
	float3 bitangent: TEXCOORD3;
	float3 color 	: COLOR0;
};

#else

struct VSInput
{
	float3 position : POSITION;
	float2 uv0 : TEXCOORD0;
	float2 uv1 : TEXCOORD1;
	float3 normal : NORMAL;
	float4 tangent : TANGENT;
	float3 color : COLOR0;
};

struct VSOutput
{
	float4 pos : SV_POSITION;
	float2 uv0 : TEXCOORD0;
	float2 uv1 : TEXCOORD1;
	float3 worldPos : TEXCOORD2;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float3 bitangent : TEXCOORD3;
	float3 color : COLOR0;
};
#endif

struct CBPerObject
{
	float4x4 worldMat;
	float4x4 invWorldMat;
};

struct CBPerCamera
{
	float4x4 viewProjMat;
	float4x4 camPos;
};

cbuffer CBConstants : register(b0, space1)
{
	float4 _Miscs;
}

#if USE_DESCRIPTOR_HEAP_INDEX
[RootSignature(DynResource_RootSig)]
#else
[RootSignature(Common_RootSig)]
#endif
VSOutput main(VSInput v)
{
	VSOutput o = (VSOutput) 0;

	const uint DrawId = uint(_Miscs.x);

#if USE_DESCRIPTOR_HEAP_INDEX
	// ConstantBuffer<CBPerObject> cbPerObjects = ResourceDescriptorHeap[0];
	// const CBPerObject cbPerObject = cbPerObjects[DrawId];
	// const float4x4 WorldMat = cbPerObjects.worldMat;
	const float4x4 WorldMat = float4x4(
		float4(1, 0, 0, 0),
		float4(0, 1, 0, 0),
		float4(0, 0, 1, 0),
		float4(0, 0, 0, 1));

	ConstantBuffer<CBPerCamera> cbPerCamera = ResourceDescriptorHeap[1];
	const float4x4 ViewProjMat = cbPerCamera.viewProjMat;
#else
	const float4x4 WorldMat = _WorldMat;
	const float4x4 ViewProjMat = _ViewProjMat;
#endif

#if 0
	float4 wPos = mul(float4(v.position, 1.0), _WorldMat);
#else
	// worldMat is not transposed
	float4 wPos = mul(WorldMat, float4(v.position, 1.0));
#endif
	// wPos = float4(v.position, 1.0);
	float4 cPos = mul(wPos, ViewProjMat);

	// float3 wNormal = normalize(mul((float3x3)_InvWorldMat, v.normal));
	// No uniform scale here
	float3 wNormal = normalize(mul((float3x3) WorldMat, v.normal));
	// TODO: no tangents yet
	float3 wTangent = float3(0, 0, 0); // normalize(mul(v.tangent.xyz, (float3x3)_WorldMat));
	float3 wBitangent = float3(0, 0, 0); // cross(wNormal, wTangent) * v.tangent.w;

	o.pos = cPos;
	o.worldPos = wPos.xyz;

	float2 uv0 = v.uv0;
	// TODO: no uv1 yet
	float2 uv1 = v.uv0; // v.uv1;
#ifdef GL_UV_STARTS_AT_BOTTOMLEFT
	// o.uv0 = float2(v.uv0.x, 1.0 - v.uv0.y);
	// o.uv1 = float2(v.uv1.x, 1.0 - v.uv1.y
#endif
	o.uv0 = uv0;
	o.uv1 = uv1;

	o.normal = wNormal;
	o.tangent = wTangent;
	o.bitangent = wBitangent;
	// TODO: no color yet
	// o.color = float3(0, 0, 0); // v.color;

	return o;
}
