#include "AiCommonRS.hlsli"

cbuffer CBPerMaterial	: register(b3)
{
	float4 _BaseColorFactor;
	float4 _MetallicRoughness;	// r-occlusion, g-metallic, b-roughness // rgb-specularColor, a-glossiness
	float3 _Emissive;
	float _EmissiveFactor;

	float _AlphaCutout = 0.5f;	// alpha threshold, only used in case the alpha mode is mask
	float _NormalScale;
	float _OcclusionStrength = 1.0f;
	float _F0 = 0.04f;
	float _SpecularTransmission;
	uint _Flags = 0;
};

struct VSOutput
{
	float4 pos 	: SV_POSITION;
	float2 uv0	: TEXCOORD0;
};

Texture2D<float4> _TexBaseColor	: register(t0);

SamplerState s_LinearSampler	: register(s0);

[RootSignature(Common_RootSig)]
float4 main(VSOutput i) : SV_TARGET
{
#ifdef _CLIP_ON
	float4 color = _TexBaseColor.Sample(s_LinearSampler, i.uv0);
	if (color.a < _AlphaCutout)
		discard;
#endif

	return float4(0.0f, 0.0f, 0.0f, 0.0f);
}
