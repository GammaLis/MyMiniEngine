#include "PresentRS.hlsli"

Texture2D _MainTex	: register(t0);

SamplerState s_LinearClampSampler	: register(s0);
SamplerState s_PointClampSampler	: register(s1);

[RootSignature(Present_RootSig)]
float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
	float4 color = 1.0f;

	// color = _MainTex[(uint2)pos.xy];	// 需要 图片大小 - 屏幕大小 一致

	color = _MainTex.Sample(s_LinearClampSampler, uv);

	return color;
}
