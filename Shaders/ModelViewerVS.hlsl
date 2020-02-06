#include "ModelViewerRS.hlsli"

cbuffer VSConstants : register(b0)
{
	float4x4 _ModelToProjection;
	float3 _CamPos;
}

struct VSInput
{
	float3 position : POSITION;
	float2 uv 		: TEXCOORD0;
	float3 normal 	: NORMAL;
	float3 tangent	: TANGENT;
	float3 bitangent: BITANGENT;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	float3 worldPos : WorldPos;
	float2 uv 		: TEXCOORD0;
	float3 viewDir	: TEXCOORD1;
	float3 normal 	: NORMAL;
	float3 tangent 	: TANGENT;
	float3 bitangent: BITANGENT;
};

[RootSignature(ModelViewer_RootSig)]
VSOutput main(VSInput v)
{
	VSOutput o;

	// o.position = mul(float4(v.position, 1.0), _ModelToProjection);
	o.position = mul(_ModelToProjection, float4(v.position, 1.0));
	o.worldPos = v.position;
	o.uv = v.uv;
	o.viewDir = v.position - _CamPos;
	o.normal = v.normal;
	o.tangent = v.tangent;
	o.bitangent = v.bitangent;

	return o;
}
