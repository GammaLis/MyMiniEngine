#include "TextRS.hlsli"

cbuffer cbFontParams : register(b0)
{
	float4 _Color;
	float2 _ShadowOffset;
	float _ShadowHardness;
	float _ShadowOpacity;
	float _HeightRange;		// the range of the signed distance field
};

Texture2D<float> _SignedDistanceFieldTex : register(t0);
SamplerState s_LinearSampler : register(s0);

struct VSOutput
{
	float4 pos	: SV_POSITION;
	float2 uv	: TEXCOORD0;
};

float GetAlpha(float2 uv)
{
	return saturate(_SignedDistanceFieldTex.Sample(s_LinearSampler, uv) * _HeightRange + 0.5);
}

[RootSignature(Text_RootSig)]
float4 main(VSOutput i) : SV_TARGET
{
	return float4(_Color.rgb, 1.0) * GetAlpha(i.uv) * _Color.a;
}