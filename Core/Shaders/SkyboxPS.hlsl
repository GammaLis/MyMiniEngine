#include "SkyboxRS.hlsli"

struct VSOutput
{
	float4 pos	: SV_POSITION;
	float3 wPos	: TEXCOORD0;
};

TextureCube<float4> _Skybox	: register(t0);

SamplerState s_LinearSampler: register(s0);

[RootSignature(Skybox_RootSig)]
float4 main(VSOutput i) : SV_TARGET
{
	float3 wPos = i.wPos;
	float4 color = _Skybox.Sample(s_LinearSampler, wPos);
	return color;
}
