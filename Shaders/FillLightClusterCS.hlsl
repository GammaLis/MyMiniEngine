#include "LightCluster.hlsli"

// outdated warning about for-loop variable scope
#pragma warning(disable: 3078)

#define FLT_MIN 	1.175494351e-38F        // min positive value
#define FLT_MAX 	3.402823466e+38F        // max value
#define PI 			3.1415926535f
#define TWOPI		6.283185307f

#define WORK_GROUP_SIZE 16
#define WORK_GROUP_THREADS (WORK_GROUP_SIZE * WORK_GROUP_SIZE)

#define NUM_DEPTH_SLICE 16

cbuffer CSConstants	: register(b0)
{
	uint _ViewportWidth, _ViewportHeight;
	uint _TileSizeX, _TileSizeY;
	float _NearClip, _FarClip;

	matrix _ViewProjMat;
	matrix _ViewMat;
	matrix _ProjMat;
};

StructuredBuffer<LightData> _LightBuffer	: register(t0);
Texture2D<float> _TexDepth	: register(t1);

RWByteAddressBuffer LightCluster	: register(u0);

groupshared uint minDepthUint;
groupshared uint maxDepthUint;

groupshared uint tempLightCount;
groupshared uint tempLightIndices[MAX_LIGHTS];

groupshared uint pointLightCount;
groupshared uint pointLightIndices[MAX_LIGHTS];
groupshared uint spotLightCount;
groupshared uint spotLightIndices[MAX_LIGHTS];

// Zview = Znear * (Zfar / ZNear) ^ (slice / numSlices)
float GetClusteredViewZ(uint slice, uint numSlices, float zNear, float zFar)
{
	float a = slice / numSlices;
	return zNear * pow( (zFar / zNear), a);
}
uint GetDepthSlice(float zView)
{
	float c = NUM_DEPTH_SLICE / log(_FarClip / _NearClip);
	return uint( log(zView) * c - c * log(_NearClip) );
}
float GetViewDepth(float zClip)
{
	float a = _ProjMat._33, b = _ProjMat._43;
	return b / (zClip - a);
}

[RootSignature(ClusteredLighting_RootSig)]
[numthreads(WORK_GROUP_SIZE, WORK_GROUP_SIZE, 1)]
void main( 
	uint3 dtid 	: SV_DispatchThreadID, 
	uint3 gtid 	: SV_GroupThreadID, 
	uint3 gid 	: SV_GroupID,
	uint  gtindex	: SV_GroupIndex)
{
	uint TileCountX = (_ViewportWidth + _TileSizeX - 1) / _TileSizeX;
	uint TileCountY = (_ViewportHeight + _TileSizeY - 1) / _TileSizeY;
	uint TileCount = TileCountX * TileCountY;

	// initialize
	if (gtindex == 0)
	{
		minDepthUint = 0xffffffff;
		maxDepthUint = 0;

		tempLightCount = 0;

		pointLightCount = 0;
		spotLightCount = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	uint2 baseCoord = gid.xy * uint2(_TileSizeX, _TileSizeY);
	for (uint index = gtindex; index < TileCount; index += WORK_GROUP_THREADS)
	{
		// depth
		uint2 groupCoord = uint2(index / _TileSizeX, index % _TileSizeX);
		uint2 coord = baseCoord + groupCoord;

		float depth = _TexDepth[coord];
		uint depthUint = asuint(depth);
		InterlockedMin(minDepthUint, depthUint);
		InterlockedMax(maxDepthUint, depthUint);
	}
	GroupMemoryBarrierWithGroupSync();

	float tileMinDepth = asfloat(minDepthUint);
	float tileMaxDepth = asfloat(maxDepthUint);
	tileMinDepth = max(tileMinDepth, FLT_MIN);

	// Z is reversed 
	float numSlices = NUM_DEPTH_SLICE;
	float minViewZ = GetViewDepth(tileMaxDepth);
	float maxViewZ = GetViewDepth(tileMinDepth);
	uint minSlice = GetDepthSlice(minViewZ);
	uint maxSlice = GetDepthSlice(maxViewZ);
	maxSlice = min(maxSlice + 1, numSlices);

	float minClusterZ = GetClusteredViewZ(minSlice, numSlices, _NearClip, _FarClip);
	float maxClusterZ = GetClusteredViewZ(maxSlice, numSlices, _NearClip, _FarClip);
	float4 startClusterPlane = float4(0, 0, 1, -minClusterZ);
	float4 lastClusterPlane = float4(0, 0, -1, maxClusterZ);
	
	float3x3 invViewMat = (float3x3)(transpose(_ViewMat));
	float3 viewDir = normalize(invViewMat[2].xyz);

	// ************************************************************
	// pass 1
	float2 invTileSize2X = float2(_ViewportWidth, _ViewportHeight) / float2(_TileSizeX, _TileSizeY);
	float3 tileBias = float3(
		-2.0 * gid.x + invTileSize2X.x - 1.0,
		-(-2.0 * gid.y + invTileSize2X.y - 1.0),
		0.0
		);
	float4x4 projToTile = float4x4(
		invTileSize2X.x, 0, 0, 0,
		0, invTileSize2X.y, 0, 0,
		0, 0, 1, 0,
		tileBias.x, tileBias.y, tileBias.z, 1
		);
	float4x4 tileMVP = mul(_ViewProjMat, projToTile);
	tileMVP = transpose(tileMVP);

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

	// (ViewMat)^T * viewPlane
	frustumPlanes[4] = mul(_ViewMat, startClusterPlane);
	frustumPlanes[5] = mul(_ViewMat, lastClusterPlane);
	startClusterPlane = frustumPlanes[4];

	// normalize planes
	for (uint n = 0; n < 6; ++n)
	{
		frustumPlanes[n] *= rsqrt(dot(frustumPlanes[n].xyz, frustumPlanes[n].xyz));
	}

	// cull lights
	for (uint lightIndex = gtindex; lightIndex < MAX_LIGHTS; lightIndex += WORK_GROUP_SIZE)
	{
		LightData lightData = _LightBuffer[lightIndex];
		float3 lightWorldPos = lightData.pos;
		float lightCullRadius = sqrt(lightData.radiusSq);

		bool bOverlapping = true;
		for (uint n = 0; n < 6; ++n)
		{
			float d = dot(float4(lightWorldPos, 1.0), frustumPlanes[n]);
			if (d < -lightCullRadius)
				bOverlapping = false;
		}

		if (bOverlapping)
		{
			uint index = 0;
			InterlockedAdd(tempLightCount, 1, index);
			tempLightIndices[index] = lightIndex;
		}
	}
	GroupMemoryBarrierWithGroupSync();

	// ************************************************************
	// pass 2
	for (uint zslice = 0; zslice < NUM_DEPTH_SLICE; ++zslice)
	{
		float nearDepth = GetClusteredViewZ(zslice, numSlices, _NearClip, _FarClip);
		float farDepth = GetClusteredViewZ(zslice, numSlices + 1, _NearClip, _FarClip);
		
		float deltaNearZ = nearDepth - minClusterZ;
		float4 nearPlane = startClusterPlane;
		nearPlane.w -= deltaNearZ;
		nearPlane *= rsqrt( dot(nearPlane.xyz, nearPlane.xyz) );

		float deltaFarZ = farDepth - minClusterZ;
		float4 farPlane = -startClusterPlane;
		farPlane.w += deltaFarZ;
		farPlane *= rsqrt( dot(farPlane.xyz, farPlane.xyz) );

		// cull lights
		for (uint i = gtindex; i < tempLightCount; i += WORK_GROUP_SIZE)
		{
			uint lightIndex = tempLightIndices[i];
			LightData lightData = _LightBuffer[lightIndex];
			float3 lightWorldPos = lightData.pos;
			float lightCullRadius = sqrt(lightData.radiusSq);

			bool bOverlapping = true;
			float d = dot(float4(lightWorldPos, 1.0), nearPlane);
			if (d < -lightCullRadius)
				bOverlapping = false;
			
			d = dot(float4(lightWorldPos, 1.0), farPlane);
			if (d < -lightCullRadius)
				bOverlapping = false;

			if (bOverlapping)
			{
				switch(lightData.type)
				{
				case 0:	// sphere
					{	
						uint slot = 0;
						InterlockedAdd(pointLightCount, 1, slot);
						pointLightIndices[slot] = lightIndex;
						
					}
					break;
				case 1: // cone
					{
						uint slot = 0;
						InterlockedAdd(spotLightCount, 1, slot);
						spotLightIndices[slot] = lightIndex;
					}
					break;
				}
			}
		}
		GroupMemoryBarrierWithGroupSync();

		if (gtindex == 0)
		{
			uint clusterIndex = zslice * TileCount + gid.y * TileCountX + gid.x;
			uint clusterOffset = GetClusterOffset(clusterIndex);

			// light count
			uint lightCount = ((pointLightCount & 0xff) << 0) | 
				((spotLightCount & 0xff) << 8);
			LightCluster.Store(clusterOffset + 0, lightCount);

			uint storeOffset = clusterOffset + 4;
			// sphere 
			for (uint i = 0; i < pointLightCount; ++i)
			{
				LightCluster.Store(storeOffset, pointLightIndices[i]);
				storeOffset += 4;
			}
			// cone
			for (uint i = 0; i < spotLightCount; ++i)
			{
				LightCluster.Store(storeOffset, spotLightIndices[i]);
				storeOffset += 4;
			}
			
			pointLightCount = 0;
			spotLightCount = 0;
		}
		GroupMemoryBarrierWithGroupSync();

	}
}

// http://www.aortiz.me/2018/12/21/CG.html#determining-active-clusters
