#include "PresentRS.hlsli"

Texture2D _MainTex	: register(t0);

SamplerState s_LinearClampSampler	: register(s0);
SamplerState s_PointClampSampler	: register(s1);

float3 SimpleTonemap(float3 Luminance)
{
	return pow(Luminance, 1.0 / 2.2);
}

// @return color in SRGB
float3 SimpleToneMap2(float3 HDRColor)
{
	// from http://filmicgames.com/archives/75
	float3 x = max(0, HDRColor - 0.004f);
	return (x * (6.2f * x + 0.5f)) / (x * (6.2f * x + 1.7f) + 0.06f);
	
	// linear/no tonemapper 
	// return HDRColor;
}

[RootSignature(Present_RootSig)]
float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
	float4 color = 1.0f;

	// color = _MainTex[(uint2)pos.xy];	// 需要 图片大小 - 屏幕大小 一致

	color = _MainTex.Sample(s_LinearClampSampler, uv);
	
	color.rgb /= color.rgb + 1.0;

	return color;
}
