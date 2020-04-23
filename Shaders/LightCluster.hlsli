#ifndef LIGHT_CLUSTER_HLSLI
#define LIGHT_CLUSTER_HLSLI

#define ClusteredLighting_RootSig \
	"RootFlags(0)," \
	"CBV(b0)," \
	"DescriptorTable(SRV(t0, numDescriptors = 2))," \
	"DescriptorTable(UAV(u0, numDescriptors = 2))"

#define MAX_LIGHTS 128
#define CLUSTER_SIZE (4 + 4 * MAX_LIGHTS)

struct LightData
{
	float3 pos;
	float radiusSq;

	float3 color;
	uint type;

	float3 coneDir;
	float2 coneAngles;	// x = 1.0f / (cos(coneInner) - cos(coneOuter)), y = cos(coneOuter)
	float4x4 shadowTextureMatrix;
};

uint GetClusterOffset(uint clusterIndex)
{
	return clusterIndex * CLUSTER_SIZE;
}

#endif	// LIGHT_CLUSTER_HLSLI