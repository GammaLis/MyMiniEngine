#include "CommonIndirectRS.hlsli"
#define SHADING_MODEL_METALLIC_ROUGHNESS
#include "BasicLighting.hlsli"
#include "PBRUtility.hlsli"
#include "../Scenes/MaterialDefines.h"

#define BASIC_LIGHTING

cbuffer CBConstants	: register(b0)
{
	uint _LightNum;
	float3 _Constants;
};
cbuffer CBPerCamera	: register(b1)
{
	matrix _ViewProjMat;
	float3 _CamPos;
};
cbuffer CBLights	: register(b2)
{
	float3 _SunDirection;
	float3 _SunColor;
	float3 _AmbientColor; 
};

StructuredBuffer<GlobalMatrix> _MatrixBuffer            : register(t0, space1);
StructuredBuffer<MaterialData> _MaterialBuffer          : register(t1, space1);
StructuredBuffer<MeshDesc> _MeshBuffer                  : register(t2, space1);
StructuredBuffer<MeshInstanceData> _MeshInstanceBuffer  : register(t3, space1);

Texture2D _MaterialTextures[]	: register(t4, space1);

SamplerState s_LinearRSampler	: register(s0);
SamplerState s_PointCSampler: register(s1);

struct VSOutput
{
	float4 pos 	: SV_POSITION;
	float2 uv0 	: TEXCOORD0;
	float3 worldPos	: TEXCOORD1;
	float3 normal 	: NORMAL;
	float3 tangent 	: TANGENT;
	float3 bitangent: BITANGENT;

	uint drawId	: DRAWID;
};

[RootSignature(CommonIndirect_RootSig)]
float4 main(VSOutput i, bool bFront : SV_IsFrontFace) : SV_TARGET
{
	MeshInstanceData meshInstance = _MeshInstanceBuffer[i.drawId];
	uint materialId = meshInstance.materialID;
	MaterialData material = _MaterialBuffer[materialId];
	float4 _BaseColorFactor = material.baseColor;
	float4 _MetallicRoughness = material.metalRough;
	float3 _Emissive = material.emissive;
	float _EmissiveFactor = material.emissiveFactor;
	float _AlphaCutout = material.alphaCutout;
	float _NormalScale = material.normalScale;
	float _OcclusionStrength = material.occlusionStrength;
	float _F0 = material.f0;
	float _SpecularTransmission = material.specularTransmission;
	uint _Flags = material.flags;

	uint texbase = MATERIAL_TEXTURE_NUM * materialId;
	Texture2D _TexBaseColor = _MaterialTextures[texbase + 0];
	Texture2D _TexMetallicRoughness = _MaterialTextures[texbase + 1];
	Texture2D _TexNormal = _MaterialTextures[texbase + 2];
	Texture2D _TexEmissive = _MaterialTextures[texbase + 3];
	Texture2D _TexOcclusion = _MaterialTextures[texbase + 4];

	uint shadingModel = EXTRACT_SHADING_MODEL(_Flags);
	uint diffuseType = EXTRACT_DIFFUSE_TYPE(_Flags);
	uint specularType = EXTRACT_SPECULAR_TYPE(_Flags);
	uint emissiveType = EXTRACT_EMISSIVE_TYPE(_Flags);
	uint normalMapType = EXTRACT_NORMAL_MAP(_Flags);
	uint occlusionType = EXTRACT_OCCLUSION_MAP(_Flags);
	uint alphaMode = EXTRACT_ALPHA_MODE(_Flags);
	uint bDoubleSided = EXTRACT_DOUBLE_SIDED(_Flags);

	// base color
	float4 baseColor = _BaseColorFactor;
	if (diffuseType == ChannelTypeTexture)
		baseColor = _TexBaseColor.Sample(s_LinearRSampler, i.uv0);

	// alpha test
	if (alphaMode == AlphaModeMask)
	{
		if (baseColor.a < _AlphaCutout)
			discard;
	}

	float gloss = 128.0;
	// normal
	float3 normal = i.normal;
	if (normalMapType == NormalMapRGB)
	{
		float3 normalMap = _TexNormal.Sample(s_LinearRSampler, i.uv0).rgb;
		normalMap = normalize((2.0 * normalMap - 1.0) * float3(_NormalScale, _NormalScale, 1.0));

		AntiAliasSpecular(normal, gloss);

		normal = i.tangent * normalMap.x + i.bitangent * normalMap.y + i.normal * normalMap.z;
	}

	// double sided
	if (bDoubleSided)
	{
		normal *= bFront ? 1.0 : -1.0;
	}

	// occlusion
	float occlusion = 1.0;
	if (occlusionType > 0)
	{
		occlusion = _TexOcclusion.Sample(s_LinearRSampler, i.uv0).r;
	}
	occlusion *= _OcclusionStrength;

	// emissive
	float3 emissive = _Emissive;
	if (emissiveType == ChannelTypeTexture)
	{
		emissive = _TexEmissive.Sample(s_LinearRSampler, i.uv0).rgb;
	}
	emissive *= _EmissiveFactor;

	// shading model
	float4 color = 0;
	float3 specularAlbedo = float3(0.56, 0.56, 0.56);
	float specularMask = 1.0f;
	if (shadingModel == ShadingModel_MetallicRoughness)
	{
		float metallic = _MetallicRoughness.y, perceptualRoughness = _MetallicRoughness.z;
		if (specularType == ChannelTypeTexture)
		{
			float2 metalRough = _TexMetallicRoughness.Sample(s_LinearRSampler, i.uv0).rg;
			metallic = metalRough.x, perceptualRoughness = metalRough.y;
		}

	}
	else if (shadingModel == ShadingModel_SpecularGlossiness)
	{
		specularMask = _TexMetallicRoughness.Sample(s_LinearRSampler, i.uv0).g;
	}
	else 	// unlit
	{

	}

	TMaterial mat;

	mat.baseColor = baseColor;

	// #if defined(SHADING_MODEL_METALLIC_ROUGHNESS)
	// 	mat.metallic = metallic;	// _Metallic
	// 	mat.perceptualRoughness = perceptualRoughness;	// _Roughness
	// 	mat.f0 = _F0;
	// #elif defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
	// 	mat.specularColor = _SpecularColor;
	// 	mat.glossiness = _Glossiness;
	// #endif

	float3 worldPos = i.worldPos;

	// view direction
	float3 viewDir = normalize(_CamPos - worldPos);

	float3 lighting = 0;
	// direct lighting
	// [unroll]	// _LightNum不是常量，无法展开
	// for (uint i = 0; i < _LightNum; ++i)
	// {
	// 	TLight curLight = _Lights[i];
	// 	lighting += DirectLighting(curLight, mat, worldPos, normal, viewDir);
	// }

	// specular
	// ...
	
#ifdef BASIC_LIGHTING
	// ambient
	lighting += ApplyAmbientLight(baseColor.rgb, 1.0, _AmbientColor);
	
	// 1 directional light
	lighting += ApplyDirectionalLight(baseColor.rgb, specularAlbedo, specularMask, gloss, normal, viewDir,
		_SunDirection, _SunColor, float3(0.0, 0.0, 0.0));
	
	// point lights + spot lights
	

#endif

	// indirect lighting 
	float3 indirectLighting = 0;

	// debug
	// lighting = baseColor.rgb;
	
	//
	color.rgb = emissive.rgb + lighting * occlusion + indirectLighting;
	// baseColor.rgb *= baseColor.a;	// premultiplied color

	// ** debug indirectLighting **
	// color.rgb = indirectLighting;
	
	color.a = baseColor.a;

	return color;
}
