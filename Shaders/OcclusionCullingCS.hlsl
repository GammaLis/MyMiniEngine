#include "CommonCullingRS.hlsli"

#define DEBUG_CULLING 	0

#define GroupSize 	1024

cbuffer CSConstants	: register(b0)
{
	uint2 _TexSize;
	uint _MaxMipLevel;
};
cbuffer CSCamera	: register(b1)
{
	float3 _CamPos;
	matrix _ViewProjMat;
	matrix _InvViewProjMat;
	matrix _ViewMat;
	matrix _ProjMat;
};

StructuredBuffer<AABB> _WorldBounds	: register(t0);
StructuredBuffer<IndirectArgs> _CommandBuffer	: register(t1);
ByteAddressBuffer _CommandCounter	: register(t2);
Texture2D<float> _HiZTexture	: register(t3);

RWStructuredBuffer<IndirectArgs> OutputBuffer	: register(u0);

bool OcclusionCull(const AABB bound)
{
	float3 boxMin = bound.vMin;
	float3 boxMax = bound.vMax;
	float3 boxSize = boxMax - boxMin;
	float3 boxCorners[8] = 
	{
		boxMin,
		float3(boxMax.x, boxMin.yz),
		float3(boxMax.xy, boxMin.z),
		float3(boxMin.x, boxMax.y, boxMin.z),

		float3(boxMin.x, boxMax.y, boxMax.z),
		float3(boxMin.xy, boxMax.z),
		float3(boxMax.x, boxMax.yz),
		boxMax
	};

#ifdef REVERZED_Z
	float maxZ = 0.0;
#else
	float minZ = 1.0;
#endif
	float2 minXY = 1.0;
	float2 maxXY = 0.0;

	[unroll]
	for (uint i = 0; i < 8; ++i)
	{
		// transform box from world space to NDC
		float4 clipPos = mul(float4(boxCorners[i], 1.0), _ViewProjMat);
		clipPos.z = max(clipPos.z, 0);
		clipPos /= clipPos.w;
		clipPos.xy = clamp(clipPos.xy, -1.0, 1.0);
		clipPos.xy = clipPos.xy * float2(0.5, -0.5) + 0.5;

		minXY = min(clipPos.xy, minXY);
		maxXY = max(clipPos.xy, maxXY);
#ifdef REVERZED_Z
		maxZ = saturate(max(clipPos.z, maxZ));
#else
		minZ = saturate(min(clipPos.z, minZ));
#endif
	}

	float4 boxUVs = float4(minXY, maxXY);
	float4 boxVPs = boxUVs * _TexSize.xyxy;

	// calculate Hi-Z buffer mip
	uint2 size = (maxXY - minXY) * _TexSize;
	float mip = ceil(log2(max(size.x, size.y)));
	mip = clamp(mip, 0, _MaxMipLevel);	// for some reason, Hi-Z texture mip level is limited

	// https://blog.selfshadow.com/publications/practical-visibility/
	// it is possible to improve upon the MIP level selection for situations when an object covers
	// less texels.
	// texel footprint for the lower (finer-grained) level
	float levelLower = max(mip - 1, 0);
	float2 scale = exp2(-levelLower);
	float2 a = floor(boxVPs.xy * scale);
	float2 b = ceil(boxVPs.zw * scale);
	float2 dims = b - a;

	// use the lower level if we only touch <= 2 texels in both dimensions
	if (dims.x <= 2 && dims.y <= 2)
		mip = levelLower;

	// load depths from Hi-Z texture
	float4 depth = 
	{
		_HiZTexture.SampleLevel(s_PointClampSampler, boxUVs.xy, mip),
		_HiZTexture.SampleLevel(s_PointClampSampler, boxUVs.xw, mip),
		_HiZTexture.SampleLevel(s_PointClampSampler, boxUVs.zw, mip),
		_HiZTexture.SampleLevel(s_PointClampSampler, boxUVs.zy, mip)
	};

#ifdef REVERZED_Z
	float minDepth = min( min( min(depth.x, depth.y), depth.z ), depth.w );
	return minDepth > maxZ;
#else
	float maxDepth = max( max( max(depth.x, depth.y), depth.z ), depth.w );
	return maxDepth < minZ;
#endif
}

groupshared uint sh_CullValues[GroupSize];
groupshared uint sh_SummedIndex[GroupSize];

[RootSignature(Culling_RootSig)]
[numthreads(GroupSize, 1, 1)]
void main(
	uint3 dtid 	: SV_DispatchThreadID,
	uint3 gtid	: SV_GroupThreadID,
	uint3 gid 	: SV_GroupID,
	uint gtIndex: SV_GroupIndex )
{
	sh_CullValues[gtIndex] = 0;
	sh_SummedIndex[gtIndex] = 0;
	GroupMemoryBarrierWithGroupSync();

	uint commandCount = _CommandCounter.Load(0);
	if (dtid.x < commandCount)
	{
		// occusion culling
		IndirectArgs commandArgs = _CommandBuffer[dtid.x];
		uint perInstanceCount = 1;	// 假定 每个Command只有一个Instance，否则不好区分PerDraw or PerInstance
		uint instanceId = commandArgs.drawArgs.startInstanceLocation + perInstanceCount - 1;

		AABB worldBound = _WorldBounds[instanceId];
		bool bOcclusionCull = OcclusionCull(worldBound);
		uint val = bOcclusionCull ? 0 : 1;
		sh_CullValues[gtid.x] = val;
		sh_SummedIndex[gtid.x] = val;
	}

	GroupMemoryBarrierWithGroupSync();

	// prefix sum
	// <<GPU Gems 3 - Chapter 39. Parallel Prefix Sum (Scan) with CUDA>>
	uint d = 1;
	uint index = 0;
	for ( ; 2 * d < GroupSize; d <<= 1)
	{
		// if ( (gtid.x + 1) % d == 0 ) ...
		
		index = 2 * d * (gtid.x + 1) - 1;	
		if (index < GroupSize)
		{
			sh_SummedIndex[index] += sh_SummedIndex[index - d];
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (gtid.x == 0)
		sh_SummedIndex[2 * d - 1] = 0;	// total sum [2 * d - 1]
	GroupMemoryBarrierWithGroupSync();
	for ( ; d > 0; d >>= 1)
	{
		index = 2 * d * (gtid.x + 1) - 1;
		if (index < GroupSize)
		{
			uint t = sh_SummedIndex[index];
			sh_SummedIndex[index] += sh_SummedIndex[index - d];
			sh_SummedIndex[index - d] = t;
		}
		GroupMemoryBarrierWithGroupSync();
	}

	if (dtid.x < commandCount)
	{
		IndirectArgs commandArgs = _CommandBuffer[dtid.x];
		uint perInstanceCount = 1;	// 假定 每个Command只有一个Instance，否则不好区分PerDraw or PerInstance
		uint instanceId = commandArgs.drawArgs.startInstanceLocation + perInstanceCount - 1;
		if (sh_CullValues[gtid.x] > 0)
		{
			OutputBuffer[sh_SummedIndex[gtid.x]] = commandArgs;
			OutputBuffer.IncrementCounter();
		}
	}
}

/**
 * 	https://blog.selfshadow.com/publications/practical-visibility/
 * 	https://interplayoflight.wordpress.com/2017/11/15/experiments-in-gpu-based-occlusion-culling/
 */
