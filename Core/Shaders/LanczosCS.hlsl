//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author(s):  James Stanard
//             Jack Elliott
//

#include "LanczosFunctions.hlsli"
#include "PresentRS.hlsli"
#include "ShaderUtility.hlsli"

#ifndef ENABLE_FAST_PATH
	#define TILE_DIM_X 16
	#define TILE_DIM_Y 16
#endif
#define GROUP_COUNT (TILE_DIM_X * TILE_DIM_Y)

/**
 * 	the fast path can be enabled when the source tile plus the extra border pixels fit within
 * the destination tile size. For 16x16 destination tiles and 4 taps, you can upsample 13x13 tiles and
 * smaller using the fast path.	Src/Dst <= 13/15 --> FAST
 */
#ifdef ENABLE_FAST_PATH
	#define SAMPLE_X TILE_DIM_X
	#define SAMPLE_Y TILE_DIM_Y
#else
	#define SAMPLE_X (TILE_DIM_X + 3)
	#define SAMPLE_Y (TILE_DIM_Y + 3)
#endif
#define TOTAL_SAMPLES (SAMPLE_X * SAMPLE_Y)

cbuffer CBConstants : register(b0)
{
	float2 _RcpScale;
};

Texture2D<float3> _SrcTex	: register(t0);
RWTexture2D<float3> dstTex	: register(u0);


// de-interleaved to avoid LDS bank conflicts
groupshared float sh_R[TOTAL_SAMPLES];
groupshared float sh_G[TOTAL_SAMPLES];
groupshared float sh_B[TOTAL_SAMPLES];

// store pixel to LDS (local data store)
void StoreLDS(uint ldsIndex, float3 rgb)
{
	sh_R[ldsIndex] = rgb.r;
	sh_G[ldsIndex] = rgb.g;
	sh_B[ldsIndex] = rgb.b;
}

// load 4 pixel samples from LDS. Stride determines horizontal or vertical groups
float3x4 LoadSamples(uint index, uint stride)
{
	uint i0 = index, i1 = index + stride, i2 = index + 2 * stride, i3 = index + 3 * stride;
	return float3x4(
		sh_R[i0], sh_R[i1], sh_R[i2], sh_R[i3],
		sh_G[i0], sh_G[i1], sh_G[i2], sh_G[i3],
		sh_B[i0], sh_B[i1], sh_B[i2], sh_B[i3]
		);
}

[numthreads(TILE_DIM_X, TILE_DIM_Y, 1)]
void main( 
	uint3 dtid 	: SV_DispatchThreadID,
	uint3 gtid	: SV_GroupThreadID,
	uint3 gid 	: SV_GroupID,
	uint  gi 	: SV_GroupIndex)
{
	// number of samples needed from the source buffer to generate the output tile dimensions
	const uint2 sampleSpace = ceil(float2(TILE_DIM_X, TILE_DIM_Y) * _RcpScale + 3.0);

	// pre-load source pixels
	int2 upperLeft = floor((gid.xy * uint2(TILE_DIM_X, TILE_DIM_Y) + 0.5) * _RcpScale - 1.5);
#ifdef ENABLE_FAST_PATH
	// NOTE: if bandwidth is more of a factor than ALU, uncomment this condition
	// if (all(gtid.xy < sampleSpace))
		StoreLDS(gi, _SrcTex[upperLeft + gtid.xy]);
#else
	for (uint i = gi; i < TOTAL_SAMPLES; i += GROUP_COUNT)
		StoreLDS(i, _SrcTex[upperLeft + int2(i % SAMPLE_X, i / SAMPLE_Y)]);
#endif

	GroupMemoryBarrierWithGroupSync();

	// the coordinate of the top-left sample from the 4x4 kernel (offset by 00.5
	// so that whole numbers land on a pixel center). This is in source texture space
	float2 topLeftSample = (dtid.xy + 0.5) * _RcpScale - 1.5;

	// position of samples relative to pixels used to evaluate the Sinc function
	float2 phase = frac(topLeftSample);

	// LDS tile coordinate for the top-left sample (for this thread)
	uint2 tileST = int2(floor(topLeftSample)) - upperLeft;

	// convolution weights, one per sample (in each dimension)
	float4 xWeights = GetUpscaleFilterWeights(phase.x);
	float4 yWeights = GetUpscaleFilterWeights(phase.y);

	// horizontally convolve the first N rows
	uint readIndex = tileST.x + gtid.y * SAMPLE_X;
#ifdef ENABLE_FAST_PATH
	StoreLDS(gi, mul(LoadSamples(readIndex, 1), xWeights));
#else
	uint writeIndex = gtid.x + gtid.y * SAMPLE_X;
	StoreLDS(writeIndex, mul(LoadSamples(readIndex, 1), xWeights));

	// if the source tile plus border is larger than the desination tile, we have to 
	// convolve a few more rows
	if (gi + GROUP_COUNT < sampleSpace.y * TILE_DIM_X)
	{
		readIndex += TILE_DIM_Y * SAMPLE_X;
		writeIndex += TILE_DIM_Y * SAMPLE_X;
		StoreLDS(writeIndex, mul(LoadSamples(readIndex, 1), xWeights));
	}
#endif

	GroupMemoryBarrierWithGroupSync();

	// convolve vertically N columns
	readIndex = gtid.x + tileST.y * SAMPLE_X;
	float3 result = mul(LoadSamples(readIndex, SAMPLE_X), yWeights);

	// transform to display settings
	result = RemoveDisplayProfile(result, LDR_COLOR_FORMAT);
	dstTex[dtid.xy] = ApplyDisplayProfile(result, DISPLAY_PLANE_FORMAT);
}
