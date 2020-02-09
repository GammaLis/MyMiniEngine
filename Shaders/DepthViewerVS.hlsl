#include "ModelViewerRS.hlsli"

cbuffer VSConstants	: register(b0)
{
	float4x4 _ModelToProjection;
};

struct VSInput
{
	float3 position : POSITION;
	float2 texcoord0: TEXCOORD0;
	float3 normal 	: NORMAL;
	float3 tangent	: TANGENT;
	float3 bitangent: BITANGENT;
};

struct VSOutput
{
	float4 pos 	: SV_POSITION;
	float2 uv 	: TEXCOORD0;
};

[RootSignature(ModelViewer_RootSig)]
VSOutput main(VSInput v)
{
	VSOutput o;
	o.pos = mul(float4(v.position, 1.0), _ModelToProjection);
	o.uv = v.texcoord0;
	
	return o;
}