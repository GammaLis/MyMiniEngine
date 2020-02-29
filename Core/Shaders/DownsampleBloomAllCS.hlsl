// The CS for downsampling 16x16 blocks of pixels down to 8x8, 4x4, 2x2, and 1x1 blocks.
#include "PostEffectsRS.hlsli"

cbuffer CSConstants	: register(b0)
{
	float2 _InverseDimensions;
};

Texture2D<float3> _BloomBuffer 	: register(t0);

RWTexture2D<float3> downsampleRes1	: register(u0);
RWTexture2D<float3> downsampleRes2	: register(u1);
RWTexture2D<float3> downsampleRes3	: register(u2);
RWTexture2D<float3> downsampleRes4	: register(u3);

SamplerState s_LinearClampSampler	: register(s0);

groupshared float3 sh_Tile[64];		// 8x8 input pixels

[numthreads(8, 8, 1)]
void main( uint3 dispatchThreadId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex )
{
	// You can tell if both x and y are divisible by a power of two with this value
    uint parity = dispatchThreadId.x | dispatchThreadId.y;

    // downsample and store the 8x8 block
    float2 centerUV = (float2(dispatchThreadId.xy) * 2.0 + 1.0) * _InverseDimensions;
    float3 avgPixel = _BloomBuffer.SampleLevel(s_LinearClampSampler, centerUV, 0);
    // 1/2
    sh_Tile[groupIndex] = avgPixel;
    downsampleRes1[dispatchThreadId.xy] = avgPixel;

    GroupMemoryBarrierWithGroupSync();

    // downsample and store the 4x4 block
    // 1/4
    if ((parity & 1) == 0)
    {
        // 采用局部变量效率更高
    	// avgPixel = (sh_Tile[groupIndex] + sh_Tile[groupIndex + 1] +
    	// 	sh_Tile[groupIndex + 8] + sh_Tile[groupIndex + 9]) * 0.25;
        avgPixel = (avgPixel + sh_Tile[groupIndex + 1] +
            sh_Tile[groupIndex + 8] + sh_Tile[groupIndex + 9]) * 0.25;
    	sh_Tile[groupIndex] = avgPixel;
    	downsampleRes2[dispatchThreadId.xy >> 1] = avgPixel;
    }

    GroupMemoryBarrierWithGroupSync();

    // downsample and store the 2x2 block
    // 1/8
    if ((parity & 3) == 0)
	{
		// avgPixel = (sh_Tile[groupIndex] + sh_Tile[groupIndex + 2] + 
		// 	sh_Tile[groupIndex + 16] + sh_Tile[groupIndex + 18]) * 0.25;
        avgPixel = (avgPixel + sh_Tile[groupIndex + 2] + 
            sh_Tile[groupIndex + 16] + sh_Tile[groupIndex + 18]) * 0.25;
		sh_Tile[groupIndex] = avgPixel;
		downsampleRes3[dispatchThreadId.xy >> 2] = avgPixel;
	}

	GroupMemoryBarrierWithGroupSync();

	// downsample and store the 1x1 block
	// 1/16
	if ((parity & 7) == 0)
	{
		// avgPixel = (sh_Tile[groupIndex] + sh_Tile[groupIndex + 4] +
		// 	sh_Tile[groupIndex + 32] + sh_Tile[groupIndex + 36]) * 0.25;
        avgPixel = (avgPixel + sh_Tile[groupIndex + 4] +
            sh_Tile[groupIndex + 32] + sh_Tile[groupIndex + 36]) * 0.25;
		downsampleRes4[dispatchThreadId.xy >> 3] = avgPixel;
	}
}
