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
    "SRV(t3, space = 1)"

static const float DeferredUVScale = 2.0f;

cbuffer CBConstants	: register(b0)
{
	float4 _Constants;
};
cbuffer CBPerCamera	: register(b1)
{
	matrix _ViewProjMat;
	float3 _CamPos;
};

StructuredBuffer<GlobalMatrix> _MatrixBuffer            : register(t0, space1);
StructuredBuffer<MaterialData> _MaterialBuffer          : register(t1, space1);
StructuredBuffer<MeshDesc> _MeshBuffer                  : register(t2, space1);
StructuredBuffer<MeshInstanceData> _MeshInstanceBuffer  : register(t3, space1);

#endif	// INDIRECT_GBUFFER_HLSLI
