#include "WaterWaveRS.hlsli"

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

Texture2D<float4> _DisplacementMap	: register(t0);

SamplerState s_LinearWrapSampler	: register(s0);

struct VSInput
{
	float3 position : POSITION;
	float2 uv		: TEXCOORD0;
};

struct VSOutput
{
	float4 pos 	: SV_POSITION;
	float2 uv	: TEXCOORD0;
};

[RootSignature(RootSig_WaterWave)]
VSOutput main(VSInput v)
{
	VSOutput o;

	float4 wPos = mul(float4(v.position, 1.0f), _WorldMat);
	float3 displacement = _DisplacementMap.SampleLevel(s_LinearWrapSampler, v.uv, 0.0f).xyz;
	wPos.xyz += displacement;
	float4 cPos = mul(wPos, _ViewProjMat);
	
	o.pos = cPos;
	o.uv = v.uv;

	return o;
}
