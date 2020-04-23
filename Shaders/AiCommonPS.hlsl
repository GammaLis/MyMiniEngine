#include "AiCommonRS.hlsli"
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
	float4 _MetallicRoughness;	// r-occlusion, g-metallic, b-roughness // rgb-specularColor, a-glossiness
	float3 _Emissive;
	float _EmissiveFactor;

	float _AlphaCutout;		// alpha threshold, only used in case the alpha mode is mask
	float _NormalScale;
	float _OcclusionStrength = 1.0f;
	float _F0 = 0.04f;
	float _SpecularTransmission;
	uint _Flags = 0;
};
cbuffer CBLights	: register(b4)
{
	float3 _SunDirection;
	float3 _SunColor;
	float3 _AmbientColor; 
};

Texture2D<float4> _TexBaseColor			: register(t0);
Texture2D<float4> _TexMetallicRoughness	: register(t1);
Texture2D<float3> _TexNormal 			: register(t2);
Texture2D<float > _TexOcclusion			: register(t3);
Texture2D<float3> _TexEmissive			: register(t4);

SamplerState s_LinearRSampler	: register(s0);
SamplerState s_PointCSampler	: register(s1);

struct VSOutput
{
	float4 pos 	: SV_POSITION;
	float2 uv0 	: TEXCOORD0;
	float3 worldPos	: TEXCOORD1;
	float3 normal 	: NORMAL;
	float3 tangent 	: TANGENT;
	float3 bitangent: BITANGENT;
};

[RootSignature(Common_RootSig)]
float4 main(VSOutput i, bool bFront : SV_IsFrontFace) : SV_TARGET
{
	uint shadingModel 	= EXTRACT_SHADING_MODEL(_Flags);
	uint diffuseType  	= EXTRACT_DIFFUSE_TYPE(_Flags);
	uint specularType 	= EXTRACT_SPECULAR_TYPE(_Flags);
	uint emissiveType 	= EXTRACT_EMISSIVE_TYPE(_Flags);
	uint normalMapType	= EXTRACT_NORMAL_MAP(_Flags);
	uint occlusionType	= EXTRACT_OCCLUSION_MAP(_Flags);
	uint alphaMode 		= EXTRACT_ALPHA_MODE(_Flags);
	uint bDoubleSided 	= EXTRACT_DOUBLE_SIDED(_Flags);

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
		normalMap = normalize( (2.0 * normalMap - 1.0) * float3(_NormalScale, _NormalScale, 1.0) );

		AntiAliasSpecular(normal, gloss);

		normal = i.tangent * normalMap.x + i.bitangent * normalMap.y + i.normal * normalMap.z;
	}

	// double sided
	if (bDoubleSided)
	{
		normal *= bFront ? 1.0 : -1.0;
	}

	// occlusion
	float occlusion = _OcclusionStrength;
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
	// lighting += ApplyDirectionalLight(baseColor.rgb, specularAlbedo, specularMask, gloss, normal, viewDir,
	// 	_SunDirection, _SunColor, float3(0.0, 0.0, 0.0));
	
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
