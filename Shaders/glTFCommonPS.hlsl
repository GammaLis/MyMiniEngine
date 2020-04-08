#include "glTFCommonRS.hlsli"
#define SHADING_MODEL_METALLIC_ROUGHNESS
#include "PBRUtility.hlsli"

cbuffer CBConstants	: register(b0)
{
	uint _LightNum;
	float3 _Constants;
};
cbuffer CBPerObject	: register(b1)
{
	matrix _WorldMat;
	matrix _InvWorldMat;
};
cbuffer CBPerCamera	: register(b2)
{
	matrix _ViewProjMat;
	float3 _CamPos;
};
cbuffer CBPerMaterial	: register(b3)
{
	float4 _BaseColorFactor;
	float3 _EmissiveFactor;
	float _AlphaCutout;
	uint4 _Texcoords[2];	// 0-baseColor, 1-metallicRoughness, 2-normal, 3-occlusion, 4-emissive,...

#if defined(SHADING_MODEL_METALLIC_ROUGHNESS)
	float _Metallic;
	float _Roughness;
	float _F0;			// default to 0.04
	float _Padding;
#elif defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
	float3 _SpecularColor;
	float _Glossiness;
#endif
	float _NormalScale;
	float _OcclusionStrength;
};

Texture2D<float4> _TexBaseColor			: register(t0);
#if defined(SHADING_MODEL_METALLIC_ROUGHNESS)
Texture2D<float4> _TexMetallicRoughness	: register(t1);
#elif defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
Texture2D<float4> _TexSpecularGlossiness: register(t1);
#endif
Texture2D<float3> _TexNormal 			: register(t2);
Texture2D<float> _TexOcclusion			: register(t3);
Texture2D<float4> _TexEmissive			: register(t4);

StructuredBuffer<TLight> _Lights		: register(t1, space1);
StructuredBuffer<SH9Color> _SHCoefs		: register(t2, space1);

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

	TMaterial mat;

	// base color
	float4 baseColor = _TexBaseColor.Sample(s_LinearRSamper, uvs[_Texcoords[0].x]);
	baseColor *= _BaseColorFactor;
	mat.baseColor = baseColor;

	if (step(baseColor.a, _AlphaCutout))
		discard;

	// emissive
	float4 emissive = _TexEmissive.Sample(s_LinearRSamper, uvs[_Texcoords[1].x]);
	mat.emissive = emissive;	// float4(_EmissiveFactor, 0.0);

	// occlusion
	float occlusion = _TexOcclusion.Sample(s_LinearRSamper, uvs[_Texcoords[0].w]);
	occlusion *= _OcclusionStrength;
	mat.occlusion = occlusion;	// _OcclusionStrength

#if defined(SHADING_MODEL_METALLIC_ROUGHNESS)
	float4 metallicRoughness = _TexMetallicRoughness.Sample(s_LinearRSamper, uvs[_Texcoords[0].y]);
	float metallic = metallicRoughness.r;
	float perceptualRoughness = metallicRoughness.g;
	mat.metallic = metallic;	// _Metallic
	mat.perceptualRoughness = perceptualRoughness;	// _Roughness
	mat.f0 = _F0;
#elif defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
	mat.specularColor = _SpecularColor;
	mat.glossiness = _Glossiness;
#endif

	float3 worldPos = i.worldPos;
	// normal
	float3 wNormal = normalize(i.normal);
	float3 wTangent = normalize(i.tangent);
	float3 wBitangent = normalize(i.bitangent);
	float3 normal = _TexNormal.Sample(s_LinearRSamper, uvs[_Texcoords[0].z]);
	// debug normal
	// baseColor.rgb = normal;
	// debug end
	normal = normalize((2.0 * normal - 1) * float3(_NormalScale, _NormalScale, 1.0));
	normal = wTangent * normal.x + wBitangent * normal.y + wNormal * normal.z;

	// view direction
	float3 viewDir = normalize(_CamPos - worldPos);

	float4 color = baseColor;
	float3 lighting = 0;
	// direct lighting
	// [unroll]	// _LightNum不是常量，无法展开
	for (uint i = 0; i < _LightNum; ++i)
	{
		TLight curLight = _Lights[i];
		lighting += DirectLighting(curLight, mat, worldPos, normal, viewDir);
	}

	// indirect lighting
	float3 indirectLighting = 0;
	// 
	// irradiance
	float3 diffuseColor = baseColor.rgb * (1 - metallic);
	float3 irradiance = 0;
	irradiance = ApproximateDiffuseSH(_SHCoefs[0], normal, diffuseColor);

	indirectLighting += irradiance;

	// specular
	// ...
	
	//
	color.rgb = emissive.rgb + lighting * occlusion + indirectLighting;
	// baseColor.rgb *= baseColor.a;	// premultiplied color
	
	// ** debug indirectLighting **
	// color.rgb = indirectLighting;

	return color;
}
