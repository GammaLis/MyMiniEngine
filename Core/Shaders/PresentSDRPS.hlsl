#include "ShaderUtility.hlsli"
#include "PresentRS.hlsli"

Texture2D<float3> _ColorTex	: register(t0);

[RootSignature(Present_RootSig)]
float3 main(float4 pos : SV_Position) : SV_Target0
{
	float3 linearRGB = _ColorTex[(int2)pos.xy];	// RemoveDisplayProfile(_ColorTex[(int2)pos.xy], LDR_COLOR_FORMAT);
	return ApplyDisplayProfile(linearRGB, DISPLAY_PLANE_FORMAT);
}
