#include "CommonIndirectRS.hlsli"
#include "../Scenes/MaterialDefines.h"

cbuffer CBConstants	: register(b0)
{
	uint _LightNum;
	float3 _Constants;
};
#if !USE_VIEW_UNIFORMS
cbuffer CBPerCamera	: register(b1)
{
	matrix _ViewProjMat;
	float3 _CamPos;
};
#else
ConstantBuffer<ViewUniformParameters> _View : register(b1);
#endif
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

	uint drawId	: DRAWID;
};

[RootSignature(CommonIndirect_RootSig)]
void main(VSOutput i, bool bFront : SV_IsFrontFace)
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

}
