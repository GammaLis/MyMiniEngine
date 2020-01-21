#include "ShaderUtility.hlsli"
#include "PresentRS.hlsli"

cbuffer Constants : register(b0)
{
	float2 _UVOffset0;
	float2 _UVOffset1;
	float _WA, _WB;
}

Texture2D<float3> _ColorTex : register(t0);

SamplerState s_BilinearClamp: register(s0);

float3 GetColor(float2 uv)
{
    float3 Color = _ColorTex.SampleLevel(s_BilinearClamp, uv, 0);
#ifdef GAMMA_SPACE
    return ApplyDisplayProfile(Color, DISPLAY_PLANE_FORMAT);
#else
    return Color;
#endif
}

[RootSignature(Present_RootSig)]
float3 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
	float3 color = _WB * GetColor(uv) - _WA * (
		GetColor(uv + _UVOffset0) + GetColor(uv - _UVOffset0) +
		GetColor(uv + _UVOffset1) + GetColor(uv - _UVOffset1));

#ifdef GAMMA_SPACE
	return color;
#else
    return ApplyDisplayProfile(color, DISPLAY_PLANE_FORMAT);
#endif
}