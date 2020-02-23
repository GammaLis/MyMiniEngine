// The CS for downsampling 16x16 blocks of pixels down to 4x4 and 1x1 blocks.
#include "PostEffectsRS.hlsli"

cbuffer CSConstants	: register(b0)
{
	float2 _InverseDimensions;
};

Texture2D<float3> _BloomBuffer 	: register(t0);

RWTexture2D<float3> downsampleRes1	: register(u0);
RWTexture2D<float3> downsampleRes2	: register(u1);

SamplerState s_LinearClampSampler	: register(s0);

groupshared float3 sh_Tile[64];		// 8x8 input pixels

// 1/4, 1/16 size
[RootSignature(PostEffects_RootSig)]
[numthreads(8, 8, 1)]
void main( uint3 dispatchThreadId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex )
{
	// you can tell if both x and y are divisible by a power of 2 with this value
	uint parity = dispatchThreadId.x | dispatchThreadId.y;

	// store the first downsampled quad per thread
	float2 centerUV = (dispatchThreadId.xy * 2.0 + 1.0) * _InverseDimensions.xy;
	float3 avgPixel = _BloomBuffer.SampleLevel(s_LinearClampSampler, centerUV, 0);
	sh_Tile[groupIndex] = avgPixel;

	GroupMemoryBarrierWithGroupSync();

	// 4个一组 
	// (0, 0) - (2, 0) ...
	// (2, 0) - (2, 2) ...
	if ((parity & 1) == 0)
	{
		avgPixel = (sh_Tile[groupIndex] + sh_Tile[groupIndex + 1] + 
			sh_Tile[groupIndex + 8] + sh_Tile[groupIndex + 9]) * 0.25;
		sh_Tile[groupIndex] = avgPixel;
		downsampleRes1[dispatchThreadId.xy >> 1] = avgPixel;
	}

	GroupMemoryBarrierWithGroupSync();

	if ((parity & 3) == 0)
	{
		avgPixel = (sh_Tile[groupIndex] + sh_Tile[groupIndex + 2] + 
			sh_Tile[groupIndex + 16] + sh_Tile[groupIndex + 18]) * 0.25;
		sh_Tile[groupIndex] = avgPixel; 
	}

	GroupMemoryBarrierWithGroupSync();

	if ((parity & 7) == 0)
	{
		avgPixel = (sh_Tile[groupIndex] + sh_Tile[groupIndex + 4] +
			sh_Tile[groupIndex + 32] + sh_Tile[groupIndex + 36]) * 0.25;
		downsampleRes2[dispatchThreadId.xy >> 3] = avgPixel;
	}
}
