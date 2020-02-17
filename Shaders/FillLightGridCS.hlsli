#include "LightGrid.hlsli"

// outdated warning about for-loop variable scope
#pragma warning(disable: 3078)

#define FLT_MIN 	1.175494351e-38F        // min positive value
#define FLT_MAX 	3.402823466e+38F        // max value
#define PI 			3.1415926535f
#define TWOPI		6.283185307f

#define WORK_GROUP_THREADS (WORK_GROUP_SIZE_X * WORK_GROUP_SIZE_Y * WORK_GROUP_SIZE_Z)

cbuffer CSConstants	: register(b0)
{
	uint _ViewportWidth, _ViewportHeight;
	float _InvTileDim;
	float _RcpZMagic;
	uint _TileCountX;
	float4x4 _ViewProjMat;
};

StructuredBuffer<LightData> _LightBuffer: register(t0);
Texture2D<float> _DepthTex				: register(t1);

RWByteAddressBuffer lightGrid			: register(u0);
RWByteAddressBuffer lightGridBitMask	: register(u1);

groupshared uint minDepthUInt;
groupshared uint maxDepthUInt;

groupshared uint tileLightCountSphere;
groupshared uint tileLightCountCone;
groupshared uint tileLightCountConeShadowed;

groupshared uint tileLightIndicesSphere[MAX_LIGHTS];
groupshared uint tileLightIndicesCone[MAX_LIGHTS];
groupshared uint tileLightIndicesConeShadowed[MAX_LIGHTS];

groupshared uint4 tileLightBitMask;

#define CS_RootSig \
	"RootFlags(0)," \
	"CBV(b0)," \
	"DescriptorTable(SRV(t0, numDescriptors = 2))," \
	"DescriptorTable(UAV(u0, numDescriptors = 2))"

[RootSignature(CS_RootSig)]
[numthreads(WORK_GROUP_SIZE_X, WORK_GROUP_SIZE_Y, WORK_GROUP_SIZE_Z)]
void main(
	uint3 globalID	: SV_DispatchThreadID, 
	uint3 groupID 	: SV_GroupID, 
	uint3 threadID 	: SV_GroupThreadID,
	uint threadIndex: SV_GroupIndex)
{
	// fetch depth
	float depth = -1.0;
	if (globalID.x >= _ViewportWidth || globalID.y >= _ViewportHeight)
	{
		// out of bounds
	}
	else
	{
		depth = _DepthTex[globalID.xy];
	}

	// initialize shared data
	// https://www.3dgep.com/forward-plus
	// since we are setting group-shared variables, only one thread in the group needs to set them. In fact,
	// the HLSL compiler will generate a race-condition error if we don't restrict the writing of these
	// variables to a single thread in the group
	if (threadIndex == 0)
	{
		tileLightCountSphere = 0;
		tileLightCountCone = 0;
		tileLightCountConeShadowed = 0;
		tileLightBitMask = 0;
		minDepthUInt = 0xffffffff;
		maxDepthUInt = 0;
	}
	
	// blocks execution of all threads in a group until all group shared accesses have been completed
	// and all threads in the group have reached this call.
	GroupMemoryBarrierWithGroupSync();

	// determine min/max Z for tile
	if (depth != -1.0)
	{
		// since we can only perform atomic operations on integers, we reinterrpret the bits from the floating-
		// point depth as an unsigned integer.
		uint depthUInt = asuint(depth);

		// performs a guaranteed atomic min
		InterlockedMin(minDepthUInt, depthUInt);
		InterlockedMax(maxDepthUInt, depthUInt);
	}

	// we again need to use the GroupMemoryBarrierWithGroupSync function to ensure all writes to group shared
	// memory have been committed and all threads in the group have reached this point.
	GroupMemoryBarrierWithGroupSync();

	// after the minimum and maximum depth values for the current tile have been found, we can reinterrpret
	// the unsigned integer back to a float.
	float tileMinDepth = asfloat(minDepthUInt);
	float tileMaxDepth = asfloat(maxDepthUInt);
	// float tileMinDepth = (rcp(asfloat(maxDepthUInt)) - 1.0) * _RcpZMagic;
	// float tileMaxDepth = (rcp(asfloat(minDepthUInt)) - 1.0) * _RcpZMagic;
	float tileDepthRange = tileMaxDepth - tileMinDepth;
	tileDepthRange = max(tileDepthRange, FLT_MIN);	// don't allow a depth range of 0
	float invTileDepthRange = rcp(tileDepthRange);
	// TODO: near/far clipping planes seem to be falling apart at or near the max depth with infinite projections

	// construct transform from world space to tile space (projection space constrained to tile area)
	/**
	 * 将原本[-1, 1]中的tile 变换到[-1, 1]
	 * |---------n tiles---------|
	 * |	| 	|	|	...		 |
	 * -1						 1
	 * |	mid		|
	 * -1 -1+1/S	-1+2/S
	 * S
	 * 0-(-1 + 1/S + x*2/S) * S = -2x + S - 1
	 */
	float2 invTileSize2X = float2(_ViewportWidth, _ViewportHeight) * _InvTileDim;
	// D3D-specific [0, 1] depth range ortho projection
	// (but without negation of Z, since we already have that from the projection matrix)
	float3 tileBias = float3(
		-2.0 * float(groupID.x) + invTileSize2X.x - 1.0,
		-2.0 * float(groupID.y) + invTileSize2X.y - 1.0,
		-tileMinDepth * invTileDepthRange);
	// column-major
	// float4x4 projToTile = float4x4(
	// 	invTileSize2X.x, 0, 0, tileBias.x,
	// 	0, -invTileSize2X.y, 0, tileBias.y,
	// 	0, 0, invTileDepthRange, tileBias.z,
	// 	0, 0, 0, 1
	// 	);
	// row-major
	float4x4 projToTile = float4x4(
		invTileSize2X.x, 0, 0, 0,
		0, -invTileSize2X.y, 0, 0,
		0, 0, invTileDepthRange, 0,
		tileBias.x, tileBias.y, tileBias.z, 1);

	float4x4 tileMVP = mul(_ViewProjMat, projToTile);
	tileMVP = transpose(tileMVP);	// 转置

	// extract frustum planes (these will be in world space)
	/**
	 * ------
	 * |	|
	 * ------
	 * left - (1,  0, 0, 1), right - (-1, 0, 0, 1)
	 * top 	- (0, -1, 0, 1), bottom- ( 0, 1, 0, 1)
	 */
	float4 frustumPlanes[6];
	frustumPlanes[0] = tileMVP[3] + tileMVP[0];
	frustumPlanes[1] = tileMVP[3] - tileMVP[0];
	frustumPlanes[2] = tileMVP[3] + tileMVP[1];
	frustumPlanes[3] = tileMVP[3] - tileMVP[1];
	frustumPlanes[4] = tileMVP[3];// + tileMVP[2];
	frustumPlanes[5] = tileMVP[3] - tileMVP[2];
	// normalize planes
	for (int n = 0; n < 6; ++n)
	{
		frustumPlanes[n] *= rsqrt(dot(frustumPlanes[n].xyz, frustumPlanes[n].xyz));
	}

	uint tileIndex = GetTileIndex(groupID.xy, _TileCountX);
	uint tileOffset = GetTileOffset(tileIndex);

	// find set of lights that overlap this tile
	for (uint lightIndex = threadIndex; lightIndex < MAX_LIGHTS; lightIndex += WORK_GROUP_THREADS)
	{
		LightData lightData = _LightBuffer[lightIndex];
		float3 lightWorldPos = lightData.pos;
		float lightCullRadius = sqrt(lightData.radiusSq);

		bool overlapping = true;
		for (int n = 0; n < 6; ++n)
		{
			float d = dot(lightWorldPos, frustumPlanes[n].xyz) + frustumPlanes[n].w;
			if (d < -lightCullRadius)
				overlapping = false;
		}

		if (overlapping)
		{
			switch (lightData.type)
			{
			case 0:	// sphere
				{
					uint slot = 0;
					InterlockedAdd(tileLightCountSphere, 1, slot);
					tileLightIndicesSphere[slot] = lightIndex;
				}
				break;

			case 1:	// cone
				{
					uint slot = 0;
					InterlockedAdd(tileLightCountCone, 1, slot);
					tileLightIndicesCone[slot] = lightIndex;
				}
				break;

			case 2:	// cone w/ shadow map
				{
					uint slot = 0;
					InterlockedAdd(tileLightCountConeShadowed, 1, slot);
					tileLightIndicesConeShadowed[slot] = lightIndex;
				}
				break;
			}

			// update bitmask
			switch (lightIndex / 32)
			{
			case 0:
				InterlockedOr(tileLightBitMask.x, 1 << (lightIndex % 32));
				break;
			case 1:
				InterlockedOr(tileLightBitMask.y, 1 << (lightIndex % 32));
				break;
			case 2:
				InterlockedOr(tileLightBitMask.z, 1 << (lightIndex % 32));
				break;
			case 4:
				InterlockedOr(tileLightBitMask.w, 1 << (lightIndex % 32));
				break;
			}
		}
	}

	GroupMemoryBarrierWithGroupSync();

	if (threadIndex == 0)
	{
		// 4 bytes
		uint lightCount = 
			((tileLightCountSphere & 0xff) << 0) |
			((tileLightCountCone & 0xff) << 8) |
			((tileLightCountConeShadowed & 0xff) << 16) ;
		lightGrid.Store(tileOffset + 0, lightCount);

		uint storeOffset = tileOffset + 4;
		for (uint n = 0; n < tileLightCountSphere; ++n)
		{
			lightGrid.Store(storeOffset, tileLightIndicesSphere[n]);
			storeOffset += 4;
		}
		for (uint n = 0; n < tileLightCountCone; ++n)
		{
			lightGrid.Store(storeOffset, tileLightIndicesCone[n]);
			storeOffset += 4;
		}
		for (uint n = 0; n < tileLightCountConeShadowed; ++n)
		{
			lightGrid.Store(storeOffset, tileLightIndicesConeShadowed[n]);
			storeOffset += 4;
		}

		lightGridBitMask.Store4(tileIndex * 16, tileLightBitMask);
	}

}

/**
 * InterlockedMin
 * Performs a guaranteed atomic min.
 * (in R dest, in T value, out T original_value)
 * dest - the destination address
 * value - the input value
 * original_value - optional. The original input value
 *
 * this operation can only be performed on int and uint typed resources and shared memory variables. There are
 *2 possible uses for this function. The first is when R is a shared memory variable type. In this case, 
 *the function performs an atomic min of value to the shared memory register referenced by dest. The second
 *is when R is a resource variable type. In this scenario, the function performs an atomic min of value to
 *the resource location referenced by dest. The overloaded function has an addtional output value which will
 *be set to the original value of dest. This overloaded operation is only available when R is readable and writable.
 */

/**
 * https://www.3dgep.com/forward-plus
 *
 * groupshared uint uMinDepth;
 * groupshared uint uMaxDepth;
 * to store the min and max depth values per tile, we need to declare some group-shared variables to store
 *the minimum and maximum depth values. The atomic increment functions will be used to make sure that only
 *one thread in a thread group can change the min/max depth values but unfortunately, shader model 5.0 does
 *not provide atomic functions for floating point values. -> The depth values will be stored as unsigned
 *int in group-shared memory which will be atomically compared and updated per thread.
 *
 * 	since the frustum used to perform culling will be the same frustum for all threads in a group, it makes
 *sense to keep only one copy of the frustum for all threads in a group.
 *
 * 	the first thing we will do in the light culling is read the depth value for the current thread. Each thread
 *in the thread group will sample the depth buffer only once for the current thread and thus all threads in
 *a group will sample all depth values for a single tile.
 */

/**
 * -mf
 * reversed-Z 
 * clipZ (0, 1) - viewZ (far, near)
 * clipZ = n / (n-f ) - nf / ((n-f) * viewZ)
 * viewZ = nf / (n + (f-n) * clipZ) = [nf / (f-n)] / [n/(f-n) + clipZ]
 */


// 2010 Intel: Deferred Rendering for Current and Future Rendering Pipelines