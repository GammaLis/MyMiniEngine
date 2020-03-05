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
};
cbuffer CBPerMaterial	: register(b3)
{
	float4 _X;
	float4 _Y;
};

Texture2D<float4> _BaseColor: register(t0);
Texture2D<float4> _NormalMap: register(t1);

SamplerState s_LinearRSamper: register(s0);
SamplerState s_PointCSampler: register(s1);

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

float4 main(VSOutput i) : SV_TARGET
{
	return _X.xyzw;
}
