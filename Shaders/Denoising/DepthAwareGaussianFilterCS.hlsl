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


// Desc: Filters values via a depth aware separable gaussian filter and input blur strength input.
// The input pixels are interleaved such that kernel cells are at cb.step offsets
// and the results are scatter wrote to memory. The interleaved layout
// allows for a separable filtering via shared memory.
// The purpose of the filter is to apply a strong blur and thus the depth test
// is more relaxed than the one used in AtrousWaveletTransform filter.
// It still however does a relaxed depth test to prevent blending surfaces too far apart.
// Supports up to 9x9 kernels.
// Requirements:
//  - Wave lane size 16 or higher.
//  - WaveReadLaneAt() with any to any to wave read lane support.

#define HLSL
#define DEPTH_AWARE_GAUSSIAN_FILTER
#define GAUSSIAN_KERNEL_3X3
#define INFORMAT float
#define OUTFORMAT float

#define GROUP_SIZE 8

#if 0
#define KERNEL_SIZE 3
#define KERNEL_RADIUS (KERNEL_SIZE/2)
#else
#define KERNEL_SIZE FilterKernel::Width
#define KERNEL_RADIUS FilterKernel::Radius
#endif

#define EPS_SUM (1e-6)

#include "DenoisingCommon.hlsl"

// Group shared memory cache for the row aggregated results
static const uint s_NumValuesToLoadPerRowOrColumn = GROUP_SIZE + (KERNEL_SIZE - 1);

groupshared uint sh_PackedValueCache[s_NumValuesToLoadPerRowOrColumn][GROUP_SIZE];		// 16bit float value, depth
groupshared float sh_FilteredResultCache[s_NumValuesToLoadPerRowOrColumn][GROUP_SIZE];	// 32bit float filtered value

// Find a DTID with steps in between the group threads and groups interleaved to cover all pixels
uint2 GetPixelIndex(uint2 gid, uint2 gtid)
{
	const uint2 GroupDim = uint2(GROUP_SIZE, GROUP_SIZE);
	const uint2 Step = _DenoisingCommonParams.FilterStep;
	uint2 groupBase = (gid / Step) * GroupDim * Step + gid % Step;
	uint2 groupThreadOffset = gtid * Step;
	uint2 dtid = groupBase + groupThreadOffset;
	return dtid;	
}

// Load up to 16x16 pixels and filter them horizontally
// The output is cached in shared memory and contains NumRows x 8 results
void FilterHorizontally(uint2 gid, uint gtindex)
{
	const uint2 GroupDim = uint2(GROUP_SIZE, GROUP_SIZE);
	const int2 BufferSize = _DenoisingCommonParams.BufferSizeAndInvSize.xy;
	const uint Step = _DenoisingCommonParams.FilterStep;

	// Process the thread group as row-major 4x16, where each sub groups of 16 threads processes one row.
	// Each thread loads up to 4 values, with the sub groups loading rows interleaved.
	// Loads up to 4x16x4 = 256 input values
	const uint2 Gtid4x16_row0 = uint2(gtindex % 16, gtindex / 16);
	const int2 GroupKernelBasePixel = GetPixelIndex(gid, 0) - int(KERNEL_RADIUS * Step); // gid * GroupDim 
	const uint NumRowsToPerThread = 4;
	const uint Row_BaseWaveLaneIndex = (WaveGetLaneIndex() / 16) * 16;
	[unroll]
	for (uint i = 0; i < NumRowsToPerThread; ++i)
	{
		uint2 gtid4x16 = Gtid4x16_row0 + uint2(0, 4 * i);
		if (gtid4x16.y >= s_NumValuesToLoadPerRowOrColumn)
		{
			break;
		}

		// Load all the contributing columns for each row
		int2 pixel = GroupKernelBasePixel + gtid4x16 * Step;
		float value = s_InvalidValue;
		float depth = 0;

		// The lane is out of bounds of the GroupDim + kernel
		// but could be within bounds of the input texture
		// so don't read it from the texture
		// However, we need to keep it as an active lane for a below split sum
		if (gtid4x16.x < s_NumValuesToLoadPerRowOrColumn && IsWithinBounds(pixel, BufferSize))
		{
			value = _BlurStrength[pixel];
			depth = _DepthBuffer[pixel];
		}

		// Cache the kernel center values
		if (IsInRange(gtid4x16.x, KERNEL_RADIUS, KERNEL_RADIUS+ GroupDim.x-1))
		{
			sh_PackedValueCache[gtid4x16.y][gtid4x16.x - KERNEL_RADIUS] = Float2ToHalf(float2(value, depth));
		}

		// Filter the values for the first GroupDim columns
		{
			// Accumulate for the whole kernel width
			float weightedValueSum = 0;
			float weightSum = 0;
			float gaussianWeightedValueSum = 0;
			float gaussianWeightSum = 0;

			// Since a row uses 16 lanes, but we only need to calculate the aggregate for the first half (8) lanes,
			// split the kernel wide aggregation among the first 8 and the second 8 lanes, and then combine them

			// Get the lane index that has the first value for a kernel in this lane
			uint Row_KernelStartLaneIndex = (Row_BaseWaveLaneIndex + gtid4x16.x) -
				(gtid4x16.x < GroupDim.x ? 0 : GroupDim.x);

			// Get values for the kernel center
			uint kcLaneIndex = Row_BaseWaveLaneIndex + KERNEL_RADIUS;
			float kcValue = WaveReadLaneAt(value, kcLaneIndex);
			float kcDepth = WaveReadLaneAt(value, kcLaneIndex);

			// Initialize the first 8 lanes to the center cell contribution of the kernel
			// This covers the remainder of 1 in KernelWidth/2 used the loop below
			if (gtid4x16.x < GroupDim.x && kcValue != s_InvalidValue && kcDepth != 0)
			{
				float w_h = FilterKernel::Kernel1D[KERNEL_RADIUS];
				gaussianWeightedValueSum = w_h * kcValue;
				gaussianWeightSum = w_h;
				weightedValueSum = gaussianWeightedValueSum;
				weightSum = w_h;
			}

			// Second 8 lanes start just past the kernel center
			uint KernelCellIndexOffset = gtid4x16.x < GroupDim.x ? 0 : (KERNEL_RADIUS + 1); // skip over the already accumulated center cell of the kernel

			// For all columns in the kernel
			for (uint c = 0; c < KERNEL_RADIUS; ++c)
			{
				uint kernelCellIndex = KernelCellIndexOffset + c;

				uint laneToReadFrom = Row_KernelStartLaneIndex + kernelCellIndex;
				float cValue = WaveReadLaneAt(value, laneToReadFrom);
				float cDepth = WaveReadLaneAt(depth, laneToReadFrom);

				if (cValue != s_InvalidValue && cDepth != 0 && kcDepth != 0)
				{
					float w_h = FilterKernel::Kernel1D[kernelCellIndex];

					// Simple depth test with tolerance growing as the kernel radius increases
					// Goal is to prevent values too far apart to blend together, while having
					// the test being relaxed enough to get a strong blurring result.
					float depthThreshold = 0.05 + Step * 0.001f * abs(int(KERNEL_RADIUS) - c);
					float w_d = abs(kcDepth - cDepth) <= depthThreshold * kcDepth;
					float w = w_h * w_d;

					weightedValueSum += w * cValue;
					weightSum += w;
					gaussianWeightedValueSum += w_h * cValue;
					gaussianWeightSum += w_h;
				}
			}

			// Combine the sub-results
			uint laneToReadFrom = min(WaveGetLaneCount() - 1, Row_BaseWaveLaneIndex + gtid4x16.x + GroupDim.x);
			weightedValueSum += WaveReadLaneAt(weightedValueSum, laneToReadFrom);
			weightSum += WaveReadLaneAt(weightSum, laneToReadFrom);
			gaussianWeightedValueSum += WaveReadLaneAt(gaussianWeightedValueSum, laneToReadFrom);
			gaussianWeightSum += WaveReadLaneAt(gaussianWeightSum, laneToReadFrom);

			// Store only the valid results, i.e. first GroupDim columns
			if (gtid4x16.x < GroupDim.x)
			{
				float gaussianFilteredValue = gaussianWeightSum > EPS_SUM ? gaussianWeightedValueSum / gaussianWeightSum : s_InvalidValue;
				float filteredValue = weightSum > EPS_SUM ? weightedValueSum / weightSum : gaussianFilteredValue;

				sh_FilteredResultCache[gtid4x16.y][gtid4x16.x] = filteredValue;
			}
		}
	}
}

void FilterVertically(uint2 dtid, uint2 gtid, float blurStrength)
{
	const uint Step = _DenoisingCommonParams.FilterStep;

	// Kernel center value
	float2 kcValueDepth = HalfToFloat2(sh_PackedValueCache[gtid.y + KERNEL_RADIUS][gtid.x]);
	float kcValue = kcValueDepth.x;
	float kcDepth = kcValueDepth.y;

	float filteredValue = kcValue;
	if (blurStrength >= 0.01 && kcDepth != 0)
	{
		float weightedValueSum = 0;
		float weightSum = 0;
		float gaussianWeightedValueSum = 0;
		float gaussianWeightSum = 0;

		// For all rows in the kernel
		[unroll]
		for (uint r = 0; r < KERNEL_SIZE; ++r)
		{
			uint rowId = gtid.y + r;
			float2 rValueDepth = HalfToFloat2(sh_FilteredResultCache[rowId][gtid.x]);
			float rDepth = rValueDepth.y;
			float rFileredValue = sh_FilteredResultCache[rowId][gtid.x];

			if (rFileredValue != s_InvalidValue && rDepth != 0)
			{
				float w_h = FilterKernel::Kernel1D[r];

				// Simple depth test with tolerance growing as the kernel radius increases.
                // Goal is to prevent values too far apart to blend together, while having 
                // the test being relaxed enough to get a strong blurring result.
				float depthThreshold = 0.05 + Step * 0.001f * abs(int(KERNEL_RADIUS) - int(r));
				float w_d = abs(kcDepth - rDepth) <= depthThreshold * kcDepth;
				float w = w_h * w_d;

				weightedValueSum += w * rFileredValue;
				weightSum += w;
				gaussianWeightedValueSum += w_h * rFileredValue;
				gaussianWeightSum += w_h;
			}			
		}

		float gaussianFilteredValue = gaussianWeightSum > EPS_SUM ? gaussianWeightedValueSum / gaussianWeightSum : s_InvalidValue;
		filteredValue = weightSum > EPS_SUM ? weightedValueSum / weightSum : gaussianFilteredValue;
		filteredValue = filteredValue != s_InvalidValue ? lerp(kcValue, filteredValue, blurStrength) : filteredValue;
	}

	Output[dtid.xy] = filteredValue;
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(uint2 gid : SV_GroupID, uint2 gtid : SV_GroupThreadID, uint gtindex: SV_GroupIndex)
{
	uint2 sDTid = GetPixelIndex(gid, gtid);
	// Pass through if all pixels have 0 blur strength set
	float blurStrength;
	{
		if (gtindex == 0)
			sh_FilteredResultCache[0][0] = 0;
		GroupMemoryBarrierWithGroupSync();

		blurStrength = 0.5f;
		// _BlurStrength[sDTid.xy]

		const float MinBlurStrength = 0.1f;
		bool bNeedsFiltering = blurStrength >= MinBlurStrength;
		if (bNeedsFiltering)
			sh_FilteredResultCache[0][0] = 1;

		GroupMemoryBarrierWithGroupSync();

		if (sh_FilteredResultCache[0][0] == 0)
			return;
	}

	FilterHorizontally(gid, gtindex);

	GroupMemoryBarrierWithGroupSync();

	FilterVertically(sDTid, gtid, blurStrength);
}
