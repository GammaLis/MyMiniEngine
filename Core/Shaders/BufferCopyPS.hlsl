#include "PresentRS.hlsli"

cbuffer Constants : register(b0)
{
	float2 _RcpDestDim;
}

Texture2D _ColorTex : register(t0);
SamplerState s_BilinearSampler : register(s0);

[RootSignature(Present_RootSig)]
float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
	float4 color = 1.0;

	// float2 uv = saturate(_RcpDestDim * pos.xy);
	// color = _ColorTex.SampleLevel(s_BilinearSampler, uv, 0);

	// -mf (显示字体纹理)
	// color = _ColorTex.SampleLevel(s_BilinearSampler, uv, 0);

	color = _ColorTex[(uint2)pos.xy];

	return color;
}
