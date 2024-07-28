#ifndef MODELVIEWER_COMMON_INCLUDED
#define MODELVIEWER_COMMON_INCLUDED

// outdated warning about for-loop variable scope
#pragma warning (disable: 3078)
// single-iteration loop
#pragma warning (disable: 3557)

/**
	kMeshConstants = 0,
	kMaterialConstants,
	kMaterialSRVs,
	kMaterialSamplers,
	kCommonCBV,
	kCommonSRVs,
	kSkinMatrices,
*/
#define ModelViewer_RootSig \
	"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)," \
	"CBV(b0, visibility = SHADER_VISIBILITY_VERTEX)," \
	"CBV(b0, visibility = SHADER_VISIBILITY_PIXEL)," \
	"DescriptorTable(SRV(t0, numDescriptors = 10), visibility = SHADER_VISIBILITY_PIXEL)," \
	"DescriptorTable(Sampler(s0, numDescriptors = 10), visibility = SHADER_VISIBILITY_PIXEL)," \
	"RootConstants(b1, num32BitConstants = 2), " \
	"DescriptorTable(SRV(t10, numDescriptors = 10), visibility = SHADER_VISIBILITY_PIXEL)," \
	"SRV(t20, visibility = SHADER_VISIBILITY_VERTEX), " \
	"StaticSampler(s10, maxAnisotropy = 8, visibility = SHADER_VISIBILITY_PIXEL)," \
	"StaticSampler(s11, visibility = SHADER_VISIBILITY_PIXEL," \
		"addressU = TEXTURE_ADDRESS_CLAMP," \
		"addressV = TEXTURE_ADDRESS_CLAMP," \
		"addressW = TEXTURE_ADDRESS_CLAMP," \
		"comparisonFunc = COMPARISON_GREATER_EQUAL," \
		"filter = FILTER_MIN_MAG_LINEAR_MIP_POINT)," \
	"StaticSampler(s12, maxAnisotropy = 8, visibility = SHADER_VISIBILITY_PIXEL)," \
	"StaticSampler(s13, visibility = SHADER_VISIBILITY_PIXEL," \
		"addressU = TEXTURE_ADDRESS_CLAMP", \
		"addressV = TEXTURE_ADDRESS_CLAMP", \
		"addressW = TEXTURE_ADDRESS_CLAMP", \
		"filter = FILTER_MIN_MAG_MIP_POINT)"

// common (static) samplers
SamplerState s_DefaultSampler : register(s10);
SamplerComparisonState s_ShadowSampler : register(s11);
SamplerState s_CubeMapSampler : register(s12);
SamplerState s_PointClampSampler : register(s13);

#ifndef ENABLE_TRIANGLE_ID
#define ENABLE_TRIANGLE_ID 0
#endif 

#if ENABLE_TRIANGLE_ID

uint HashTriangleID(uint vertexID)
{
	// TBD SM6.1 stuff
	uint index0 = EvaluateAttributeAtVertex(vertexID, 0);
	uint index1 = EvaluateAttributeAtVertex(vertexID, 1);
	uint index2 = EvaluateAttributeAtVertex(vertexID, 2);

	// when triangles are clipped (to the near plane?)  their interpolants can sometimes
	// be reordered. To stabilize the ID generation, we need to sort the indices before forming the hash.
	uint i0 = __XB_Min3_U32(index0, index1, index2);
	uint i1 = __XB_Med3_U32(index0, index1, index2);
	uint i2 = __XB_Max3_U32(index0, index1, index2);
	
	return (I2 & 0xFF) << 16 | (I1 & 0xFF) << 8 | (I0 & 0xFF0000FF);
}

#endif  // ENABLE_TRIANGLE_ID

#endif // MODELVIEWER_COMMON_INCLUDED
