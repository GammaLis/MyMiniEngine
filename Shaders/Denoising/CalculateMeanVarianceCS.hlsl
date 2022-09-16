// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

// Desc: Calculate Local Mean and Variance via a separable kernel and using wave intrinsics.
// Requirements:
//  - Wave lane size 16 or higher.
//  - WaveReadLaneAt() with any to any to wave read lane support.
// Supports:
//  - up to 9x9 kernels.
//  - checkerboard ON/OFF input. If enabled, outputs only for active pixels.
//     Active pixel is a pixel on the checkerboard pattern and has a valid / 
//     generated value for it. The kernel is stretched in y direction 
//    to sample only from active pixels. 
// Performance:
// - 4K, 2080Ti, 9x9 kernel: 0.37ms (separable) -> 0.305 ms (separable + wave intrinsics)

#define HLSL
#define CALCULATE_MEAN_VARIANCE
#define OUTFORMAT float2

#define MULTI_COMPONENTS 0

#define GROUP_SIZE 8
#define KERNEL_SIZE 9
#define KERNEL_RADIUS (KERNEL_SIZE/2)

#include "DenoisingCommon.hlsl"

// Group shared memory cache for the for aggregated results
#if !MULTI_COMPONENTS
groupshared uint sh_PackedRowResultCache[16][8]; // 16bit float valueSum, squaredValueSum
#else
groupshared uint3 sh_PackedRowResultCache[16][8]; // 16bit float valueSum, squaredValueSum
#endif
groupshared uint sh_NumValuesCache[16][8];

// Adjust an index to a pixel that had a valid value generated for it
// Inactive pixel indices get increased by 1 in the y direction
int2 GetActivePixelIndex(int2 pixel)
{
	bool isEvenPixel = ((pixel.x + pixel.y) & 1) == 0;
	// TODO...
	return pixel;
}

// Load up to 16x16 pixels and filter them horizontally
// The output is cached in shared memory and contains NumRows x 8 results
void FilterHorizontally(uint2 gid, uint gtindex)
{
	const uint2 GroupDim = uint2(GROUP_SIZE, GROUP_SIZE);
	const uint  NumValuesToLoadPerRowOrColum = GroupDim.x + (KERNEL_SIZE-1); // 8 + (9-1) = 16
	const int2  BufferSize = (int2)_DenoisingCommonParams.BufferSizeAndInvSize.xy;

	// Process the thread group as row-major 4x16, where each sub group of 16 threads processes one row.
	// Each thread loads up to 4 values, with the sub groups loading rows interleaved.
	// Loads up to 4x16x4 = 256 input values
	uint2 gtid4x16_row0 = uint2(gtindex % 16, gtindex / 16);
	const uint2 KernelBasePixel = (gid * GroupDim - KERNEL_RADIUS);
	const uint NumRowsToLoadPerThread = 4;
	const uint Row_BaseWaveLaneIndex = (WaveGetLaneIndex() / 16) * 16;

	[unroll]
	for (uint i = 0; i < NumRowsToLoadPerThread; ++i)
	{
		uint2 gtid4x16 = gtid4x16_row0 + uint2(0, i*4);
		if (gtid4x16.y >= NumValuesToLoadPerRowOrColum)
		{
			if (gtid4x16.x < GroupDim.x)
			{
				sh_NumValuesCache[gtid4x16.y][gtid4x16.x] = 0;
			}
			break;
		}

		// Load all the contributing columns for each row
		int2 pixel = GetActivePixelIndex(KernelBasePixel + gtid4x16);
#if !MULTI_COMPONENTS
		INFORMAT value = s_InvalidValue;
#else
		INFORMAT value = s_InvalidColor;
#endif

		// The lane if out of bounds of the GroupDim + kernel
		// but could be within bounds of the input texture,
		// so don't read it from the texture
		// However, we need to keep it as an active lane for a below split sum
		if (gtid4x16.x < NumValuesToLoadPerRowOrColum && IsWithinBounds(pixel, BufferSize))
		{
#if !MULTI_COMPONENTS
			float3 color = _ColorBuffer[pixel].rgb;
			value = Luminance(color);
#else
			value = _ColorBuffer[pixel].rgb;
#endif
		}

		// Filter the values for the first GroupDim columns
		{
			// Accumulate for the whole kernel width
			INFORMAT valueSum  = 0;
			INFORMAT valueSum2 = 0;
			uint numValues = 0;

			// Since a row uses 16 lanes, but we only need to calculate the aggregate for the first half (8) lanes,
			// split the kernel wide aggregation among the first 8 and the second 8 lanes, and then combine them
			
			// Initialize the first 8 lanes to the first cell contribution of the kernel
			// This covers the remainder of 1 in KERNl_WIDTH/2 used in the loop below
			if (gtid4x16.x < GroupDim.x && value != s_InvalidValue)
			{
				valueSum  = value;
				valueSum2 = value * value;
				++numValues;
			}

			// Get the lane index that has the first value for a kernel in this lane
			uint row_KernelStartLaneIndex = 
				Row_BaseWaveLaneIndex 
				+ 1 	// skip over the already accumulated first cell of the kernel
				+ (gtid4x16.x < GroupDim.x ? gtid4x16.x : (gtid4x16.x - GroupDim.x) + KERNEL_RADIUS);

			for (uint c = 0; c < KERNEL_RADIUS; ++c)
			{
				uint laneToReadFrom = row_KernelStartLaneIndex + c;
				INFORMAT cValue = WaveReadLaneAt(value, laneToReadFrom);
				if (cValue != s_InvalidValue)
				{
					valueSum  += cValue;
					valueSum2 += cValue * cValue;
					++numValues;
				}
			}

			// Combine the sub-results
			uint laneToReadFrom = min(WaveGetLaneCount() - 1, Row_BaseWaveLaneIndex + gtid4x16.x + GroupDim.x);
			valueSum  += WaveReadLaneAt(valueSum , laneToReadFrom);
			valueSum2 += WaveReadLaneAt(valueSum2, laneToReadFrom);
			numValues += WaveReadLaneAt(numValues, laneToReadFrom);

			// Store only the value results, i.e. first GroupDim columns
			if (gtid4x16.x < GroupDim.x)
			{
#if !MULTI_COMPONENTS
				sh_PackedRowResultCache[gtid4x16.y][gtid4x16.x] = Float2ToHalf(float2(valueSum, valueSum2));
#else
				sh_PackedRowResultCache[gtid4x16.y][gtid4x16.x] = uint3(
					Float2ToHalf(float2(valueSum.x, valueSum2.x)),
					Float2ToHalf(float2(valueSum.y, valueSum2.y)),
					Float2ToHalf(float2(valueSum.z, valueSum2.z)));
#endif
				sh_NumValuesCache[gtid4x16.y][gtid4x16.x] = numValues;
			}
		}
	}
}

void FilterVertically(uint2 dtid, uint2 gtid)
{
	INFORMAT valueSum  = 0;
	INFORMAT valueSum2 = 0;
	uint numValues = 0;

	uint2 pixel = GetActivePixelIndex(dtid.xy);

	// Accumulate for the whole kernel
	for (uint r = 0; r < KERNEL_SIZE; ++r)
	{
		uint rowId = gtid.y + r;
		uint rNumValues = sh_NumValuesCache[rowId][gtid.x];
		if (rNumValues > 0)
		{
#if !MULTI_COMPONENTS
			float2 unpackedRowSum = HalfToFloat2(sh_PackedRowResultCache[rowId][gtid.x]);
#else
			uint3 packedRowSum = sh_PackedRowResultCache[rowId][gtid.x];
			float2 unpackX = HalfToFloat2(packedRowSum.x);
			float2 unpackY = HalfToFloat2(packedRowSum.y);
			float2 unpackZ = HalfToFloat2(packedRowSum.z);
			float3 unpackedRowSum[2] =
			{
				{ unpackX.x, unpackY.x, unpackZ.x },
				{ unpackX.y, unpackY.y, unpackZ.y }
			};
#endif
			valueSum  += unpackedRowSum[0];
			valueSum2 += unpackedRowSum[1];
			numValues += rNumValues;
		}
	}

	// Calculate mean and variance
	float invN = 1.f / max(numValues, 1);
	INFORMAT mean = valueSum * invN;

	// Apply Bessel's correction to the estimated variance, multiply by N/N-1,
	// since the true population mean is not known; it is only estimated as the sample mean.
	float besselCorrection = numValues / float(max(numValues, 2) - 1);
	INFORMAT variance = besselCorrection * (valueSum2 * invN - mean * mean);
	variance = max(0, variance); // ensure variance doesn't go negative due to imprecision

#if !MULTI_COMPONENTS
	Output[pixel] = numValues > 0 ? float2(mean, variance) : s_InvalidValue;
#else
	// TODO...
#endif
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(uint2 dtid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID, uint2 gid : SV_GroupID, uint gtindex : SV_GroupIndex)
{
	FilterHorizontally(gid, gtindex);
	
	GroupMemoryBarrierWithGroupSync();

	FilterVertically(dtid, gtid);
}
