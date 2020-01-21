#include "ShaderUtility.hlsli"
#include "PresentRS.hlsli"

Texture2D<float3> _ColorTex     : register(t0);

SamplerState s_BilinearFilter   : register(s0);

[RootSignature(Present_RootSig)]
float3 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET0
{
    float3 linearRGB = RemoveDisplayProfile(_ColorTex.SampleLevel(s_BilinearFilter, uv, 0), LDR_COLOR_FORMAT);
    return ApplyDisplayProfile(linearRGB, DISPLAY_PLANE_FORMAT);
}
