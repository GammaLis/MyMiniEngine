#define Culling_RootSig \
	"RootFlags(0)," \
	"RootConstants(b0, num32BitConstants = 4)," \
	"CBV(b1)," \
	"SRV(t0)," \
	"DescriptorTable(SRV(t1, numDescriptors = 4))," \
	"DescriptorTable(UAV(u0, numDescriptors = 4))," \
	"StaticSampler(s0, " \
		"addressU = TEXTURE_ADDRESS_CLAMP," \
		"addressV = TEXTURE_ADDRESS_CLAMP," \
		"addressW = TEXTURE_ADDRESS_CLAMP," \
		"filter = FILTER_MIN_MAG_MIP_POINT)"

// outdated warning about for-loop variable scope
#pragma warning (disable: 3078)

#define REVERZED_Z

struct AABB
{
	float3 vMin;
	float3 vMax;
};
struct Frustum
{
	float4 frustumPlanes[6];
};
// D3D12_DRAW_INDEXED_ARGUMENTS
struct DrawIndexedArgs
{
	uint indexCountPerInstance;
	uint instanceCount;
	uint startIndexLocation;
	int baseVertexLocation;
	uint startInstanceLocation;
};
struct IndirectArgs
{
	DrawIndexedArgs drawArgs;
};

SamplerState s_PointClampSampler	: register(s0);
