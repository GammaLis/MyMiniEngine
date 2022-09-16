//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

// Stage 1 of Temporal Supersampling. Samples temporal cache via motion vectors/reverse reprojection.
// If no valid values have been retrieved from the cache, the tspp is set to 0.

#define HLSL
#define TEMPORAL_REPROJECTION

#define GROUP_SIZE 8

#include "DenoisingCommon.hlsl"

float4 GetBilinearWeights(float2 samplePos)
{
	float2 weights = samplePos - floor(samplePos);
	float4 sampleWeights = float4(
		(1.0 - weights.x) * (1.0 - weights.y),
		weights.x * (1.0 - weights.y),
		(1.0 - weights.x) * weights.y,
		weights.x * weights.y
	);
	return sampleWeights;
}

float4 GetSampleWeights(float2 samplePos, float depth, float3 normal ,int2 sampleIndices[4], float4 sampleDepths, float3 sampleNormals[4])
{
	const int2 BufferSize = (int2) _DenoisingCommonParams.BufferSizeAndInvSize.xy;
	bool4 isWithinBounds = bool4(
		IsWithinBounds(sampleIndices[0], BufferSize),
		IsWithinBounds(sampleIndices[1], BufferSize),
		IsWithinBounds(sampleIndices[2], BufferSize),
		IsWithinBounds(sampleIndices[3], BufferSize));

	float4 depthWeights = float4(
		GetEdgeStoppingDepthWeight(depth, sampleDepths[0]),
		GetEdgeStoppingDepthWeight(depth, sampleDepths[1]),
		GetEdgeStoppingDepthWeight(depth, sampleDepths[2]),
		GetEdgeStoppingDepthWeight(depth, sampleDepths[3]));

	float4 normalWeights = float4(
		GetEdgeStoppingNormalWeight(normal, sampleNormals[0]),
		GetEdgeStoppingNormalWeight(normal, sampleNormals[1]),
		GetEdgeStoppingNormalWeight(normal, sampleNormals[2]),
		GetEdgeStoppingNormalWeight(normal, sampleNormals[3]));

	float4 bilinearWeights = GetBilinearWeights(samplePos - 0.5f);

	float4 sampleWeights = depthWeights * normalWeights 
	#if 1
		* bilinearWeights
	#endif
		;
	sampleWeights = isWithinBounds ? sampleWeights : 0;

	return sampleWeights;
}


[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(uint2 dtid : SV_DispatchThreadID)
{
	float2 texPos = dtid + 0.5f;

	float deviceDepth = _DepthBuffer[dtid].x;
	float3 normal = _NormalBuffer[dtid].xyz;

	if (deviceDepth == 0.0)
	{
		RWReprojectedValue[dtid] = 0;
		RWTspp[dtid] = 0;
		return;
	}
#if 0
	// TODO: There is some error in this func ???
	float sceneDepth = ConvertFromDeviceZ(deviceDepth);
#else
	float sceneDepth = LinearEyeDepth(deviceDepth);
#endif

	const int2 PosOffsets[] =
	{
		{0, 0}, {1, 0}, {0, 1}, {1, 1}
	};

	float3 velocity = UnpackVelocity(_VelocityBuffer[dtid]);
	float2 prevTexPos = texPos + velocity.xy;
	// Find the nearest integer index smaller than the texture position
	// The floor() ensures that the value sign is taken into consideration
	int2 prevTopLeftPos = floor(prevTexPos - 0.5f);
	float2 adjustedPrevUV = (prevTopLeftPos + 0.5f) * _DenoisingCommonParams.BufferSizeAndInvSize.zw;

	int2 loadIndices[] = 
	{
		prevTopLeftPos + PosOffsets[0],
		prevTopLeftPos + PosOffsets[1],
		prevTopLeftPos + PosOffsets[2],
		prevTopLeftPos + PosOffsets[3],
	};
	float4 prevDepths;
	float3 prevNormals[4];
	{
		prevDepths = _DepthHistory.GatherRed(s_PointClampSampler, adjustedPrevUV).wzxy;
		[unroll]
		for (int i = 0; i < 4; ++i)
		{
			// prevDepths[i] = _DepthHistory.Load(int3(loadIndices[i], 0)).x;
			prevNormals[i] = _NormalHistory.Load(int3(loadIndices[i], 0)).xyz; // index may out of range, not _Tex[]
		}
	}

#if 1
	
#if 0
	prevDepths = float4(
		ConvertFromDeviceZ(prevDepths[0]), ConvertFromDeviceZ(prevDepths[1]),
		ConvertFromDeviceZ(prevDepths[2]), ConvertFromDeviceZ(prevDepths[3]));
#else
	prevDepths = float4(
		LinearEyeDepth(prevDepths[0]), LinearEyeDepth(prevDepths[1]),
		LinearEyeDepth(prevDepths[2]), LinearEyeDepth(prevDepths[3]));
#endif
	
#endif

	// float4 GetSampleWeights(float2 samplePos, float depth, float3 normal ,int2 sampleIndices[4], float4 sampleDepths, float3 sampleNormals[4])
	float4 weights = GetSampleWeights(prevTexPos, sceneDepth, normal, loadIndices, prevDepths, prevNormals);

	// Invalidate weights for invalid values in the cache
	float3 vCacheValues[4];
	[unroll]
	for (int i = 0; i < 4; ++i)
	{
		vCacheValues[i] = _ColorHistory.Load(int3(loadIndices[i], 0)).xyz; // index may out of range
	}
	float4 luma = { Luminance(vCacheValues[0]), Luminance(vCacheValues[1]), Luminance(vCacheValues[2]), Luminance(vCacheValues[3]) };

	weights = luma != 0 ? weights : 0;
	float weightSum = dot(1, weights);

	float3 cachedValue = s_InvalidColor;
	float  cachedValueSquaredMean = 0;

	uint tspp = 0;
	bool bCacheValuesValid = weightSum > 1e-3f;
	if (bCacheValuesValid)
	{
		uint4 vCachedTspp = _TsppHistory.GatherRed(s_PointClampSampler, adjustedPrevUV).wzxy;
		// Enforce tspp of at least 1 for reprojection for valid values
		// This is because the denoiser will fill in invalid values with filtered 
		// ones if it can. But it doesn't increase tspp.
		vCachedTspp = max(1, vCachedTspp);


		float4 nWeights = weights / weightSum; // normalize the weights

		// Scale the tspp by the total weight. This is to keep the tspp low for 
		// total contributions that have very low reprojection weight. While it's preferred to 
		// get a weighted value even for reprojections that have low weights but still
		// satisfy consistency tests, the tspp needs to be kept small so that the Target calculdated values
		// are quickly filled in over a few frames. Otherwise, bad estimates from reprojections, 
		// such as on disocclusions of surfaces on rotation, are kept around long enough to create
		// visible streaks that fade away very slow.
		// Example: rotating camera around dragon's nose up close.
		float tsppScale = 1.0f;

		float cachedTspp = tsppScale * dot(nWeights, vCachedTspp);
		tspp = round(cachedTspp);

		if (tspp > 0)
		{
			cachedValue = nWeights[0] * vCacheValues[0] + 
				nWeights[1] * vCacheValues[1] + 
				nWeights[2] * vCacheValues[2] + 
				nWeights[3] * vCacheValues[3];

			float4 vCachedValueSquaredMean = _LumaMomentsHistory.GatherGreen(s_PointClampSampler, adjustedPrevUV).wzxy;
			cachedValueSquaredMean = dot(nWeights, vCachedValueSquaredMean);
		}
	}
	else
	{
		// No valid values can be retrieved from the cache
		// TODO: try a greater cache footprint to find useful samples,
		//	For example a 3x3 pixel cache footprint or use lowe mip cache input.
		tspp = 0;
	}

	// uint packed = f32tof16(cachedValueSquaredMean) | ((tspp & 0xFFFF) << 16);
	RWReprojectedValue[dtid] = uint4(f32tof16(cachedValue), f32tof16(cachedValueSquaredMean));
	RWTspp[dtid] = tspp;
}
