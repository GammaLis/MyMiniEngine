#include "AiCommonRS.hlsli"

cbuffer CBConstants	: register(b0)
{
	float4 _Constants;
};
cbuffer CBPerObject	: register(b1)
{
	matrix _WorldMat;
	matrix _InvWorldMat;
};
cbuffer CBPerCamera	: register(b2)
{
	matrix _ViewProjMat;
	float3 _CamPos;
};

struct VSInput
{
	float3 position : POSITION;
	float3 normal 	: NORMAL;
	float3 tangent	: TANGENT;
	float3 bitangent: BITANGENT;
	float2 uv0		: TEXCOORD0;
};

struct VSOutput
{
	float4 pos 	: SV_POSITION;
	float2 uv0 	: TEXCOORD0;
	float3 worldPos	: TEXCOORD1;
	float3 normal 	: NORMAL;
	float3 tangent 	: TANGENT;
	float3 bitangent: BITANGENT;
};

[RootSignature(Common_RootSig)]
VSOutput main(VSInput v)
{
	VSOutput o;

	float4 wPos = mul(float4(v.position, 1.0), _WorldMat);
	// wPos = float4(v.position, 1.0);
	float4 cPos = mul(wPos, _ViewProjMat);

	float3 wNormal = normalize(mul((float3x3)_InvWorldMat, v.normal));
	float3 wTangent = normalize(mul(v.tangent.xyz, (float3x3)_WorldMat));
	float3 wBitangent = normalize(mul(v.bitangent.xyz, (float3x3)_WorldMat));

	o.pos = cPos;
	o.worldPos = wPos.xyz;
	o.normal = wNormal;
	o.tangent = wTangent;
	o.bitangent = wBitangent;
	o.uv0 = v.uv0;

	return o;
}
