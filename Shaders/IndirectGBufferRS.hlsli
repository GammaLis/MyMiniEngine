#ifndef INDIRECT_GBUFFER_HLSLI
#define INDIRECT_GBUFFER_HLSLI

#include "MaterialDefines.hlsli"
#include "MeshDefines.hlsli"

#define IndirectGBuffer_RootSig \
	"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)," \
	"RootConstants(b0, num32BitConstants = 4)," \
	"CBV(b1)," \
	"SRV(t0, space = 1)," \
    "SRV(t1, space = 1)," \
    "SRV(t2, space = 1)," \
    "SRV(t3, space = 1)," \
    "DescriptorTable(SRV(t4, space = 1, numDescriptors = unbounded), visibility = SHADER_VISIBILITY_PIXEL)," \
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

static const float DeferredUVScale = 2.0f;

struct ViewUniformParameters
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
#define USE_VIEW_UNIFORMS 1

cbuffer CBConstants	: register(b0)
{
	float4 _Constants;
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

StructuredBuffer<GlobalMatrix> _MatrixBuffer            : register(t0, space1);
StructuredBuffer<MaterialData> _MaterialBuffer          : register(t1, space1);
StructuredBuffer<MeshDesc> _MeshBuffer                  : register(t2, space1);
StructuredBuffer<MeshInstanceData> _MeshInstanceBuffer  : register(t3, space1);

SamplerState s_LinearRSampler	: register(s0);
SamplerState s_PointCSampler	: register(s1);

#endif	// INDIRECT_GBUFFER_HLSLI
