// INTEL - TAA
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2019-2020, Intel Corporation
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated 
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation 
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of 
// the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, 
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
// SOFTWARE.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "TemporalRS.hlsli"
#include "PixelPacking_Velocity.hlsli"
#include "PixelPacking_R11G11B10.hlsli"

#define ENABLE_DEBUG 0
#define USE_BICUBIC_FILTER 1
#define USE_TONE_MAPPED_COLOR_ONLY_IN_FINAL 0
#define PACK_COLOR 1

cbuffer CBConstants : register(b1) 
{
    float4 _Resolution; // width, height, 1/width, 1/height
    float2 _Jitter;
    uint _FrameIndex;
    uint _DebugFlags;
};

Texture2D<packed_velocity_t> _VelocityBuffer: register(t0);
// current color buffer - rgb used
Texture2D<float3> _InColor      : register(t1);
// stored temporal antialiasing pixel - .a should be sufficient enough to handle weight stored as float [0.5f, 1.0f)
Texture2D<float4> _InTemporal   : register(t2);
// current linear depth buffer 
Texture2D<float>  _CurDepth     : register(t3);
// previous linear depth buffer
Texture2D<float>  _PreDepth     : register(t4);

// antialiased color buffer (used in a next frame as the HistoryTexture
RWTexture2D<float4> OutTemporal : register(u0);

SamplerState s_LinearSampler    : register(s0);
SamplerState s_PointSampler     : register(s1);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Defines to tweak in/out formats
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// use Tone-mapped values only for final lerp
#ifndef USE_TONE_MAPPED_COLOR_ONLY_IN_FINAL
#define USE_TONE_MAPPED_COLOR_ONLY_IN_FINAL 0
#endif

// history buffer is stored as tone mapped
#ifndef KEEP_HISTORY_TONE_MAPPED
#define KEEP_HISTORY_TONE_MAPPED ( 1 && (USE_TONE_MAPPED_COLOR_ONLY_IN_FINAL == 0) )
#endif

// enabled tweaking from host app, uses _DebugFlags to toggle features
#ifndef ENABLE_DEBUG
#define ENABLE_DEBUG 1
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Defines to tweak quality options (and performance)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Best quality settings:
//#define USE_DEPTH_THRESHOLD 1
//#define VARIANCE_BBOX_NUMBER_OF_SAMPLES USE_SAMPLES_9
//#define USE_VARIANCE_CLIPPING 2
//#define USE_YCOCG_SPACE 1
//#define ALLOW_NEIGHBOURHOOD_SAMPLING 1
//#define USE_BICUBIC_FILTER 1
//#define USE_LONGEST_VELOCITY_VECTOR 1 //greatly improves edges AA quality, but may introduce blur - app choice

// High quality settings that should handle most cases
//#define USE_DEPTH_THRESHOLD 1
//#define VARIANCE_BBOX_NUMBER_OF_SAMPLES USE_SAMPLES_5
//#define USE_VARIANCE_CLIPPING 1
//#define USE_YCOCG_SPACE 1
//#define ALLOW_NEIGHBOURHOOD_SAMPLING 1
//#define USE_BICUBIC_FILTER 1
//#define USE_LONGEST_VELOCITY_VECTOR 1 //greatly improves edges AA quality, but may introduce blur - app choice

// Gen9 (skull canyon) / Gen11 settings
//#define USE_DEPTH_THRESHOLD 0
//#define VARIANCE_BBOX_NUMBER_OF_SAMPLES USE_SAMPLES_5
//#define USE_VARIANCE_CLIPPING 1
//#define USE_YCOCG_SPACE 0
//#define ALLOW_NEIGHBOURHOOD_SAMPLING 0
//#define USE_BICUBIC_FILTER 0
//#define USE_LONGEST_VELOCITY_VECTOR 0

// MIN - MAX variance gamma, it's lerped using a velocity confidence factor
#define MIN_VARIANCE_GAMMA 0.75f // under motion
#define MAX_VARIANCE_GAMMA 2.0f // no motion

// MAX_T to ensure stability when using USE_VARIANCE_CLIPPING option 2
#define VARIANCE_INTERSECTION_MAX_T 100

// defference between current depth buffer and previous depth buffer to consider a pixel as an edge
#define DEPTH_DIFF 0.002f

// mark pixel as no-valid-history when depth value between current and previous frames goes above a threshold
#ifndef USE_DEPTH_THRESHOLD 
#define USE_DEPTH_THRESHOLD 1
#endif

// definition of number of samples
#define USE_SAMPLES_5 0
#define USE_SAMPLES_9 1

// how many samples for variance clipping, 5 is faster and usually gives expected results, 9 should be used in scenes when 
// there's shimmering on edges between dark and bright colors
#ifndef VARIANCE_BBOX_NUMBER_OF_SAMPLES
#define VARIANCE_BBOX_NUMBER_OF_SAMPLES USE_SAMPLES_9
#endif

// use variance clipping: 0 - no clipping, 1 - clamp to AABB min/max, 2 - use color intersection
// 1 is sufficient for most cases
#ifndef USE_VARIANCE_CLIPPING
#define USE_VARIANCE_CLIPPING 1 // 0 - no clipping, 1 - use min/max clipping, 2 - use color intersection
#endif

// use YCoCg color space when the Variance Clipping is enabled
#ifndef USE_YCOCG_SPACE
#define USE_YCOCG_SPACE 1
#endif

// use neighborhood sampling for pixels that don't have valid history
#ifndef ALLOW_NEIGHBORHOOD_SAMPLING
#define ALLOW_NEIGHBORHOOD_SAMPLING 1
#endif

// allow to use bicubic filter for better quality of the history color
#ifndef USE_BICUBIC_FILTER
#define USE_BICUBIC_FILTER 1
#endif

// sample neighborhood and select the longest vector - greatly improves quality on edges but may introduce more blur
#ifndef USE_LONGEST_VELOCITY_VECTOR
#define USE_LONGEST_VELOCITY_VECTOR 1
#endif

// how many samples to use for finding the longest velocity vector
#ifndef LONGEST_VELOCITY_VECTOR_SAMPLES
#define LONGEST_VELOCITY_VECTOR_SAMPLES USE_SAMPLES_9
#endif

// difference in pixels for velocity after the pixel is marked as no history
#ifndef FRAME_VELOCITY_IN_PIXELS_DIFF
#define FRAME_VELOCITY_IN_PIXELS_DIFF 128 // valid for 1920x1080
#endif

#ifndef NEEDS_EDGE_DETECTION
#define NEEDS_EDGE_DETECTION ( (USE_BICUBIC_FILTER == 0) && (USE_LONGEST_VELOCITY_VECTOR == 0) )
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Implementation
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define NUM_THREADS_X 8
#define NUM_THREADS_Y 8

#define TGSM_CACHE_WIDTH  (NUM_THREADS_X + 2) // +2 for border
#define TGSM_CACHE_HEIGHT (NUM_THREADS_Y + 2) // +2 for border
#define MAKEOFFSET(x, y)  int((x) + (y) * TGSM_CACHE_WIDTH)
// NOTE:宏 加括号！！！

#if PACK_COLOR
groupshared uint TGSM_Color[TGSM_CACHE_WIDTH * TGSM_CACHE_HEIGHT];  // 8x8 + 1 pixel border = 10x10
#else
#define Pack_R11G11B10_FLOAT 
groupshared float3 TGSM_Color[TGSM_CACHE_WIDTH * TGSM_CACHE_HEIGHT];
#endif

bool AllowYCoCg () 
{ 
#if ENABLE_DEBUG == 1
    const bool useYCoCgSpace = (_DebugFlags & 0x10) > 0;
#elif USE_YCOCG_SPACE == 1
    const bool useYCoCgSpace = true;
#else
    const bool useYCoCgSpace = false;
#endif

    return useYCoCgSpace;
}

/// Helper Functions
float3 RGB2YCoCg (float3 inRGB )
{
    // Y  = R / 4 + G / 2 + B / 4
    // Co = R / 2 - B / 2
    // Cg = -R / 4 + G / 2 - B / 4
    if ( AllowYCoCg() )
    {
        const float y  = dot( inRGB, float3( 0.25f, 0.5f, 0.25f ) );
        const float co = dot( inRGB, float3( 0.5f, 0.f, -0.5f ) );
        const float cg = dot( inRGB, float3( -0.25f, 0.5f, -0.25f ) );
        return float3( y, co, cg );
    }
    else
    {
        return inRGB;
    }
}

float3 YCoCg2RGB( float3 inYCoCg )
{
    // R = Y + Co - Cg
    // G = Y + Cg
    // B = Y - Co - Cg
    if ( AllowYCoCg() )
    {
        const float r = dot( inYCoCg, float3( 1.f, 1.f, -1.f ) );
        const float g = dot( inYCoCg, float3( 1.f, 0.f, 1.f ) );
        const float b = dot( inYCoCg, float3( 1.f, -1.f, -1.f ) );
        return float3( r, g, b );
    }
    else
    {
        return inYCoCg;
    }
}

// Reinhard tone mapper
float LuminanceRec709 (float3 rgb)
{
    return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 Reinhard (float3 rgb)
{
    return rgb / (1.0f + LuminanceRec709(rgb));
}

float3 InverseReinhard (float3 rgb)
{
    return rgb / (1.0f - LuminanceRec709(rgb));
}

// precache current colors to SLM
void StoreCurrentColorToSLM (uint2 groupStartThread, uint2 groupThreadId)
{
    if ((groupThreadId.x < (TGSM_CACHE_WIDTH / 2)) && (groupThreadId.y < (TGSM_CACHE_HEIGHT / 2)))
    {
        const uint2 indexMul2 = groupThreadId.xy * uint2(2, 2);

        const int linearTGSMIndexOfTopLeft = MAKEOFFSET(indexMul2.x, indexMul2.y);

        const int2 stForTexture = int2(groupStartThread) + int2(indexMul2) - 1;
        const float2 uv = _Resolution.zw * float2(stForTexture + 1.0); // +1 ??? - Intel is +0.5
        float4 Rs = _InColor.GatherRed(s_LinearSampler, uv);
        float4 Gs = _InColor.GatherGreen(s_LinearSampler, uv);
        float4 Bs = _InColor.GatherBlue(s_LinearSampler, uv);

    #if USE_TONE_MAPPED_COLOR_ONLY_IN_FINAL == 0
        TGSM_Color[linearTGSMIndexOfTopLeft]                        = Pack_R11G11B10_FLOAT( Reinhard(float3(Rs.w, Gs.w, Bs.w)) );
        TGSM_Color[linearTGSMIndexOfTopLeft + 1]                    = Pack_R11G11B10_FLOAT( Reinhard(float3(Rs.z, Gs.z, Bs.z)) );
        TGSM_Color[linearTGSMIndexOfTopLeft + TGSM_CACHE_WIDTH]     = Pack_R11G11B10_FLOAT( Reinhard(float3(Rs.x, Gs.x, Bs.x)) );
        TGSM_Color[linearTGSMIndexOfTopLeft + TGSM_CACHE_WIDTH + 1] = Pack_R11G11B10_FLOAT( Reinhard(float3(Rs.y, Gs.y, Bs.y)) );
    #else
        TGSM_Color[linearTGSMIndexOfTopLeft]                        = Pack_R11G11B10_FLOAT( float3(Rs.w, Gs.w, Bs.w) );
        TGSM_Color[linearTGSMIndexOfTopLeft + 1]                    = Pack_R11G11B10_FLOAT( float3(Rs.z, Gs.z, Bs.z) );
        TGSM_Color[linearTGSMIndexOfTopLeft + TGSM_CACHE_WIDTH]     = Pack_R11G11B10_FLOAT( float3(Rs.x, Gs.x, Bs.x) );
        TGSM_Color[linearTGSMIndexOfTopLeft + TGSM_CACHE_WIDTH + 1] = Pack_R11G11B10_FLOAT( float3(Rs.y, Gs.y, Bs.y) );
    #endif
    }
    GroupMemoryBarrierWithGroupSync();
}

// returns current frame color
float3 GetCurrentColor (int st)
{
#if PACK_COLOR
    return float3(Unpack_R11G11B10_FLOAT(TGSM_Color[st]));
#else
    return TGSM_Color[st];
#endif
}

// performs ray-aabb intersection
float3 ClipToAABB (float3 historyColor, float3 currentColor, float3 BBCenter, float3 BBExtents)
{
    const float3 direction = currentColor - historyColor;

    // calculate intersection for the closest slabs from the center of the AABB in HistoryColor direction
    const float3 intersection = ( (BBCenter - sign(direction) * BBExtents) - historyColor ) / direction;

    // clip unexpected T values
    const float3 possibleT = intersection >= 0.0 ? intersection : VARIANCE_INTERSECTION_MAX_T + 1.0f;
    const float t = min(VARIANCE_INTERSECTION_MAX_T, min(possibleT.x, min(possibleT.y, possibleT.z)));

    // final history color
    return float3(t < VARIANCE_INTERSECTION_MAX_T ? historyColor + direction * t : historyColor);
}

// performs VarianceClipping
// following Marco Salvi's paper (idea + implementation) from GDC16: An excursion in Temporal Supersampling
bool AllowVarianceClipping () 
{
#if ENABLE_DEBUG == 1
    const bool useVarianceClipping = (_DebugFlags & 0x8) > 0;
#elif USE_VARIANCE_CLIPPING != 0
    const bool useVarianceClipping = true;
#else
    const bool useVarianceClipping = false;
#endif

    return useVarianceClipping;
}

float3 ClipHistoryColor (float3 currentColor, float3 historyColor, int screenST, float varianceGamma, uint frameNumber)
{
    float3 toReturn = historyColor;
    if (AllowVarianceClipping())
    {
    #if VARIANCE_BBOX_NUMBER_OF_SAMPLES == USE_SAMPLES_9
        frameNumber = 0;
        // 9 samples in '+' and 'x'
        const int offsets[1][8] = 
        {
            MAKEOFFSET(-1, -1),
            MAKEOFFSET(-1,  0),
            MAKEOFFSET(-1, +1),
            MAKEOFFSET( 0, -1),
            MAKEOFFSET( 0, +1),
            MAKEOFFSET(+1, -1),
            MAKEOFFSET(+1,  0),
            MAKEOFFSET(+1, +1)
        };
        const uint imax = 8;
        const float rcpDivider = 1.0 / 9.0;

    #else
        // 5 samples, current '+' is used. I had an idea to remove shimmering using rotating sampling pattern but i don't have good test case...
        const int offsets[2][4] = 
        {
            { MAKEOFFSET(-1,  0), MAKEOFFSET( 0, +1), MAKEOFFSET(+1,  0), MAKEOFFSET( 0, -1) },
            { MAKEOFFSET(-1, -1), MAKEOFFSET(+1, -1), MAKEOFFSET(+1, +1), MAKEOFFSET(-1, +1) }
        }；
        const uint imax = 4;
        const float rcpDivider = 0.2f;
    #endif
        // calculate mean value (mean) and standard deviation (variance)
        const float3 currentColorInYCoCg = RGB2YCoCg(currentColor);

        float3 moment1 = currentColorInYCoCg;
        float3 moment2 = currentColorInYCoCg * currentColorInYCoCg;
        [unroll]
        for (uint i = 0; i < imax; ++i)
        {
            const int newST = screenST + offsets[frameNumber][i];
            const float3 newColor = RGB2YCoCg(GetCurrentColor(newST));
            moment1 += newColor;
            moment2 += newColor * newColor;
        }

        // mean is the center of AABB and variance (standard deviation) is its extents
        const float3 mean = moment1 * rcpDivider;
        const float3 variance = sqrt(moment2 * rcpDivider - mean * mean) * varianceGamma;

    #if USE_VARIANCE_CLIPPING == 1
        // clamp to AABB min/max
        const float3 minC = float3(mean - variance);
        const float3 maxC = float3(mean + variance);

        toReturn = clamp(historyColor, YCoCg2RGB(minC), YCoCg2RGB(maxC));
    #else
        // do the color/AABB intersection
        toReturn = YCoCg2RGB( ClipToAABB( RGB2YCoCg(historyColor), float3(currentColorInYCoCg), mean, variance ) );

    #endif
    }

    return toReturn;
}

// 5-tap bicubic sampling - taken from MiniEngine by Microsoft, few things changed to fit my approach
float4 BicubicSampling5 (float2 historyST)
{
    const float2 rcpResolution = _Resolution.zw;
    const float2 fractional = frac(historyST);
    const float2 uv = ( floor(historyST) + float2(0.5, 0.5) ) * rcpResolution;

    // 5-tap bicubic sampling (for Hermite/Catmull-Rom filter) -- (approximate from original 16->9-tap bilinear fetching)
    const float2 t  = fractional;
    const float2 t2 = fractional * fractional;
    const float2 t3 = fractional * fractional * fractional;
    const float s = 0.5;
    const float2 w0 = -s * t3 + 2.0f * s * t2 - s * t;
    const float2 w1 = (2.0 - s) * t3 + (s - 3.0) * t2 + 1.0;
    const float2 w2 = (s - 2.0) * t3 + (3 - 2.0 * s) * t2 + s * t;
    const float2 w3 = s * t3 - s * t2;
    const float2 s0 = w1 + w2;
    const float2 f0 = w2 / (w1 + w2);
    const float2 m0 = uv + f0 * rcpResolution;
    const float2 tc0 = uv - 1.f * rcpResolution;
    const float2 tc3 = uv + 2.f * rcpResolution;

    const float4 A = _InTemporal.SampleLevel(s_LinearSampler, float2(m0.x, tc0.y), 0);
    const float4 B = _InTemporal.SampleLevel(s_LinearSampler, float2(tc0.x, m0.y), 0);
    const float4 C = _InTemporal.SampleLevel(s_LinearSampler, float2(m0.x, m0.y ), 0);
    const float4 D = _InTemporal.SampleLevel(s_LinearSampler, float2(tc3.x, m0.y), 0);
    const float4 E = _InTemporal.SampleLevel(s_LinearSampler, float2(m0.x, tc3.y), 0);
    const float4 col = (0.5 * (A + B) * w0.x + A * s0.x + 0.5 * (A + B) * w3.x) * w0.y + 
        (B * w0.x + C * s0.x + D * w3.x) * s0.y + 
        (0.5 * (B + E) * w0.x + E * s0.x + 0.5 * (D + E) * w3.x) * w3.y;
    return col;
}

bool AllowLongestVelocityVector () 
{
#if ENABLE_DEBUG == 1
    const bool useLongestVelocityVector = (_DebugFlags & 0x40) > 0;
#elif ALLOW_NEIGHBOURHOOD_SAMPLING == 1
    const bool useLongestVelocityVector = true;
#else
    const bool useLongestVelocityVector = false;
#endif

    return useLongestVelocityVector;
}

bool AllowBicubicFilter () 
{ 
#if ENABLE_DEBUG == 1
    const bool useBicubicFilter = (_DebugFlags & 0x4) > 0;
#elif USE_BICUBIC_FILTER == 1
    const bool useBicubicFilter = true;
#else
    const bool useBicubicFilter = false;
#endif

    return useBicubicFilter;
}

// sample history, if Longest Velocity Vector is used then entire history is sampled using selected algorithm (Bicubic or Bilinear).
// Bilinear gets entire image blurrier but looks better on edges if Longest Velocity Vector is _not_ enabled (and it's faster)
float4 GetHistory (float2 historyUV, float2 historyST, bool bIsOnEdge)
{
    float4 toReturn = 0.0;
    const bool useBilinearOnEdges = (AllowLongestVelocityVector() == false);
    const bool useBicubic = (AllowLongestVelocityVector() == true) || ( (bIsOnEdge == false) && (useBilinearOnEdges == true) );
    if (AllowBicubicFilter() && (useBicubic == true))
    {
        toReturn = BicubicSampling5(historyST);
    }
    else
    {
        toReturn = float4(_InTemporal.SampleLevel(s_LinearSampler, historyUV, 0));
    }

#if (KEEP_HISTORY_TONE_MAPPED == 0) && (USE_TONE_MAPPED_COLOR_ONLY_IN_FINAL)
    toReturn = float4(Reinhard(toReturn.rgb), toReturn.a);
#endif
    return toReturn;
}

// get velocity and expected depth diff for the current pixel
float3 GetVelocity (int2 screenST)
{
    float3 toReturn = UnpackVelocity(_VelocityBuffer[screenST]);
    if (AllowLongestVelocityVector())
    {
    #if LONGEST_VELOCITY_VECTOR_SAMPLES == USE_SAMPLES_9
        const int2 offsets[8] = 
        {
            int2(-1, -1), int2(-1,  0), int2(-1, +1), 
            int2( 0, -1), int2( 0, +1),
            int2(+1, -1), int2(+1,  0), int2(+1,  +1)
        };
        const uint NSamples = 8;
    #else
        const int2 offsets[4] = 
        {
            int2(-1, -1), int2(-1, +1), int2(+1, -1), int2(+1, +1)
        };
        const uint NSamples = 4;
    #endif
        float currentLengthSq = dot(toReturn.xy, toReturn.xy);
        [unroll]
        for (uint i = 0; i < NSamples; ++i)
        {
            const float3 velocity = UnpackVelocity(_VelocityBuffer[screenST + offsets[i]]);
            const float sampleLengthSq = dot(velocity.xy, velocity.xy);
            if (sampleLengthSq > currentLengthSq)
            {
                toReturn = velocity;
                currentLengthSq = sampleLengthSq;
            }
        }
    }

    return toReturn;
}

// samples neighborhood in 'x' pattern for no-history pixel
bool AllowNeighborhoodSampling() { return true; }

float3 GetCurrentColorNeighborhood (float3 currentColor, int screenST)
{
    const int offsets[4] = 
    {
        MAKEOFFSET(-1, -1), MAKEOFFSET(-1, +1), MAKEOFFSET(+1, -1), MAKEOFFSET(+1, +1)
    };

    float3 accCol = currentColor;
    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        const int newST = screenST + offsets[i];
        accCol += GetCurrentColor(newST);
    }
    accCol *= 0.2f;

    return accCol;
}

// Min and Max of depth values helpers
float MaxOf(float4 depths) { return max(depths.x, max(depths.y, max(depths.z, depths.w))); }
float MinOf(float4 depths) { return min(depths.x, min(depths.y, min(depths.z, depths.w))); }

// get current depth and check whether it's on the edge
float GetCurrentDepth (float2 uv, out bool isOnEdge)
{
    const float4 depths = _CurDepth.Gather(s_LinearSampler, uv);
    const float minDepth = MinOf(depths);
#if NEEDS_EDGE_DETECTION == 1
    const float maxDepth = MaxOf(depths);
    isOnEdge = abs(maxDepth - minDepth) > DEPTH_DIFF;
#else
    isOnEdge = false;
#endif
    
    return minDepth;
}

// previous depth
float GetPreviousDepth (float2 uv)
{
    return MaxOf(_PreDepth.Gather(s_LinearSampler, uv)) + 0.001f;
}

// helpe to convert ST coordinates to UV
float2 GetUV (float2 st)
{
    return (st + 0.5) * _Resolution.zw;
}

// calculate depth confidence factor using the current and previous depth buffers
bool AllowDepthThreshold () 
{
#if ENABLE_DEBUG == 1
    const bool useDepthThreshold = (_DebugFlags & 0x2) == 2;
#elif USE_DEPTH_THRESHOLD == 1
    const bool useDepthThreshold = true;
#else
    const bool useDepthThreshold = false;
#endif
    
    return useDepthThreshold;
}

float GetDepthConfidenceFactor (uint2 st, float3 velocity, float currentFrameDepth, bool bIsOnEdge)
{
    float depthDiffFactor = 1.0f;
    if (AllowDepthThreshold())
    {
        const float prevDepth = GetPreviousDepth( GetUV(st + velocity.xy + _Jitter.xy) );
        const float currDepth = currentFrameDepth + velocity.z;
    #if NEEDS_EDGE_DETECTION == 1
        depthDiffFactor = !bIsOnEdge ? step(currDepth, prevDepth) : depthDiffFactor;
    #else
        depthDiffFactor = step(currDepth, prevDepth);
    #endif
    }

    return depthDiffFactor;
}

// perfrom final lerp between the current frame color and the temporal history color
float4 GetFinalColor (float3 currentColor, float3 historyColor, float weight)
{
    // calculate a new confidence factor for the next frame. The value is between [0.5, 1.0)
    const float newWeight = saturate( 1.0f / (2.0f - weight) );
#if USE_TONE_MAPPED_COLOR_ONLY_IN_FINAL == 0
    float4 toReturn = float4( lerp(currentColor, historyColor, weight), newWeight );
#if KEEP_HISTORY_TONE_MAPPED == 0
    toReturn = float4( InverseReinhard(toReturn.rgb), toReturn.a );
#endif

#else
    float4 toReturn = float4( InverseReinhard(lerp(Reinhard(currentColor), Reinhard(historyColor), weight)), newWeight );
#endif
    
    return toReturn;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Debug functions
//
//    uint        DebugFlags;         
// AllowLongestVelocityVector | AllowNeighbourhoodSampling | AllowYCoCg | AllowVarianceClipping | AllowBicubicFilter | AllowDepthThreshold | MarkNoHistoryPixels
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float3 DebugColorNoHistory ()
{
#if ENABLE_DEBUG == 1
    const bool markColors = (_DebugFlags & 0x1);
#else
    const bool markColors = false;
#endif
    
    if (markColors)
    {
        return float3( 0.2f, 0.8f, 0.2f );
    }
    else
    {
        return float3( 1.0f, 1.0f, 1.0f );
    }
}

/// Entry
[RootSignature(Temporal_RootSig)]
[numthreads(NUM_THREADS_X, NUM_THREADS_Y, 1)]
void main( 
    uint3 dtid  : SV_DispatchThreadID,
    uint3 gid   : SV_GroupID,
    uint3 gtid  : SV_GroupThreadID)
{
    const uint2 screenST = dtid.xy;

    const uint2 groupStartingThread = uint2(gid.xy) * uint2(NUM_THREADS_X, NUM_THREADS_Y);
    StoreCurrentColorToSLM(groupStartingThread, gtid.xy);

    const int groupST = MAKEOFFSET(gtid.x + 1, gtid.y + 1); // +1 border

    // sample current color
    const float3 currentFrameColor = GetCurrentColor(groupST);

    // to mark edges - edges may get different filtering
    bool bIsOnEdge = false;

    // get velocity
    const float3 velocity = GetVelocity(screenST);

    // calculate confidence factor based on the velocity of current pixel, everything moving faster than FRAME_VELOCITY_IN_PIXELS_DIFF 
    // frame-to-frame will be marked as no-history
    const float velocityConfidenceFactor = saturate( 1.0 - length(velocity.xy) / FRAME_VELOCITY_IN_PIXELS_DIFF );

    // prev frame ST and UV
    const float2 prevFrameScreenST = screenST + velocity.xy;
    const float2 prevFrameScreenUV = GetUV(prevFrameScreenST);

    // get current depth and ...
    const float depth = GetCurrentDepth(GetUV(screenST), bIsOnEdge);

    // get depth confidence factor, larger than 0, assume the history is valid
    const float depthDiffFactor = GetDepthConfidenceFactor(screenST, velocity, depth, bIsOnEdge);

    // do we have a valid history
    const float uvWeight = (all(prevFrameScreenUV >= float2(0.f, 0.f)) && all(prevFrameScreenUV < float2(1.f, 1.f))) ? 1.0f : 0.0f;
    const bool hasValidHistory = (velocityConfidenceFactor * depthDiffFactor * uvWeight) > 0.0f;
    float4 finalColor = float4(1.f.xxxx);

    if (hasValidHistory)
    {
        // sample history
        float4 rawHistoryColor = float4(GetHistory(prevFrameScreenUV, prevFrameScreenST, bIsOnEdge));

        // lerp between MIN and MAX variance gamma to ensure when no motion specular highlights not cut by the variance clipping
        const float varianceGamma = lerp( MIN_VARIANCE_GAMMA, MAX_VARIANCE_GAMMA, velocityConfidenceFactor * velocityConfidenceFactor );

        // clip history color to the bounding box of expected colors based on the current frame color
        const float3 historyColor = ClipHistoryColor(currentFrameColor, rawHistoryColor.rgb, groupST, varianceGamma, 0); // _FrameIndex

        // final weight for lerp between the current frame color and the temporal history color
        const float weight = rawHistoryColor.a * velocityConfidenceFactor * depthDiffFactor;

        finalColor = GetFinalColor(currentFrameColor, historyColor, weight);
    }
    else 
    {
        const float3 filteredCurrentNeighborhood = GetCurrentColorNeighborhood(currentFrameColor, groupST) * DebugColorNoHistory();
        finalColor = float4(filteredCurrentNeighborhood, 0.5f);
    }

    // store the final pixel color
    OutTemporal[dtid.xy] = finalColor.rgba;
}
