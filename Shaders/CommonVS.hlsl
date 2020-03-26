#include "CommonRS.hlsli"

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
	float2 uv0		: TEXCOORD0;
	float2 uv1		: TEXCOORD1;
	float3 normal 	: NORMAL;
	float4 tangent	: TANGENT;
	float3 color	: COLOR0;
};

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

[RootSignature(Common_RootSig)]
VSOutput main( VSInput v )
{
	VSOutput o;

	float4 wPos = mul(float4(v.position, 1.0), _WorldMat);
	// wPos = float4(v.position, 1.0);
	float4 cPos = mul(wPos, _ViewProjMat);

	float3 wNormal = normalize(mul((float3x3)_InvWorldMat, v.normal));
	float3 wTangent = normalize(mul(v.tangent.xyz, (float3x3)_WorldMat));
	float3 wBitangent = cross(wNormal, wTangent) * v.tangent.w;

	o.pos = cPos;
	o.worldPos = wPos.xyz;
#ifdef GL_UV_STARTS_AT_BOTTOMLEFT	// (貌似不用反转y轴)
	// o.uv0 = float2(v.uv0.x, 1.0 - v.uv0.y);
	// o.uv1 = float2(v.uv1.x, 1.0 - v.uv1.y);
	o.uv0 = v.uv0;
	o.uv1 = v.uv1;
#else
	o.uv0 = v.uv0;
	o.uv1 = v.uv1;
#endif
	o.normal = wNormal;
	o.tangent = wTangent;
	o.bitangent = wBitangent;
	o.color = v.color;

	return o;
}
