#include "CommonRS.hlsli"

cbuffer CBConstants	: register(b0)
{
	float4 _Constants;
};
cbuffer CBPerObject	: register(b1)
{
	matrix _WorldMat;
	matrix _InvWorldMat;
};
cbuffer CBPerCamera	: register(b2)
{
	matrix _ViewProjMat;
};
cbuffer CBPerMaterial	: register(b3)
{
	float4 _BaseColorFactor;
	float4 _EmissiveFactor;		// xyz - emissive factor, w - alpha cutoff
	uint4 _Texcoords[2];	// 0-baseColor, 1-metallicRoughness, 2-normal, 3-occlusion, 4-emissive,...
	float4 _Misc;	// x - metallic, y - roughness, z - normalScale, w - occlusionStr
};

Texture2D<float4> _TexBaseColor			: register(t0);
Texture2D<float4> _TexMetallicRoughness	: register(t1);
Texture2D<float3> _TexNormal 			: register(t2);
Texture2D<float4> _TexOcclusion			: register(t3);
Texture2D<float4> _TexEmissive			: register(t4);

SamplerState s_LinearRSamper: register(s0);
SamplerState s_PointCSampler: register(s1);

struct VSOutput
{
	float4 pos 	: SV_POSITION;
	float2 uv0 	: TEXCOORD0;
	float2 uv1	: TEXCOORD1;
	float3 worldPos	: TEXCOORD2;
	float3 normal 	: NORMAL;
	float3 tangent 	: TANGENT;
	float3 bitangent: TEXCOORD3;
	float3 color 	: COLOR0;
};

float4 main(VSOutput i) : SV_TARGET
{
	float2 uvs[] = {i.uv0, i.uv1};

	float4 baseColor = _TexBaseColor.Sample(s_LinearRSamper, uvs[_Texcoords[0].x]);
	baseColor *= _BaseColorFactor;

	// float4 metallicRoughness = _TexMetallicRoughness.Sample(s_LinearRSamper, uvs[_Texcoords[0].y]);
	// baseColor = metallicRoughness;

	// float3 tNormal = _TexNormal.Sample(s_LinearRSamper, uvs[_Texcoords[0].z]);
	// baseColor.rgb = tNormal;

	return baseColor;
}
