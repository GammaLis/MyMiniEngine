#include "ShaderUtility.hlsli"
#include "PresentRS.hlsli"

cbuffer Constants	: register(b0)
{
	float _ScaleFactor;
}

Texture2D<float3> _ColorTex	: register(t0);
SamplerState s_PointSampler	: register(s1);

[RootSignature(Present_RootSig)]
float3 main(float4 pos : SV_Position, float2 uv : Texcoord0) : SV_TARGET
{
	float2 scaledUV = _ScaleFactor * (uv - 0.5) + 0.5;
	return _ColorTex.SampleLevel(s_PointSampler, scaledUV, 0);
}