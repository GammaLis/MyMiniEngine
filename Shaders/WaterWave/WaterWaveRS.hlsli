#ifndef WATERWAVERS_HLSLI
#define WATERWAVERS_HLSLI

#include "../Utility.hlsli"

#define RootSig_WaterWave \
	"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)," \
	"RootConstants(b0, num32BitConstants = 8)," \
	"CBV(b1)," \
	"CBV(b2)," \
	"DescriptorTable(SRV(t0, numDescriptors = 4))," \
	"DescriptorTable(UAV(u0, numDescriptors = 4)), " \
	"StaticSampler(s0, " \
		"addressU = TEXTURE_ADDRESS_WRAP, " \
		"addressV = TEXTURE_ADDRESS_WRAP, " \
		"addressW = TEXTURE_ADDRESS_WRAP, " \
		"filter = FILTER_MIN_MAG_MIP_LINEAR)"

#define Precision_High 		1024
#define Precision_Medium 	512
#define Precision_Low		256

#define Precision 	Precision_Low

#ifdef FFT_HORIZONTAL
#define GroupSizeX 	Precision
#define GroupSizeY 	1
#else
#define GroupSizeX 	1
#define GroupSizeY 	Precision
#endif

static const float c_g = 9.81f;
static const float c_epsilon = 1e-6;
static const uint c_nBit = ceil(log2(Precision));

float2 ComplexMultiply(float2 a, float2 b)
{
	return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

float2 Conj(float2 v)
{
	return float2(v.x, -v.y);
}

#endif	// WATERWAVERS_HLSLI
