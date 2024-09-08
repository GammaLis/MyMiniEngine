#include "CommonCullingRS.hlsli"

#define DEBUG_CULLING 	0
#define NUM_BANKS 		32

#define GroupSize 	1024

/**
 * 	Root Signatures
 * 	0 - RootConstants
 * 	1 - CBV
 * 	2 - [in] 1 AABB buffers + 1 CommandBuffer
 * 	3 - [out]1 CommandBuffer 
 */
#if 0
cbuffer CSConstants : register(b0)
{
	uint2 _OpaqueOffsetCount;
	uint _InstanceCount;
	uint _GridDimensions;
};
#else
cbuffer CSConstants : register(b0)
{
	uint2 _ReadStartCount;
	uint _WriteStart;
	uint _InstanceCount;
	uint _GridDimensions;
};
#endif

#if !USE_VIEW_UNIFORMS
cbuffer CSCamera	: register(b1)
{
	float3 _CamPos;
	matrix _ViewProjMat;
	matrix _InvViewProjMat;
	matrix _ViewMat;
	matrix _ProjMat;
};
#else
ConstantBuffer<ViewUniformParameters> _View : register(b1);
#endif

StructuredBuffer<AABB> _WorldBounds	: register(t0);
StructuredBuffer<IndirectArgs> _CommandBuffer	: register(t1);

RWStructuredBuffer<IndirectArgs> OutputBuffer	: register(u0);
// auxiliary buffers
#if DEBUG_CULLING
RWByteAddressBuffer CullValues	: register(u1);
RWByteAddressBuffer SummedIndex	: register(u2);
#endif

Frustum GetFrustum(matrix mat)
{
	float4 FrustumPlanes[6];
	
#if 0
	// 6 clip planes in NDC
	FrustumPlanes[0] = float4(1.0f, 0.0f, 0.0f, 1.0f); // left
	FrustumPlanes[1] = float4(-1.0f, 0.0f, 0.0f, 1.0f); // right
	FrustumPlanes[2] = float4(0.0f, 1.0f, 0.0f, 1.0f); // up
	FrustumPlanes[3] = float4(0.0f, -1.0f, 0.0f, 1.0f); // down
		
	// normal Z
	// FrustumPlanes[4] = float4( 0.0f,  0.0f,  1.0f, 0.0f);	// front
	// FrustumPlanes[5] = float4( 0.0f,  0.0f, -1.0f, 1.0f);	// back

	// reversed Z
	FrustumPlanes[4] = float4(0.0f, 0.0f, -1.0f, 1.0f); // front
	FrustumPlanes[5] = float4(0.0f, 0.0f, 1.0f, 0.0f); // back
#endif

	matrix viewProjTranspose = transpose(mat);
	FrustumPlanes[0] =  viewProjTranspose[0] + viewProjTranspose[3];
	FrustumPlanes[1] = -viewProjTranspose[0] + viewProjTranspose[3];
	FrustumPlanes[2] =  viewProjTranspose[1] + viewProjTranspose[3];
	FrustumPlanes[3] = -viewProjTranspose[1] + viewProjTranspose[3];

	// normal Z
	// FrustumPlanes[4] =  viewProjTranspose[2] /*+ viewProjTranspose[3]*/;
	// FrustumPlanes[5] = -viewProjTranspose[2] + viewProjTranspose[3];

	// reversed Z
	FrustumPlanes[4] = -viewProjTranspose[2] + viewProjTranspose[3];
	FrustumPlanes[5] = viewProjTranspose[2] /*+ viewProjTranspose[3]*/;

	// normalize planes
	for (uint i = 0; i < 6; ++i)
	{
		float rLen = rsqrt((dot(FrustumPlanes[i].xyz, FrustumPlanes[i].xyz)));
		FrustumPlanes[i] *= rLen;
	}
	
	Frustum frustum;
	frustum.frustumPlanes[0] = FrustumPlanes[0];
	frustum.frustumPlanes[1] = FrustumPlanes[1];
	frustum.frustumPlanes[2] = FrustumPlanes[2];
	frustum.frustumPlanes[3] = FrustumPlanes[3];
	frustum.frustumPlanes[4] = FrustumPlanes[4];
	frustum.frustumPlanes[5] = FrustumPlanes[5];
	
	return frustum;
}

bool CalcFrustumBoundingBoxIntersection(const Frustum frustum, const AABB bound)
{
	float3 boxMin = bound.vMin;
	float3 boxMax = bound.vMax;
	float3 center = (boxMin + boxMax) * 0.5;
	float3 extent = (boxMax - boxMin) * 0.5;
	
	[unroll]
	for (uint i = 0; i < 6; ++i)
	{
		float4 frustumPlane = frustum.frustumPlanes[i];		
		float d = dot(frustumPlane.xyz, center.xyz) + frustumPlane.w + dot(abs(frustumPlane.xyz), extent.xyz);
		if (d < 0.0)
			return false;
	}
	return true;
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
	sh_CullValues[gtid.x] = 0;
	sh_SummedIndex[gtid.x] = 0;
	
	GroupMemoryBarrierWithGroupSync();
	
	// if (dtid.x < _InstanceCount)
	// TODO: Frustum可以放到LDS里，不必每个Thread都去创建
	if (dtid.x < _ReadStartCount.y)
	{
	#if USE_VIEW_UNIFORMS
		Frustum frustum = GetFrustum(_View.viewProjMat);
	#else
		Frustum frustum = GetFrustum(_ViewProjMat);
	#endif
		
		// frustum culling
		AABB worldBound = _WorldBounds[_ReadStartCount.x + dtid.x];
		bool bIntersect = CalcFrustumBoundingBoxIntersection(frustum, worldBound);
		uint val = bIntersect ? 1 : 0;
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
	
	// debug
#if DEBUG_CULLING
	CullValues.Store(4 * (_WriteStart + dtid.x), sh_CullValues[gtid.x]);
	SummedIndex.Store(4 * (_WriteStart + dtid.x), sh_SummedIndex[gtid.x]);
	GroupMemoryBarrierWithGroupSync();
#endif

	// 
	// if (dtid.x < _InstanceCount)
	if (dtid.x < _ReadStartCount.y)
	{
		IndirectArgs commandArgs = _CommandBuffer[_ReadStartCount.x + dtid.x];
		uint perInstanceCount = 1;	// suppose one Command just has one Instance, otherwise has issue with 'PerDraw' or 'PerInstance'
		uint instanceId = commandArgs.drawArgs.startInstanceLocation + perInstanceCount - 1;
		if (sh_CullValues[gtid.x] > 0)
		{
			OutputBuffer[_WriteStart + sh_SummedIndex[gtid.x]] = commandArgs;
			OutputBuffer.IncrementCounter();
		}
	}
}

/**
 * 	IncrementCounter function
 * 	increments the object's hidden counter
 * 	uint IncrementCounter(void);
 * 	return value - uint, the pre-incremented counter value
 */

/**
 * 	<<GPU Gems 3 - Chapter 39. Parallel Prefix Sum (Scan) with CUDA>>
 * 	Bank Conflict 
 * 	... the shared memory exploited by this scan algorithm is made of multiple banks. When multiple
 *  threads in the same warp access the same bank, a bank conflict occurs unless all threads
 *  of the warp access the same address with the same 32-bit word. The number of threads that access
 *  a single bank is called the degree of the bank conflict. Bank conflict cause serialization of
 *  the multiple accesses to the memory bank, so that a shared memory access with a degree-n bank conflict
 *  requires n times as many cycles to process as an access with no conflict.
 *  	... Bank conflicts are avoidable in most CUDA compuations if care is taken when accessing __shared__
 *  memory arrays. >>>We can avoid most bank conflicts in scan by adding a variable amount of padding
 *  to each shared memory array index we compute. Specifically, we add to the index the value of the index
 *  divided by the number of shared memory banks. <<<
 *
 *
 * 	https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html
 * 	... to achieve high memory bandwidth for concurrent accesses, shared memory is divided into equally sized 
 * memory modules(banks) that can be accessed simultaneously. Therefore, any memory load or store of n addresses
 * that spans n distinct memory banks can be serviced simultaneously, yielding an effective bandwidth that is n times
 * as high as the bandwidth of a single bank.
 * 	however, if multiple addresses of a memory request map to the same memory bank, the accesses are serialized. 
 * The hardware splits a memory request that has bank conflicts into as many separate conflict-free requests as 
 * necessary, decreasing the effective bandwidth by a factor equal to the number of separate memory requests. 
 * The one exception here is when multiple threads in a warp address the same shared memory location, resulting 
 * in a broadcast, multiple broadcasts from different banks are coalesced into a single multicast from the 
 * requested shared memory locations to the threads.
 * 	To minimize bank conflicts, it is important to understand how memory addresses map to memory banks and how to
 * optimally schedule memory requests.
 * 	On devices of compute capability 5.x or newer, each bank has a bandwidth of 32 bits every clock cycle, and
 * successive 32-bit words are assigned to successive banks. The warp size is 32 threads and the number of 
 * banks is also 32, so bank conflicts can occur between any threads in the warp.
 */
