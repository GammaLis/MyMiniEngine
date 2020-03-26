#include "SkyboxRS.hlsli"

cbuffer cbPerCamera	: register(b0)
{
	matrix _ViewMat;
};

cbuffer cbProjection: register(b1)
{
	matrix _ProjMat;
};

struct VSInput
{
	float3 position	: POSITION;
};

struct VSOutput
{
	float4 pos	: SV_POSITION;
	float3 wPos	: TEXCOORD0;
};

[RootSignature(Skybox_RootSig)]
VSOutput main(VSInput v)
{
	VSOutput o;
	
	float3 worldPos = v.position;
	float3 vPos = mul(worldPos, (float3x3)_ViewMat);
	float4 cPos = mul(float4(vPos, 1.0), _ProjMat);
	cPos.z = 0;
	
	o.pos = cPos;	// z -> 0.0, for reversed-Z, z->w, normal Z
	o.wPos = worldPos;

	return o;
}
