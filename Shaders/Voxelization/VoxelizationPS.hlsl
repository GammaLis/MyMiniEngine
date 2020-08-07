#include "CommonIndirectRS.hlsli"
#include "BasicLighting.hlsli"
#define SHADING_MODEL_METALLIC_ROUGHNESS
#include "PBRUtility.hlsli"
#include "VoxelUtility.hlsli"
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
cbuffer CBMiscs		: register(b3)
{
	uint _VoxelGridRes;			// 256
	float3 _VoxelGridCenterPos;	// grid center potisiton
	float3 _RcpVoxelSize;		// 1.0 / voxelSize, voxelSize = gridExtent / gridRes
};

StructuredBuffer<GlobalMatrix> _MatrixBuffer            : register(t0, space1);
StructuredBuffer<MaterialData> _MaterialBuffer          : register(t1, space1);
StructuredBuffer<MeshDesc> _MeshBuffer                  : register(t2, space1);
StructuredBuffer<MeshInstanceData> _MeshInstanceBuffer  : register(t3, space1);

Texture2D _MaterialTextures[]	: register(t4, space1);

struct VoxelData
{
	uint colorOcclusionMask;		// encoded color & occlusion, voxel only contains geometry info if occlusion > 0
	uint4 normalMasks;	// encoded normals
};
RWStructuredBuffer<VoxelData> VoxelBuffer	: register(u0);
// using RWStructuredBuffer to be able to support atomic operations easily
// the StructuredBuffer is a linear array of VoxelData of size GridDimensions X*Y*Z

SamplerState s_LinearRSampler	: register(s0);
SamplerState s_PointCSampler	: register(s1);

struct GSOutput
{
	float4 pos 	: SV_POSITION;
	float2 uv0	: TEXCOORD0;
	float3 worldPos	: TEXCOORD1;
	float3 normal 	: NORMAL;
	float3 tangent 	: TANGENT;
	float3 bitangent: BITANGENT;

	uint drawId : DRAWID;
};

/**
 * 	instead of outputting the rasterized information into the bound render-target, it will be written
 * into a 3D structured buffer. In this way dynamically a voxel-grid can be generated.
 */
[RootSignature(CommonIndirect_RootSig)]
void main(GSOutput i, bool bFront : SV_IsFrontFace)
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

	// encode color in linear space into unsigned integer
	float3 linearColor = baseColor.rgb < 0.04045f ? baseColor.rgb / 12.92f :
		pow( (baseColor.rgb + 0.055f) / 1.055f, 2.4f );
	uint colorOcclusionMask = EncodeColor(linearColor);

	/**
	 * 	since voxels are a simplified representation of the actual scene, high frequency information
	 * gets lost. In order to amplify color bleeding in the final global illumination output, colors
	 * with high difference in their color channels (called here contrast) are preferred. By writing
	 * the contrast value (0-255) in the highest 8 bit of the color-mask, automatically colors with
	 * high contrast will dominate, since we write the results with an InterlockedMax into the voxel-grids.
	 * The contrast value is calculated in sRGB space.
	 */	
	float contrast = length(baseColor.rrg - baseColor.gbb) / (sqrt(2.0f) + baseColor.r + baseColor.g + baseColor.b);
	uint iContrast = uint(contrast * 127.0);
	colorOcclusionMask |= (iContrast << 24u);

	// encode occlusion into highest bit
	colorOcclusionMask |= (1 << 31u);

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
		normal = normalize(normal);
	}

	// double sided
	if (bDoubleSided)
	{
		normal *= bFront ? 1.0 : -1.0;
	}
	uint normalMask = EncodeNormal(normal);

	/**
	 * 	normal values also have to be carefully written into the voxels, since for example thin geometry
	 * can have opposite normals in one single voxel. Therefore it is determined, to which face of a 
	 * tetrahedron the current normal is closest to. By writing the corresponding dotProduct value in 
	 * the highest 5 bit of the normal mask, automatically the closest normal to the determined tetrahedron
	 * face will be selected, since we write the results with an InterlockedMax into the voxel grids.
	 * 	According to the retrieved tetrahedron face the normals are written into the corresponding normal
	 * channel of the voxel. Later on, when the voxels are illuminated, the closest normal to the light vector
	 * is chosen, so that the best illumination can be obtained.
	 */
	float dotProduct;
	uint normalIndex = GetNormalIndex(normal, dotProduct);
	uint iDotProduct = uint(saturate(dotProduct) * 31.0f);
	normalMask |= (iDotProduct << 27u);

	// get offset into the voxel grid
	float3 gridOffset = (i.worldPos - _VoxelGridCenterPos) * _RcpVoxelSize;
	gridOffset = floor(gridOffset);

	// get position in the voxel grid
	int3 voxelPos = _VoxelGridRes / 2 + int3(gridOffset);

	if ((voxelPos.x >= 0 && voxelPos.x < (int)_VoxelGridRes) && 
		(voxelPos.y >= 0 && voxelPos.y < (int)_VoxelGridRes) &&
		(voxelPos.z >= 0 && voxelPos.z < (int)_VoxelGridRes))
	{
		// get index into the voxel grid
		uint voxelIndex = GetGridIndex(voxelPos, _VoxelGridRes);

		// output color/occlusion
		InterlockedMax(VoxelBuffer[voxelIndex].colorOcclusionMask, colorOcclusionMask);

		// output normal according to normal index
		InterlockedMax(VoxelBuffer[voxelIndex].normalMasks[normalIndex], normalMask);
	}

	// ...Voxel End

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

	float3 worldPos = i.worldPos;

	// view direction
	float3 viewDir = normalize(worldPos - _CamPos);		// world-space vector from eye to point

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
	lighting += 0.25f * ApplyAmbientLight(baseColor.rgb, 1.0, _AmbientColor);
	
	// 1 directional light
	lighting += ApplyDirectionalLight(baseColor.rgb, specularAlbedo, specularMask, gloss, normal, viewDir,
		_SunDirection, _SunColor, float3(0.0, 0.0, 0.0));
	
	// point lights + spot lights
	

#endif

	// indirect lighting 
	float3 indirectLighting = 0;

	// debug
	// lighting = baseColor.rgb;
	
	// occlusiont有问题，暂时设置为1.0
	occlusion = 1.0f;
	color.rgb = emissive.rgb + lighting * occlusion + indirectLighting;
	// baseColor.rgb *= baseColor.a;	// premultiplied color

	// ** debug indirectLighting **
	// color.rgb = indirectLighting;
	
	color.a = baseColor.a;
}
