#ifndef DENOISING_COMMON_INCLUDED
#define DENOISING_COMMON_INCLUDED

// Ref: AMD - FFXDenoiser

#include "../Common.hlsl"
#include "../../Core/Shaders/PixelPacking_Velocity.hlsli"

#ifndef MULTI_COMPONENTS
#define MULTI_COMPONENTS 0
#endif

#ifndef INFORMAT

#if MULTI_COMPONENTS
#define INFORMAT float3
#else
#define INFORMAT float
#endif

#endif

#ifndef OUTFORMAT

#if MULTI_COMPONENTS
#define OUTFORMAT float3
#else
#define OUTFORMAT float
#endif

#endif

static const float s_RadianceWeightBias = 0.6f;
static const float s_RadianceWeightVarianceK = 0.1f;
// 128 - SVGF paper, 512 - AMD
static const float s_NormalSigma = 128.0f; // 512.0f;
static const float s_DepthSigma = 4.0f;

static const float  s_InvalidValue = -1;
static const float3 s_InvalidColor = 0;

struct DenoisingCommonParameters
{
	float4 BufferSizeAndInvSize;
	uint FilterStep;
	float MinSmoothFactor;
	float StdDevGamma;
	float MinStdDevTolerance;
	float ClampDifferenceToTsppScale;
	uint MinTsppToUseTemporalVariance;
	float MinVarianceToDenoise;
	float DepthWeightCutoff;
	uint BlurStrength_MaxTspp;
	float BlurDecayStrength;
};

ConstantBuffer<DenoisingCommonParameters> _DenoisingCommonParams : register(b0);

Texture2D<float4> _ColorBuffer 		: register(t0);
Texture2D<float>  _DepthBuffer 		: register(t1);
Texture2D<float4> _NormalBuffer		: register(t2);
Texture2D<packed_velocity_t> _VelocityBuffer	: register(t3);
Texture2D<float2> _CurrentFrameMeanVariance	: register(t5);
Texture2D<uint4>  _ReprojectedValue 	: register(t6);
Texture2D<float>  _Variance			: register(t7);
Texture2D<float>  _BlurStrength		: register(t8);

Texture2D<float4> _ColorHistory 	: register(t10);
Texture2D<float>  _DepthHistory 	: register(t11);
Texture2D<float4> _NormalHistory 	: register(t12);
Texture2D<float2> _LumaMomentsHistory	: register(t13);
Texture2D<uint>   _TsppHistory		: register(t14);

#if defined(TEMPORAL_REPROJECTION)
RWTexture2D<uint4>  RWReprojectedValue	: register(u0);
#else
RWTexture2D<OUTFORMAT> Output		: register(u0);
#endif
RWTexture2D<float4> RWColorBuffer 	: register(u1);
// RWTexture2D<float2> RWMeanVariance 	: register(u2);
RWTexture2D<float2> RWLumaMoments	: register(u2);
RWTexture2D<uint>   RWTspp			: register(u3);

SamplerState s_LinearClampSampler 	: register(s0);
SamplerState s_PointClampSampler 	: register(s1);

// DirectX-Graphics-Sample
uint Float2ToHalf(float2 val)
{
	return (f32tof16(val.x) | (f32tof16(val.y) << 16));
}

float2 HalfToFloat2(uint val)
{
	return float2(f16tof32(val & 0xFFFF), f16tof32(val >> 16));
}

bool IsWithinBounds(int2 index, int2 size)
{
	return all(index >= 0) && all(index < size);
}

bool IsWithinBounds(float2 index, float2 size)
{
	return all(index >= 0) && all(index < size);
}

bool IsInRange(uint val, uint minVal, uint maxVal)
{
	return (val >= minVal && val <= maxVal);
}

bool IsInRange(float val, float minVal, float maxVal)
{
	return (val >= minVal && val <= maxVal);
}

// Ref: AMD - FFXDenoiser
float GetEdgeStoppingNormalWeight(float3 normal, float3 sampleNormal)
{
	return pow(saturate(dot(normal, sampleNormal)), s_NormalSigma);
}

float GetEdgeStoppingDepthWeight(float depth, float sampleDepth)
{
	return exp(-abs(depth - sampleDepth) * depth * s_DepthSigma);
}

float GetRadianceWeight(float3 radiance, float3 sampleRadiance, float variance)
{
	return max( exp(-(s_RadianceWeightBias + variance * s_RadianceWeightVarianceK)
		* length(radiance - sampleRadiance)),
		1.0e-2);
}

// Filter kernels
namespace FilterKernel
{
#if defined(BOX_KERNEL_3X3)
	static const uint Radius = 1;
	static const uint Width = 1 + 2 * Radius;
	static const float Kernels[Width][Width] =
	{
		{ 1. / 9, 1. / 9, 1. / 9 },
        { 1. / 9, 1. / 9, 1. / 9 },
        { 1. / 9, 1. / 9, 1. / 9 },
	};

#elif defined(BOX_KERNEL_5X5)
	static const uint Radius = 2;
	static const uint Width = 1 + 2 * Radius;
	static const float Kernel[Width][Width] =
    {
        { 1. / 25, 1. / 25, 1. / 25, 1. / 25, 1. / 25  },
        { 1. / 25, 1. / 25, 1. / 25, 1. / 25, 1. / 25  },
        { 1. / 25, 1. / 25, 1. / 25, 1. / 25, 1. / 25  },
        { 1. / 25, 1. / 25, 1. / 25, 1. / 25, 1. / 25  },
        { 1. / 25, 1. / 25, 1. / 25, 1. / 25, 1. / 25  },
    };

#elif defined(BOX_KERNEL_7X7)
	static const uint Radius = 3;
	static const uint Width = 1 + 2 * Radius;

#elif defined(GAUSSIAN_KERNEL_3X3)
	static const uint Radius = 1;
	static const uint Width = 1 + 2 * Radius;
	static const float Kernel1D[Width] = { 0.27901, 0.44198, 0.27901 };
    static const float Kernel[Width][Width] =
    {
        { Kernel1D[0] * Kernel1D[0], Kernel1D[0] * Kernel1D[1], Kernel1D[0] * Kernel1D[2] },
        { Kernel1D[1] * Kernel1D[0], Kernel1D[1] * Kernel1D[1], Kernel1D[1] * Kernel1D[2] },
        { Kernel1D[2] * Kernel1D[0], Kernel1D[2] * Kernel1D[1], Kernel1D[2] * Kernel1D[2] },
    };

#elif defined(GAUSSIAN_KERNEL_5X5)
	static const uint Radius = 2;
	static const uint Width = 1 + 2 * Radius;
	static const float Kernel1D[Width] = { 1. / 16, 1. / 4, 3. / 8, 1. / 4, 1. / 16 };
    static const float Kernel[Width][Width] =
    {
        { Kernel1D[0] * Kernel1D[0], Kernel1D[0] * Kernel1D[1], Kernel1D[0] * Kernel1D[2], Kernel1D[0] * Kernel1D[3], Kernel1D[0] * Kernel1D[4] },
        { Kernel1D[1] * Kernel1D[0], Kernel1D[1] * Kernel1D[1], Kernel1D[1] * Kernel1D[2], Kernel1D[1] * Kernel1D[3], Kernel1D[1] * Kernel1D[4] },
        { Kernel1D[2] * Kernel1D[0], Kernel1D[2] * Kernel1D[1], Kernel1D[2] * Kernel1D[2], Kernel1D[2] * Kernel1D[3], Kernel1D[2] * Kernel1D[4] },
        { Kernel1D[3] * Kernel1D[0], Kernel1D[3] * Kernel1D[1], Kernel1D[3] * Kernel1D[2], Kernel1D[3] * Kernel1D[3], Kernel1D[3] * Kernel1D[4] },
        { Kernel1D[4] * Kernel1D[0], Kernel1D[4] * Kernel1D[1], Kernel1D[4] * Kernel1D[2], Kernel1D[4] * Kernel1D[3], Kernel1D[4] * Kernel1D[4] },
    };

#elif defined(GAUSSIAN_KERNEL_7X7)
	static const uint Radius = 3;
    static const uint Width = 1 + 2 * Radius;
    static const float Kernel1D[Width] = { 0.00598, 0.060626, 0.241843, 0.383103, 0.241843, 0.060626, 0.00598 };

#elif defined(GAUSSIAN_KERNEL_9X9)
	static const unsigned int Radius = 4;
    static const unsigned int Width = 1 + 2 * Radius;
    static const float Kernel1D[Width] = { 0.000229, 0.005977, 0.060598, 0.241732, 0.382928, 0.241732, 0.060598, 0.005977, 0.000229 };
	
#endif
}


#endif // DENOISING_COMMON_INCLUDED
