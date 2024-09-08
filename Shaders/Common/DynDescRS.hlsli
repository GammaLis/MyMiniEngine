#pragma once

// Ref: https://learn.microsoft.com/en-us/windows/win32/direct3d12/specifying-root-signatures-in-hlsl

#define DynResource_RootSig \
	"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED )," \
	"RootConstants(b0, num32BitConstants = 64, space = 1, Visibility = SHADER_VISIBILITY_ALL)," \
	"StaticSampler(s0, " \
		"filter = FILTER_MIN_MAG_MIP_LINEAR," \
		"addressU = TEXTURE_ADDRESS_CLAMP," \
		"addressV = TEXTURE_ADDRESS_CLAMP," \
		"addressW = TEXTURE_ADDRESS_CLAMP)," \
	"StaticSampler(s1, " \
		"filter = FILTER_MIN_MAG_MIP_LINEAR," \
		"addressU = TEXTURE_ADDRESS_WRAP," \
		"addressV = TEXTURE_ADDRESS_WRAP," \
		"addressW = TEXTURE_ADDRESS_WRAP)," \
	"StaticSampler(s2, " \
		"filter = FILTER_MIN_MAG_MIP_POINT," \
		"addressU = TEXTURE_ADDRESS_CLAMP," \
		"addressV = TEXTURE_ADDRESS_CLAMP," \
		"addressW = TEXTURE_ADDRESS_CLAMP)," \
	"StaticSampler(s3, " \
		"filter = FILTER_MIN_MAG_MIP_POINT," \
		"addressU = TEXTURE_ADDRESS_BORDER," \
		"addressV = TEXTURE_ADDRESS_BORDER," \
		"addressW = TEXTURE_ADDRESS_BORDER)," \
	"StaticSampler(s4, " \
		"filter = FILTER_ANISOTROPIC," \
		"addressU = TEXTURE_ADDRESS_WRAP," \
		"addressV = TEXTURE_ADDRESS_WRAP," \
		"addressW = TEXTURE_ADDRESS_WRAP," \
		"maxAnisotropy = 8)," \
	"StaticSampler(s5, " \
		"filter = FILTER_MIN_MAG_MIP_POINT," \
		"addressU = TEXTURE_ADDRESS_CLAMP," \
		"addressV = TEXTURE_ADDRESS_CLAMP," \
		"addressW = TEXTURE_ADDRESS_CLAMP," \
		"comparisonFunc = COMPARISON_GREATER_EQUAL)," \
	"StaticSampler(s6, " \
		"filter = FILTER_MIN_MAG_MIP_POINT," \
		"addressU = TEXTURE_ADDRESS_WRAP," \
		"addressV = TEXTURE_ADDRESS_WRAP," \
		"addressW = TEXTURE_ADDRESS_WRAP)"

SamplerState sampler_LinearClamp		: register(s0);
SamplerState sampler_LinearWrap			: register(s1);
SamplerState sampler_PointClamp			: register(s2);
SamplerState sampler_PointBorder		: register(s3);
SamplerState sampler_AnisotropicWrap	: register(s4);
SamplerComparisonState sampler_Shadow	: register(s5);
SamplerState sampler_VolumeWrap			: register(s6);

// Ref: Scene.ViewUniformParameters
struct FViewUniformBufferParameters
{
	float4x4 viewProjMat;
	float4x4 invViewProjMat;
	float4x4 viewMat;
	float4x4 projMat;
	float4 bufferSizeAndInvSize;
	float4 camPos;
	float4 cascadeSplits;
	float nearClip, farClip;
};

#define DESCRIPTOR_INDEX_VIEW (512)

#define DESCRIPTOR_INDEX_MATERIALS (125)
#define DESCRIPTOR_INDEX_MESHES (DESCRIPTOR_INDEX_MATERIALS+1)
#define DESCRIPTOR_INDEX_MESH_INSTANCES (DESCRIPTOR_INDEX_MATERIALS+2)
#define DESCRIPTOR_INDEX_VERTICES (DESCRIPTOR_INDEX_MATERIALS+3)
#define DESCRIPTOR_INDEX_INDICES (DESCRIPTOR_INDEX_MATERIALS+4)
#define DESCRIPTOR_INDEX_MATRICES (DESCRIPTOR_INDEX_MATERIALS+5)

// 25 * 5 = 125
#define DESCRIPTOR_INDEX_MATERIAL_TEXTURES 0
#define NUM_TEXTURES_PER_MATERIAL 5

#define MATERIAL_TEXTURE_BASECOLOR 0
#define MATERIAL_TEXTURE_SPECULAR 1
#define MATERIAL_TEXTURE_NORMAL 2
#define MATERIAL_TEXTURE_EMISSIVE 3
#define MATERIAL_TEXTURE_OCCLUSION 4
