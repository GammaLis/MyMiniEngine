#include "Quaternion.hlsli"
#include "MaterialDefines.hlsli"
#include "../Scenes/MaterialDefines.h"

#define DeferredCS_RootSig \
	"RootFlags(0)," \
	"CBV(b0)," \
	"DescriptorTable(SRV(t0, numDescriptors = 5))," \
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
		"filter = FILTER_MIN_MAG_MIP_POINT)"

#define MATERIAL_TEXTURE_NUM 5

#define DeferredTileSize 16
#define ThreadGroupSize (DeferredTileSize * DeferredTileSize)

static const float DeferredUVScale = 2.0f;

cbuffer CSConstants	: register(b0)
{
	uint _ViewportWidth, _ViewportHeight;
	float _NearClip, _FarClip;
	float3 _CamPos;
	matrix _ViewProjMat;
	matrix _InvViewProjMat;
	matrix _ViewMat;
	matrix _ProjMat;
};

Texture2D<uint> _TexMaterialID	: register(t0);
Texture2D _GBuffer[4]	: register(t1);

// Texture2D _GBuffer[4]	: register(t0);
// Texture2D<uint> _TexMaterialID	: register(t4);
/**
 * 这种顺序总是提示出错，_Tex未绑定	-2020-4-21
 * 	Texture2D _TexArr[]	: register(t0);
 * 	Texture2D _Tex 	: register(t4);
 */

// materials
StructuredBuffer<MaterialData> _MaterialBuffer	: register(t1, space1);
Texture2D _MaterialTextures[]	: register(t4, space1);

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

// shades a single sample point, given a pixel position and an MSAA subsample index
void ShadeSample(in uint2 pixelPos, in uint sampleIdx, in uint numMSAASamples)
{
	Texture2D tangentFrameMap = _GBuffer[0];
	Texture2D uvMap = _GBuffer[1];
	Texture2D uvGradientMap = _GBuffer[2];
	Texture2D depthMap = _GBuffer[3];
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

	// decals
	const uint numDecals = 2;
	for (uint i = 0; i < numDecals; ++i)
	{
		DecalData decal = _DecalBuffer[i];
		matrix decalInvWorldMat = decal._InvWorldMat;
		float3 decalSpacePos = mul(float4(wPos, 1.0f), decalInvWorldMat).xyz;
		if (all(abs(decalSpacePos) < 0.5f))
		{
			uint albedoIndex = decal.albedoIndex;
			if (albedoIndex != uint(-1))
			{
				Texture2D albedo = _DecalTextures[albedoIndex];

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
				uvdx = dsDX.xz;
				uvdy = dsDY.xz;
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
				// float4 decalAlbedo = albedo.SampleLevel(s_LinearRSampler, uv, 0.0f);
				float4 decalAlbedo = albedo.SampleGrad(s_LinearRSampler, uv, uvdx, uvdy);

				baseColor.rgb = lerp(baseColor.rgb, decalAlbedo.rgb, decalAlbedo.a);
				// debug
				// color.rgb = 0.0f;
			}
		}
	}	

	float4 color = baseColor;

	// debug materialID
	// color = (float)materialID / 16;

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
