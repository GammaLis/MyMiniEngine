// The CS for combining a lower resolution bloom buffer with a higher resolution buffer
// (via bilinear upsampling) and then guassian blurring the resultant buffer.
//
// For the intended bloom blurring algorithm, it is expected that this shader will be
// used repeatedly to upsample and blur successively higher resolutions until the final
// bloom buffer is the destination.
#include "PostEffectsRS.hlsli"

cbuffer CSConstants	: register(b0)
{
	float2 _InverseDimensions;
	float _UpsampleBlendFactor;
};

Texture2D<float3> _HigherResBuf	: register(t0);
Texture2D<float3> _LowerResBuf	: register(t1);

RWTexture2D<float3> blurRes		: register(u0);

SamplerState s_LinearBorderSamper	: register(s1);

// the gaussian blur weights (derived from Pascal's triangle - 杨辉三角)
static const float Weights5[3] = {6.0 / 16.0, 4.0 / 16.0, 1.0 / 16.0};
static const float Weights7[4] = {20.0 / 64.0, 15.0 / 64.0, 6.0 / 16.0, 1.0 / 64.0};
static const float Weights9[5] = {70.0 / 256.0, 56.0 / 256.0, 28.0 / 256.0, 8.0 / 256.0, 1.0 / 256.0};

float3 Blur5(float3 a, float3 b, float3 c, float3 d, float3 e, float3 f, float3 g, float3 h, float3 i)
{
	return Weights5[0] * e + Weights5[1] * (d + f) + Weights5[2] * (c + g);
}

float3 Blur7(float3 a, float3 b, float3 c, float3 d, float3 e, float3 f, float3 g, float3 h, float3 i)
{
	return Weights7[0] * e + Weights7[1] * (d + f) + Weights7[2] * (c + g) + Weights7[3] * (b + h);
}

float3 Blur9(float3 a, float3 b, float3 c, float3 d, float3 e, float3 f, float3 g, float3 h, float3 i)
{
	return Weights9[0] * e + Weights9[1] * (d + f) + Weights9[2] * (c + g) + Weights9[3] * (b + h) + Weights9[4] * (a + i);
}

#define BlurPixels Blur9

// 16x16 pixels with an 8x8 center that we will be blurring writing out. Each uint is 2 color channels packed together
groupshared uint sh_R[128];
groupshared uint sh_G[128];
groupshared uint sh_B[128];

void Store2Pixels(uint index, float3 pixel1, float3 pixel2)
{
	sh_R[index] = f32tof16(pixel1.r) | f32tof16(pixel2.r) << 16;
	sh_G[index] = f32tof16(pixel1.g) | f32tof16(pixel2.g) << 16;
	sh_B[index] = f32tof16(pixel1.b) | f32tof16(pixel2.b) << 16;
}

void Load2Pixels(uint index, out float3 pixel1, out float3 pixel2)
{
	// uint rr = sh_R[index];
	// uint gg = sh_G[index];
	// uint bb = sh_B[index];
	// pixel1 = float3(f16tof32(rr), f16tof32(gg), f16tof32(bb));
	// pixel2 = float3(f16tof32(rr >> 16), f16tof32(gg >> 16), f16tof32(bb >> 16));

	uint3 rgb = uint3(sh_R[index], sh_G[index], sh_B[index]);
	pixel1 = f16tof32(rgb);
	pixel2 = f16tof32(rgb >> 16);
}

void Store1Pixel(uint index, float3 pixel)
{
	sh_R[index] = asuint(pixel.r);
	sh_G[index] = asuint(pixel.g);
	sh_B[index] = asuint(pixel.b);
}

void Load1Pixel(uint index, out float3 pixel)
{
	pixel = asfloat(uint3(sh_R[index], sh_G[index], sh_B[index]));
}

// blur 2 pixels horizontally. This reduces LDS reads and pixel unpacking
void BlurHorizontally(uint outIndex, uint leftMostIndex)
{
	float3 s0, s1, s2, s3, s4, s5, s6, s7, s8, s9;
	Load2Pixels(leftMostIndex + 0, s0, s1);
	Load2Pixels(leftMostIndex + 1, s2, s3);
	Load2Pixels(leftMostIndex + 2, s4, s5);
	Load2Pixels(leftMostIndex + 3, s6, s7);
	Load2Pixels(leftMostIndex + 4, s8, s9);

	Store1Pixel(outIndex, 	BlurPixels(s0, s1, s2, s3, s4, s5, s6, s7, s8));
    Store1Pixel(outIndex+1, BlurPixels(s1, s2, s3, s4, s5, s6, s7, s8, s9));
}

void BlurVertically(uint2 pixelCoord, uint topMostIndex)
{
	float3 s0, s1, s2, s3, s4, s5, s6, s7, s8;
	Load1Pixel(topMostIndex		, s0);
	Load1Pixel(topMostIndex +  8, s1);
	Load1Pixel(topMostIndex + 16, s2);
	Load1Pixel(topMostIndex + 24, s3);
	Load1Pixel(topMostIndex + 32, s4);
	Load1Pixel(topMostIndex + 40, s5);
	Load1Pixel(topMostIndex + 48, s6);
	Load1Pixel(topMostIndex + 56, s7);
	Load1Pixel(topMostIndex + 64, s8);

	blurRes[pixelCoord] = BlurPixels(s0, s1, s2, s3, s4, s5, s6, s7, s8);
}

[RootSignature(PostEffects_RootSig)]
[numthreads(8, 8, 1)]
void main( 
	uint3 dispatchThreadId  : SV_DispatchThreadID,
	uint3 groupThreadId		: SV_GroupThreadID,
	uint3 groupId 			: SV_GroupID)
{
	// load 4 pixels per thread into LDS(local data share)
	int2 groupTopLeft = (groupId.xy << 3) - 4;	// (groupId.xy * 8(GroupSizeX, GroupSizeY)) - 4(blur radius)
		// upper-left pixel coordinate of group read location
	
	int2 threadTopLeft = (groupThreadId.xy << 1) + groupTopLeft;	
		// upper-left pixel coordinate of quad this thread will read
	
	// store 4 blended-but-unblurred pixels in LDS
	float2 uvUL = (float2(threadTopLeft) + 0.5) * _InverseDimensions.xy;
	float2 uvLR = uvUL + _InverseDimensions.xy;
	float2 uvUR = float2(uvLR.x, uvUL.y);
	float2 uvLL = float2(uvUL.x, uvLR.y);
	// 每个uint存储2个pixel，(row = 16, col = 16/2 = 8)
	int destIdx = groupThreadId.x + (groupThreadId.y << 4);

	float3 pixel1a = lerp(_HigherResBuf[threadTopLeft + uint2(0, 0)], _LowerResBuf.SampleLevel(s_LinearBorderSamper, uvUL, 0.0), _UpsampleBlendFactor);
	float3 pixel1b = lerp(_HigherResBuf[threadTopLeft + uint2(1, 0)], _LowerResBuf.SampleLevel(s_LinearBorderSamper, uvUR, 0.0), _UpsampleBlendFactor);
	Store2Pixels(destIdx+0, pixel1a, pixel1b);    

	float3 pixel2a = lerp(_HigherResBuf[threadTopLeft + uint2(0, 1)], _LowerResBuf.SampleLevel(s_LinearBorderSamper, uvLL, 0.0), _UpsampleBlendFactor);
	float3 pixel2b = lerp(_HigherResBuf[threadTopLeft + uint2(1, 1)], _LowerResBuf.SampleLevel(s_LinearBorderSamper, uvLR, 0.0), _UpsampleBlendFactor);
	Store2Pixels(destIdx+8, pixel2a, pixel2b);

	GroupMemoryBarrierWithGroupSync();

	// horizontally blur the pixels in shared memory
	uint row = groupThreadId.y << 4;	// groupThreadId.y * 8 * 2
    BlurHorizontally(row + (groupThreadId.x << 1), row + groupThreadId.x + (groupThreadId.x & 4));	// +0 or +4

	GroupMemoryBarrierWithGroupSync();

	// vertically blur the pixels and write the result to memory
	BlurVertically(dispatchThreadId.xy, (groupThreadId.y << 3) + groupThreadId.x);
}
