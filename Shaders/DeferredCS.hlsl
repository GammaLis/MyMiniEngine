#include "Quaternion.hlsli"
#include "MaterialDefines.hlsli"
#include "BasicLighting.hlsli"
#include "../Scenes/MaterialDefines.h"

#define DeferredCS_RootSig \
	"RootFlags(0)," \
	"CBV(b0)," \
	"CBV(b1)," \
	"CBV(b2)," \
	"DescriptorTable(SRV(t1, numDescriptors = 5))," \
	"DescriptorTable(SRV(t10, numDescriptors = 1))," \
	"SRV(t1, space = 1)," \
	"DescriptorTable(SRV(t4, space = 1, numDescriptors = unbounded))," \
	"SRV(t0, space = 2)," \
	"DescriptorTable(SRV(t1, space = 2, numDescriptors = 10)),"	\
	"DescriptorTable(UAV(u0, numDescriptors = 1))," \
	"StaticSampler(s0, " \
		"addressU = TEXTURE_ADDRESS_WRAP," \
		"addressV = TEXTURE_ADDRESS_WRAP," \
		"addressW = TEXTURE_ADDRESS_WRAP," \
		"filter = FILTER_MIN_MAG_MIP_LINEAR)," \
	"StaticSampler(s1, " \
		"addressU = TEXTURE_ADDRESS_CLAMP," \
		"addressV = TEXTURE_ADDRESS_CLAMP," \
		"addressW = TEXTURE_ADDRESS_CLAMP," \
		"filter = FILTER_MIN_MAG_MIP_POINT)," \
	"StaticSampler(s2, " \
		"addressU = TEXTURE_ADDRESS_CLAMP," \
		"addressV = TEXTURE_ADDRESS_CLAMP," \
		"addressW = TEXTURE_ADDRESS_CLAMP," \
		"comparisonFunc = COMPARISON_LESS_EQUAL," \
		"filter = FILTER_MIN_MAG_LINEAR_MIP_POINT)"

// outdated warning about for-loop variable scope
#pragma warning (disable: 3078)

#define MATERIAL_TEXTURE_NUM 5

#define DeferredTileSize 16
#define ThreadGroupSize (DeferredTileSize * DeferredTileSize)

// shadow samples
#define SINGLE_SAMPLE

static const float DeferredUVScale = 2.0f;
static const uint NumCascades = 4;

static const float4 DebugColors[] = 
{
	float4(1.0f, 0.0f, 0.0f, 1.0f),
	float4(0.0f, 1.0f, 0.0f, 1.0f),
	float4(0.0f, 0.0f, 1.0f, 1.0f),
	float4(1.0f, 1.0f, 0.0f, 1.0f),
};

cbuffer CSConstants	: register(b0)
{
	uint _ViewportWidth, _ViewportHeight;
	float _NearClip, _FarClip;
	float3 _CamPos;
	float3 _CascadeSplits;
	matrix _ViewProjMat;
	matrix _InvViewProjMat;
	matrix _ViewMat;
	matrix _ProjMat;
};

cbuffer CommonLights	: register(b1)
{
	float3 _SunDirection;
	float3 _SunColor;
	float3 _AmbientColor;
};

cbuffer CascadedShadowConstants	: register(b2)
{
	matrix _LightViewProjMat[NumCascades];
};

Texture2D<float> _DepthTexture 	: register(t1);
Texture2D _GBuffer[3]			: register(t2);
Texture2D<uint> _TexMaterialID	: register(t5);

Texture2DArray _CascadedShadowMap	: register(t10);

// Texture2D _GBuffer[4]	: register(t0);
// Texture2D<uint> _TexMaterialID	: register(t4);
/**
 * _TexArr is unbound,
 * 	Texture2D _TexArr[]	: register(t0);
 * 	Texture2D _Tex 	: register(t4);
 */

// materials
StructuredBuffer<MaterialData> _MaterialBuffer	: register(t1, space1);
Texture2D _MaterialTextures[]	: register(t4, space1);	// unbounded t(4)~t(+Inf)

// decals
struct DecalData
{
	matrix _WorldMat;
	matrix _InvWorldMat;
	uint albedoIndex;
	uint normalIndex;
};
StructuredBuffer<DecalData> _DecalBuffer: register(t0, space2);
Texture2D _DecalTextures[4]	: register(t1, space2);

RWTexture2D<float4> Output	: register(u0);

SamplerState s_LinearRSampler	: register(s0);
SamplerState s_PointCSampler: register(s1);
SamplerComparisonState s_ShadowSampler	: register(s2);

// MSAA subsample locations
#if _NumMSAASamples == 4
static const float2 SubSampleOffsets[4] = 
{
	float2(-0.125f, -0.375f),
	float2( 0.375f, -0.125f),
	float2(-0.375f,  0.125f),
	float2( 0.125f,  0.375f)
};
#elif _NumMSAASamples == 2
static const float2 SubSampleOffsets[2] = 
{
	float2( 0.25f,  0.25f),
	float2(-0.25f, -0.25f)
};
#else 
static const float2 SubSampleOffsets[1] = 
{
    float2(0.0f, 0.0f)
};
#endif

#if _MSAA
#define TextureLoad(tex, pos, idx) tex.Load(pos, idx)
#else
#define TextureLoad(tex, pos, idx) tex[pos]
#endif

float GetLinearDepth(float zw)
{
	float a = _ProjMat._m22, b = _ProjMat._m32;
	return b / (zw - a);
}

// compute world-space position from post-projection depth
float3 GetWorldPosition(float2 screenUV, float zw)
{
	float3 cPos = float3(2.0f * screenUV - 1.0f, zw);
	cPos.y *= -1.0f;
	float4 wPos = mul(float4(cPos, 1.0f), _InvViewProjMat);
	return wPos.xyz / wPos.w;
}

float GetShadow(float3 shadowCoord, Texture2DArray shadowMapArray, uint shadowIndex, SamplerComparisonState shadowSampler)
{
	float2 shadowMapSize;
	float numSlices;
	shadowMapArray.GetDimensions(shadowMapSize.x, shadowMapSize.y, numSlices);
	float2 shadowTexelSize = 1.0f / shadowMapSize;

	float3 sampleUV = float3(shadowCoord.xy, shadowIndex);
#ifdef SINGLE_SAMPLE
	float result = shadowMapArray.SampleCmpLevelZero(shadowSampler, sampleUV, shadowCoord.z).r;
#else
	const float Dilation = 2.0;
	float d1 = Dilation * shadowTexelSize.x * 0.125;
	float d2 = Dilation * shadowTexelSize.x * 0.875;
	float d3 = Dilation * shadowTexelSize.x * 0.625;
	float d4 = Dilation * shadowTexelSize.x * 0.375;
	float result = (
		2.0 * shadowMapArray.SampleCmpLevelZero(shadowSampler, sampleUV, shadowCoord.z).r +
		shadowMapArray.SampleCmpLevelZero(shadowSampler, sampleUV + float3(-d2,  d1, 0), shadowCoord.z).r +
		shadowMapArray.SampleCmpLevelZero(shadowSampler, sampleUV + float3(-d1, -d2, 0), shadowCoord.z).r +
		shadowMapArray.SampleCmpLevelZero(shadowSampler, sampleUV + float3( d2, -d1, 0), shadowCoord.z).r +
		shadowMapArray.SampleCmpLevelZero(shadowSampler, sampleUV + float3( d1,  d2, 0), shadowCoord.z).r +
		shadowMapArray.SampleCmpLevelZero(shadowSampler, sampleUV + float3(-d4,  d3, 0), shadowCoord.z).r +
		shadowMapArray.SampleCmpLevelZero(shadowSampler, sampleUV + float3(-d3, -d4, 0), shadowCoord.z).r +
		shadowMapArray.SampleCmpLevelZero(shadowSampler, sampleUV + float3( d4, -d3, 0), shadowCoord.z).r +
		shadowMapArray.SampleCmpLevelZero(shadowSampler, sampleUV + float3( d3,  d4, 0), shadowCoord.z).r
		) / 10.0;
#endif
	return result * result;
}

// shades a single sample point, given a pixel position and an MSAA subsample index
void ShadeSample(in uint2 pixelPos, in uint sampleIdx, in uint numMSAASamples)
{
	Texture2D tangentFrameMap = _GBuffer[0];
	Texture2D uvMap = _GBuffer[1];
	Texture2D uvGradientMap = _GBuffer[2];
	Texture2D<float> depthMap = _DepthTexture;
	Texture2D<uint> materialIDMap = _TexMaterialID;
	// Texture2D materialIDMap = _GBuffer[4];

	uint packedMaterialID = TextureLoad(materialIDMap, pixelPos, sampleIdx).r;
	if ((packedMaterialID & 0x7f) == 0)	// 没有材质直接返回（packedMaterilID 从1开始）
		return;

	Quaternion tangentFrame = UnpackQuaternion(TextureLoad(tangentFrameMap, pixelPos, sampleIdx));
	float4 uvSample = TextureLoad(uvMap, pixelPos, sampleIdx);
	float2 uv = uvSample.xy * DeferredUVScale;
	float2 zwGradients = uvSample.zw;
	
	float zw = TextureLoad(depthMap, pixelPos, sampleIdx).x;

	// recover the tangent frame handedness from the material ID, and then reconstruct the w component
	float handedness = (packedMaterialID & 0x80) ? -1.0f : 1.0f;
	float3x3 tangentMatrix = QuatTo3x3(tangentFrame);
	tangentMatrix._m10_m11_m12 *= handedness;

#if _ComputeUVGradients
	int2 iPixelPos = int2(pixelPos);
	// compute gradients, trying not to walk off the edge of the triangle that isn't coplanar
	float4 zwGradUp 	= TextureLoad(uvMap, iPixelPos + int2( 0, -1), sampleIdx);
	float4 zwGradDown 	= TextureLoad(uvMap, iPixelPos + int2( 0, +1), sampleIdx);
	float4 zwGradLeft	= TextureLoad(uvMap, iPixelPos + int2(-1,  0), sampleIdx);
	float4 zwGradRight	= TextureLoad(uvMap, iPixelPos + int2(+1,  0), sampleIdx);

	uint matIDUp 	= TextureLoad(materialIDMap, iPixelPos + int2( 0, -1), sampleIdx);
	uint matIDDown	= TextureLoad(materialIDMap, iPixelPos + int2( 0, +1), sampleIdx);
	uint matIDLeft 	= TextureLoad(materialIDMap, iPixelPos + int2(-1,  0), sampleIdx);
	uint matIDRight	= TextureLoad(materialIDMap, iPixelPos + int2(+1,  0), sampleIdx);

	const float zwGradThreshold = 0.0025f;
	bool bUp 	= all(abs(zwGradUp.zw - zwGradients) <= zwGradThreshold) && (matIDUp == packedMaterialID);
	bool bDown 	= all(abs(zwGradDown.zw - zwGradients) <= zwGradThreshold) && (matIDDown == packedMaterialID);
	bool bLeft 	= all(abs(zwGradLeft.zw - zwGradients) <= zwGradThreshold) && (matIDLeft == packedMaterialID);
	bool bRight = all(abs(zwGradRight.zw - zwGradients) <= zwGradThreshold) && (matIDRight == packedMaterialID);

	float2 uvdx = 0.0f;
	float2 uvdy = 0.0f;	
	if (bUp)
		uvdy = uv - zwGradUp.xy * DeferredUVScale;
	else if (bDown)
		uvdy = zwGradDown.xy * DeferredUVScale - uv;

	if (bLeft)
		uvdx = uv - zwGradLeft.xy * DeferredUVScale;
	else if (bRight)
		uvdx = zwGradRight.xy * DeferredUVScale - uv;

	// check for wrapping around due to frac(), and correct for it.
	if (uvdx.x > 1.0f)
		uvdx.x -= 2.0f;
	else if (uvdx.x < -1.0f)
		uvdx.x += 2.0f;
	if (uvdx.y > 1.0f)
		uvdx.y -= 2.0f;
	else if (uvdx.y < -1.0f)
		uvdx.y += 2.0f;

	if (uvdy.x > 1.0f)
		uvdy.x -= 2.0f;
	else if (uvdy.x < -1.0f)
		uvdy.x += 2.0f;
	if (uvdy.y > 1.0f)
		uvdy.y -= 2.0f;
	else if (uvdy.y < -1.0f)
		uvdy.y += 2.0f;
#else
	float4 uvGradients = TextureLoad(uvGradientMap, pixelPos, sampleIdx);
	float2 uvdx = uvGradients.xy;
	float2 uvdy = uvGradients.zw;
#endif

	float2 invRTSize = 1.0f / float2(_ViewportWidth, _ViewportHeight);

	// reconstruct the surface position from the depth buffer
	float linearDepth = GetLinearDepth(zw);
	float2 screenUV = (pixelPos + 0.5f + SubSampleOffsets[sampleIdx]) * invRTSize;
	float3 wPos = GetWorldPosition(screenUV, zw);

	// compute the position derivatives using the stored Z derivatives
	zwGradients = sign(zwGradients) * pow(abs(zwGradients), 2.0f);
	float2 zwNeighbors = saturate(zw.x + zwGradients);
	float3 dxPos = GetWorldPosition( (screenUV + int2(1, 0) * invRTSize), zwNeighbors.x ) - wPos;
	float3 dyPos = GetWorldPosition( (screenUV + int2(0, 1) * invRTSize), zwNeighbors.y ) - wPos;

	uint materialID = (packedMaterialID & 0x7f) - 1;	// -1 获取原始materialID
	
	MaterialData material = _MaterialBuffer[materialID];
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

	uint texbase = MATERIAL_TEXTURE_NUM * materialID;
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
		baseColor = _TexBaseColor.SampleGrad(s_LinearRSampler, uv, uvdx, uvdy);

	// normal
	float3 normal = normalize(tangentMatrix._m20_m21_m22);

	float gloss = 128.0;
	// normal
	if (normalMapType == NormalMapRGB)
	{
		float3 normalMap = _TexNormal.SampleGrad(s_LinearRSampler, uv, uvdx, uvdy).rgb;
		normalMap = normalize((2.0 * normalMap - 1.0) * float3(_NormalScale, _NormalScale, 1.0));

		AntiAliasSpecular(normal, gloss);

		normal = mul(normalMap, tangentMatrix);
	}

	// decals
	const uint numDecals = 2;
	for (uint i = 0; i < numDecals; ++i)
	{
		DecalData decal = _DecalBuffer[i];
		matrix decalWorldMat = decal._WorldMat;
		float3 decalOrientation = normalize(decalWorldMat._m10_m11_m12);	// xz平面法线 - Y
		float cosTheta = dot(normal, decalOrientation);
		if (cosTheta < 0.5f)
			continue;

		matrix decalInvWorldMat = decal._InvWorldMat;
		float3 decalSpacePos = mul(float4(wPos, 1.0f), decalInvWorldMat).xyz;
		if (all(abs(decalSpacePos) < 0.5f))
		{
			uint albedoIndex = decal.albedoIndex;
			if (albedoIndex != uint(-1))
			{
				Texture2D _TexDecalalbedo = _DecalTextures[albedoIndex];

				// uv derivates
				// 1. 
				// wPosRight = wPos + dxPos -> reprojection -> uvRight => (uvRight - uv) = uvdx
				// wPosDown = wPos + dyPos -> reprojection -> uvDown => (uvDown - uv) = uvdy
				// 2.
				float3 dsDX = mul(dxPos, (float3x3)decalInvWorldMat).xyz;
				float3 dsDY = mul(dyPos, (float3x3)decalInvWorldMat).xyz;
				float3 dsDZ = cross(dsDX, dsDY);
				dsDZ = abs(normal);
				float2 uv, uvdx, uvdy;
				uv = decalSpacePos.xz;
				uv.y *= -1.0f;
				uvdx = dsDX.xz; uvdx.y *= -1.0f;
				uvdy = dsDY.xz;	uvdy.y *= -1.0f;
				// if (dsDZ.x > dsDZ.y && dsDZ.x > dsDZ.z)
				// {
				// 	uv = decalSpacePos.yz;
				// 	uv.x *= -1.0f;
				// 	uvdx = dsDX.yz;
				// 	uvdy = dsDY.yz;
				// }
				// else if (dsDZ.y > dsDZ.x && dsDZ.y > dsDZ.z)
				// {
				// 	uv = decalSpacePos.xz;
				// 	uvdx = dsDX.xz;
				// 	uvdy = dsDY.xz;
				// }
				// else
				// {
				// 	uv = decalSpacePos.xy;
				// 	uv.y *= -1.0f;
				// 	uvdx = dsDX.xy;
				// 	uvdy = dsDY.xy;
				// }

				uv += 0.5f;
				// float4 decalAlbedo = _TexDecalalbedo.SampleLevel(s_LinearRSampler, uv, 0.0f);
				float4 decalAlbedo = _TexDecalalbedo.SampleGrad(s_LinearRSampler, uv, uvdx, uvdy);
				baseColor.rgb = lerp(baseColor.rgb, decalAlbedo.rgb, decalAlbedo.a);
				// debug
				// color.rgb = 0.0f;
				
				// normal map
				uint normalIndex = decal.normalIndex;
				if (normalIndex != uint(-1))
				{
					Texture2D _TexDecalNormal = _DecalTextures[normalIndex];
					float3 decalNormal = _TexDecalNormal.SampleGrad(s_LinearRSampler, uv, uvdx, uvdy).rgb;
					decalNormal = decalNormal * 2.0f - 1.0f;
					float3x3 tangentToObject = float3x3(
						1.0f, 0.0f,  0.0f,
						0.0f, 0.0f, -1.0f,
						0.0f, 1.0f,  0.0f);
					float3x3 decalRotation = (float3x3)decalWorldMat;
					decalRotation._m00_m01_m02 = normalize(decalRotation._m00_m01_m02);
					decalRotation._m10_m11_m12 = normalize(decalRotation._m10_m11_m12);
					decalRotation._m20_m21_m22 = normalize(decalRotation._m20_m21_m22);
					// float3x3 tangengToWorld = tangentToObject * decalRotation;	// 分量乘法
					float3x3 tangentToWorld = mul(tangentToObject, decalRotation);	// 矩阵乘法
					decalNormal = mul(decalNormal, tangentToWorld);
					normal = lerp(normal, decalNormal, decalAlbedo.a);
				}
			}
		}
	}

	// shading model
	float4 color = 0;
	float3 specularAlbedo = float3(0.56, 0.56, 0.56);
	float specularMask = 1.0f;
	if (shadingModel == ShadingModel_MetallicRoughness)
	{
		float metallic = _MetallicRoughness.y, perceptualRoughness = _MetallicRoughness.z;
		if (specularType == ChannelTypeTexture)
		{
			float2 metalRough = _TexMetallicRoughness.SampleGrad(s_LinearRSampler, uv, uvdx, uvdy).rg;
			metallic = metalRough.x, perceptualRoughness = metalRough.y;
		}

	}
	else if (shadingModel == ShadingModel_SpecularGlossiness)
	{
		specularMask = _TexMetallicRoughness.SampleGrad(s_LinearRSampler, uv, uvdx, uvdy).g;
	}
	else 	// unlit
	{

	}

	float3 worldPos = wPos;
	float3 viewDir = normalize(worldPos - _CamPos);	// world-space vector from eye to point

	// basic lighting
	float3 lighting = 0.0f;

	// ambient
	lighting += 0.25f * ApplyAmbientLight(baseColor.rgb, 1.0f, _AmbientColor);

	// shadows
	float shadow = 1.0f;
	{
		float viewDepth = abs(linearDepth);		// View Space 可能为右手坐标系，Z轴向外，深度为负
		uint split = NumCascades - 1;
		const float CascadeSplits[] = {_CascadeSplits.x, _CascadeSplits.y, _CascadeSplits.z};
		for (uint i = 0; i < NumCascades-1; ++i)
		{
			if (viewDepth < _CascadeSplits[i])
			{
				split = i;
				break;
			}
		}
		// debug 指定阴影层级
		// 目前只渲染一级阴影 -2020-4-29
		split = 0;
		matrix activeViewProjMat = _LightViewProjMat[split];
		float4 shadowCoord = mul(float4(worldPos, 1.0f), activeViewProjMat);
		shadowCoord.xyz /= shadowCoord.w;
		// shadow = GetShadow(shadowCoord.xyz, _CascadedShadowMap, split, s_ShadowSampler);
		// shadow = _CascadedShadowMap.SampleLevel(s_LinearRSampler, float3(shadowCoord.xy, split), 0.0f).r;
		shadow = _CascadedShadowMap.SampleCmpLevelZero(s_ShadowSampler, float3(shadowCoord.xy, split), shadowCoord.z);

		// float3 uvIndex = float3(screenUV, 0.0f);
		// shadow = _CascadedShadowMap.SampleLevel(s_LinearRSampler, uvIndex, 0.0f).r;
	}
	
	// sun light
	lighting += shadow * ApplyDirectionalLight(baseColor.rgb, specularAlbedo, specularMask, gloss, normal, viewDir,
		_SunDirection, _SunColor, float3(0.0f, 0.0f, 0.0f));

	color.rgb = lighting;
	
	// debug baseColor
	// color = baseColor;

	// debug normal
	// color.rgb = normal * 0.5 + 0.5;

	// debug materialID
	// color = (float)materialID / 16;

	// debug shadow
	// color = shadow;
	
	// cascade splits
	// color = DebugColors[split];

	Output[pixelPos] = color;

}

[RootSignature(DeferredCS_RootSig)]
[numthreads(DeferredTileSize, DeferredTileSize, 1)]
void main( 
	uint3 dtid	: SV_DispatchThreadID,
	uint3 gtid	: SV_GroupThreadID,
	uint3 gid	: SV_GroupID,
	uint gtindex: SV_GroupIndex)
{
	if (dtid.x >= _ViewportWidth || dtid.y >= _ViewportHeight)
		return;

	const uint2 pixelPos = dtid.xy;
	const uint numMSAASamples = 1;

	ShadeSample(pixelPos, 0, numMSAASamples);
	
}

/**
 * 	Load(DirectX HLSL Texture Object)
 * 	reads texel data without any filtering or sampling
 * 	ret Object.Load(int Location, int SampleIndex, int Offset)
 * 	Location - the texture coordinate; the last component specifies the mipmap level. This method
 * use a 0-based coordinate system and not a 0.0-1.0 UV system. The argument type is dependent on 
 * the texture-object type.
 * 		Buffer - int, 
 * 		Texture1D/Texture2DMS - int2, 
 * 		Texture1DArray, Texture2D, Texture2DMSArray - int3
 * 	SampleIndex - an optional sampling index
 * 		SampleIndex is only available for multi-sample textures
 * 	Offset - in optional offset applied to the texture coordinates before sampling. The offset type
 * is dependent on the texture-object type.
 * 		Texture1D, Texture1DArray - int,
 * 		Texture2D, Texture2DArray, Texture2DMS, Texture2DMSArray - in2,
 * 		Texture3D, Texture2DArray - in3
 */
