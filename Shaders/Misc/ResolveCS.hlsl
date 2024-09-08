// Ref: MSAAFilter - Resolve.hlsl

#include "Common/DynDescRS.hlsli"

#define GROUP_SIZE 8
#define MSAA_SAMPLES 4

#define RESOLVE_DEPTH 0

static const float EPS = 1e-5f;

static const float PI = 3.1415926535897932f;
static const float TWO_PI = 2.0f * PI;
static const float TAU = 2.0f * PI;
static const float ONE_OVER_PI = 1.0f / PI;
static const float ONE_OVER_TWO_PI = 1.0f / TWO_PI;

cbuffer CBConstants : register(b0, space1)
{
    float4 _TextureIndices;
    float4 _RTSize;
    float4 _Miscs;
};

// These are the sub-sample locations for the 2x, 4x, and 8x standard multisample patterns.
// See the MSDN documentation for the D3D11_STANDARD_MULTISAMPLE_QUALITY_LEVELS enumeration.
#if MSAA_SAMPLES == 8
static const float2 SubSampleOffsets[8] = {
    float2( 0.0625f, -0.1875f),
    float2(-0.0625f,  0.1875f),
    float2( 0.3125f,  0.0625f),
    float2(-0.1875f, -0.3125f),
    float2(-0.3125f,  0.3125f),
    float2(-0.4375f, -0.0625f),
    float2( 0.1875f,  0.4375f),
    float2( 0.4375f, -0.4375f),
};
#elif MSAA_SAMPLES == 4
static const float2 SubSampleOffsets[4] = {
    float2(-0.125f, -0.375f),
    float2( 0.375f, -0.125f),
    float2(-0.375f,  0.125f),
    float2( 0.125f,  0.375f),
};
#elif MSAA_SAMPLES == 2
static const float2 SubSampleOffsets[2] = {
    float2( 0.25f,  0.25f),
    float2(-0.25f, -0.25f),
};
#else
static const float2 SubSampleOffsets[1] = {
    float2(0.0f, 0.0f),
};
#endif

static const int FilterTypes_Box = 0;
static const int FilterTypes_Triangle = 1;
static const int FilterTypes_Gaussian = 2;
static const int FilterTypes_BlackmanHarris = 3;
static const int FilterTypes_Smoothstep = 4;
static const int FilterTypes_BSpline = 5;
static const int FilterTypes_CatmullRom = 6;
static const int FilterTypes_Mitchell = 7;
static const int FilterTypes_GeneralizedCubic = 8;
static const int FilterTypes_Sinc = 9;


// All filtering functions assume that 'x' is normalized to [0, 1], where 1 == FilterRadius
float FilterBox(float x)
{
    return x <= 1.0f;
}

float FilterTriangle(float x)
{
    return saturate(1.0 - x);
}

float FilterGaussian(float x)
{
    const float Sigma = 1.0f;
    const float g = 1.0f / sqrt(TWO_PI * Sigma * Sigma);
    return (g * exp(-(x * x) / (2 * Sigma * Sigma)));
}

float FilterSinc(float x, float filterRadius)
{
    float s;
    x *= filterRadius * 2.0f;
    if (x < 0.001f)
        s = 1.0f;
    else
        s = sin(x * PI) / (x * PI);
    return s;
}

float FilterBlackmanHarris(float x)
{
    x = 1.0f - x;

    const float a0 = 0.35875f;
    const float a1 = 0.48829f;
    const float a2 = 0.14128f;
    const float a3 = 0.01168f;
    return saturate(a0 - a1 * cos(PI * x) + a2 * cos(2 * PI * x) - a3 * cos(3 * PI * x));
}

float FilterSmoothstep(float x)
{
    return 1.0f - smoothstep(0.0f, 1.0f, x);
}

float Filter(float x, float filterType, float filterRadius)
{
    if (filterType == FilterTypes_Box)
        return FilterBox(x);
    else if (filterType == FilterTypes_Triangle)
        return FilterTriangle(x);
    else if (filterType == FilterTypes_Gaussian)
        return FilterGaussian(x);
    else if (filterType == FilterTypes_Sinc)
        return FilterSinc(x, filterRadius);
    else if (filterType == FilterTypes_Sinc)
        return FilterSinc(x, filterRadius);
    else
        return 1.0f;
}

#define MSAA_LOAD(tex, addr, subSampleIdx) tex.Load(uint2(addr), subSampleIdx)

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main( uint2 dtid : SV_DispatchThreadID )
{
    uint2 RTSize = uint2(_RTSize.xy);
    if (any(dtid >= RTSize))
        return;

    uint2 pixelCoord = dtid;
    float2 pixelPos = float2(pixelCoord + 0.5);
    
	const uint4 TextureIndices = uint4(_TextureIndices);
    
    const uint colorTextureIndex = TextureIndices.x;
    const uint colorResolveIndex = TextureIndices.y;
    const uint depthTextureIndex = TextureIndices.z;
    const uint depthResolveIndex = TextureIndices.w;
    
    Texture2DMS<float4> colorTexture = ResourceDescriptorHeap[colorTextureIndex];
#if RESOLVE_DEPTH
    Texture2DMS<float>  depthTexture = ResourceDescriptorHeap[depthTextureIndex];
#endif

    
#if 0
    const uint FilterType = FilterTypes_Box;
    
    const float SampleRadius = max(0.0, min(_TextureIndices.z, 10.0f));
    const float FilterRadius = 1.0f;
    
	float4 colorSum = 0.0;
	float weightSum = 0.0;
    
    float2 uv = pixelPos * _RTSize.zw;
    for (int y = -SampleRadius; y <= SampleRadius; y++)
    {
        for (int x = -SampleRadius; x <= SampleRadius; x++)
        {
            float2 sampleOffset = float2(x, y);
            float2 samplePos = pixelPos + sampleOffset;
            samplePos = clamp(samplePos, 0.0f, _RTSize.xy);

            for (uint subSampleIdx = 0; subSampleIdx < MSAA_SAMPLES; subSampleIdx++)
            {
                float2 subSampleOffset = SubSampleOffsets[subSampleIdx];
                float2 sampleDist = abs(sampleOffset + subSampleOffset) /  SampleRadius;
                
                bool bUseSample = all(sampleDist <= 1.0f);
                float4 sample = MSAA_LOAD(colorTexture, samplePos, subSampleIdx);
                sample = max(sample, 0.0);

                float weight = Filter(sampleDist.x, FilterType, FilterRadius) *
                    Filter(sampleDist.y, FilterType, FilterRadius);

                colorSum += sample * weight;
                weightSum += weight;
            }
        }
    }
	float4 colorResolve = colorSum / max(weightSum, EPS);
#else
    
	uint2 msaaPixel = pixelCoord >> 1;
	uint subSampleIndex = ((pixelCoord.y & 1) << 1) | (pixelCoord.x & 1);
	float4 sample = MSAA_LOAD(colorTexture, msaaPixel, subSampleIndex);
    
	float4 colorResolve = sample;
#endif

    RWTexture2D<float4> colorResolveTex = ResourceDescriptorHeap[colorResolveIndex];
    
#if RESOLVE_DEPTH
    RWTexture2D<float > depthResolveTex = ResourceDescriptorHeap[depthResolveIndex];
#endif

    // DEBUG:
	// colorResolve = float4(uv, 0.0f, 1.0f);
    
    colorResolveTex[pixelCoord] = colorResolve;
}
