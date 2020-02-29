// The CS for guassian blurring a single RGB buffer.
//
// For the intended bloom blurring algorithm, this shader is expected to be used only on
// the lowest resolution bloom buffer before starting the series of upsample-and-blur
// passes.
#include "PostEffectsRS.hlsli"

cbuffer CSConstants	: register(b0)
{
	float2 _InverseDimensions;
};

Texture2D<float3> _InputBuffer	: register(t0);

RWTexture2D<float3> blurRes		: register(u0);

// the gaussian blur weights (derived from Pascal's triangles -杨辉三角)
static const float Weights[5] = {70.0 / 256.0, 56.0 / 256.0, 28.0 / 256.0, 8.0 / 256.0, 1.0 / 256.0};

float3 BlurPixels(float3 a, float3 b, float3 c, float3 d, float3 e, float3 f, float3 g, float3 h, float3 i)
{
	return Weights[0] * e + Weights[1] * (d + f) + Weights[2] * (c + g) + Weights[3] * (b + h) + Weights[4] * (a + i);
}

float3 BlurPixels(float colors[9])
{
	return Weights[0] * colors[4] + 
	Weights[1] * (colors[3] + colors[5]) + 
	Weights[2] * (colors[2] + colors[6]) + 
	Weights[3] * (colors[1] + colors[7]) +
	Weights[4] * (colors[0] + colors[8]);
}

// lds - local data share (AMD GCN)
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
	uint rr = sh_R[index];
	uint gg = sh_G[index];
	uint bb = sh_B[index];
	pixel1 = float3(f16tof32(rr), f16tof32(gg), f16tof32(bb));
	pixel2 = float3(f16tof32(rr >> 16), f16tof32(gg >> 16), f16tof32(bb >> 16));
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
	Store1Pixel(outIndex+1,	BlurPixels(s1, s2, s3, s4, s5, s6, s7, s8, s9));
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
	uint3 dispatchThreadId 	: SV_DispatchThreadID,
	uint3 groupThreadId		: SV_GroupThreadID,
	uint3 groupId 			: SV_GroupID)
{
	// load 4 pixels per thread into LDS(local data share)
	int2 groupTopLeft = (groupId.xy << 3) - 4;	// (groupId.xy * 8(GroupSizeX, GroupSizeY)) - 4(blur radius)
		// upper-left pixel coordinate of group read location
	
	int2 threadTopLeft = (groupThreadId.xy << 1) + groupTopLeft;	
		// upper-left pixel coordinate of quad this thread will read
	
	// store 4 unblurred pixels in LDS
	// 每个uint存储2个pixel，(row = 16, col = 16/2 = 8)
	int destIdx = groupThreadId.x + (groupThreadId.y << 4);
	Store2Pixels(destIdx+0, _InputBuffer[threadTopLeft + uint2(0, 0)], _InputBuffer[threadTopLeft + uint2(1, 0)]);
	Store2Pixels(destIdx+8, _InputBuffer[threadTopLeft + uint2(0, 1)], _InputBuffer[threadTopLeft + uint2(1, 1)]);

	GroupMemoryBarrierWithGroupSync();

	// horizontally blur the pixels in shared memory
	uint row = groupThreadId.y << 4;	// groupThreadId.y * 8 * 2
	BlurHorizontally(row + (groupThreadId.x << 1), row + groupThreadId.x + (groupThreadId.x & 4));	// +0 or +4

	GroupMemoryBarrierWithGroupSync();

	// vertically blur the pixels and write the result to memory
	BlurVertically(dispatchThreadId.xy, (groupThreadId.y << 3) + groupThreadId.x);
}
