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

// Atrous Wavelet Transform Cross Bilateral Filter.
// Based on a 1st pass of [SVGF] filter.
// Ref: [Dammertz2010], Edge-Avoiding A-Trous Wavelet Transform for Fast Global Illumination Filtering
// Ref: [SVGF], Spatiotemporal Variance-Guided Filtering
// Ref: [RTGCH19] Ray Tracing Gems (Ch 19)

#define HLSL
#define GAUSSIAN_KERNEL_3X3
#define OUTFORMAT float3

#define GROUP_SIZE 8

#define KERNEL_SIZE FilterKernel::Width
#define KERNEL_RADIUS FilterKernel::Radius

#include "DenoisingCommon.hlsl"

static const float s_ValueSigma = 1.0f;

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(uint2 dtid : SV_DispatchThreadID, uint2 gid : SV_GroupID)
{
	const int2 BufferSize = _DenoisingCommonParams.BufferSizeAndInvSize.xy;
	const float2 InvBufferSize = _DenoisingCommonParams.BufferSizeAndInvSize.zw;

	if (!IsWithinBounds(int2(dtid), BufferSize))
		return;

	// Initial values to the current pixel / center filter kernel value
	float3 value = _ColorBuffer[dtid].rgb;
	float3 normal = _NormalBuffer[dtid].xyz;
	float  depth = _DepthBuffer[dtid].x;
	float  luma = Luminance(value);

	bool isValidValue = all(value != 0);
	// Always valid ?
	isValidValue = true;
	float3 filteredValue = value;
	float variance = _Variance[dtid];
	if (depth != 0)
	{
		float sceneDepth = LinearEyeDepth(depth);
		
		float3 weightedValueSum = 0;
		float weightSum = 0;
		float stdDeviation = 1;
		if (isValidValue)
		{
			float w = FilterKernel::Kernel[KERNEL_RADIUS][KERNEL_RADIUS];
			weightSum += w;
			weightedValueSum = w * value;
			stdDeviation = sqrt(variance);
		}

		// Adaptive kernel size
		// Scale the kernel span based on AO ray hit distance
		// This helps filter out lower frequency noise, a.k.a. boiling artifacts
		// Ref: [RTGCH19]
		uint2 kernelStep = 1;
	#if 0
		if (isValidValue)
		{
			float avgRayHitDistance = 0;

			float perPixelViewAngle = (fovY * InvBufferSize.y) * PI / 180.0;
			float tan_a = tan(perPixelViewAngle);
			float2 projectedSurfaceDim = ApproximateProjectedSurfaceDimensionsPerPixel(depth, ddxy, tan_a);

			// TODO...
		}
	#endif

		if (variance >= _DenoisingCommonParams.MinVarianceToDenoise)
		{
			// Add contributions from the neighborhood
			[unroll]
			for (int r = 0; r < KERNEL_SIZE; ++r)
			{
				[unroll]
				for (int c = 0; c < KERNEL_SIZE; ++c)
				{
					if (r != KERNEL_RADIUS && c != KERNEL_RADIUS)
					{
						int2 pixelOffset;
						float kernelWidth;
						float varianceScale = 1;

						pixelOffset = int2(r - KERNEL_RADIUS, c - KERNEL_RADIUS) * kernelStep;
						int2 sampleIndex = int2(dtid) + pixelOffset;
						if (IsWithinBounds(sampleIndex, BufferSize))
						{
							float3 sampleValue = _ColorBuffer[sampleIndex].rgb;
							float3 sampleNormal = _NormalBuffer[sampleIndex].xyz;
							float sampleDepth = _DepthBuffer[sampleIndex].x;
							float sampleLuma = Luminance(sampleValue);
							bool  isValidSample = all(sampleValue != s_InvalidColor);
							// TODO: Always valid ?
							isValidSample = true;
							if (!isValidSample || sampleDepth == 0)
								continue;

						#if 1
							sampleDepth = LinearEyeDepth(sampleDepth);
						#endif
							// Calculate a weight for the neighbor's contribution
							// Ref: [SVGF]
							float w;
							{
								// Value based weight
								// Lower value tolerance for the neighbors further apart. Prevents overbluring sharp value transitions.
								// Ref: [Dammertz2010]
								const float ErrorOffset = 0.005f;
								float valueSigmaDistCoef = 1.0f / length(pixelOffset);
								float e_x = -abs(sampleLuma - luma) / (valueSigmaDistCoef * s_ValueSigma * stdDeviation + ErrorOffset);
								float w_x = exp(e_x);

								// Normal based weight
								float w_n = pow(max(0, dot(normal, sampleNormal)), s_NormalSigma);

								// Depth based weight
								float w_d;
								{
									float2 pixelOffsetForDepth = pixelOffset;

									// Acocunt for sample offset in bilateral downsampled partial depth 
									// ...
									
									w_d = GetEdgeStoppingDepthWeight(sceneDepth, sampleDepth);
									
									// Scale down contributions for samples beyond tolerance, but completely disable contribution for samples too far away
									w_d *= w_d >= _DenoisingCommonParams.DepthWeightCutoff;
								}

								// Fitler kernel weight
								float w_h = FilterKernel::Kernel[r][c];

								// Final weight
								w = w_h * w_n * w_x * w_d;
							}

							weightedValueSum += w * sampleValue;
							weightSum += w;
						}
					}
				}
			}
		}

		float smallValue = 1e-6f;
		if (weightSum > smallValue)
		{
			filteredValue = weightedValueSum / weightSum;
		}
		else 
		{
			filteredValue = s_InvalidColor;
		}
	}

	Output[dtid] = filteredValue;
}
