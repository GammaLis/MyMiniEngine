// The CS for extracting bright pixels and downsampling them to an unblurred bloom buffer.
#include "PostEffectsRS.hlsli"
#include "ShaderUtility.hlsli"

cbuffer CSConstants	: register(b0)
{
	float2 _InverseOutputSize;
	float _BloomThreshold;
};

Texture2D<float3> _TexColor	: register(t0);

RWTexture2D<float3> bloomResult		: register(u0);

SamplerState s_LinearClampSampler	: register(s0);

[RootSignature(PostEffects_RootSig)]
[numthreads(8, 8, 1)]
void main( uint3 dispatchThreadId : SV_DispatchThreadID )
{
	// we need the scale factor and the size of one pixel so that out 4 samples are right in the middle
	// of the quadrant they are covering
	// uv
	float2 uv = (dispatchThreadId.xy + 0.5) * _InverseOutputSize;
	float2 offset = _InverseOutputSize * 0.25;	// 0.25 ??? -20-2-23

	// use 4 bilinear samples to guarantee we don't undersample when downsampling by more than 2x
	float3 color1 = _TexColor.SampleLevel(s_LinearClampSampler, uv + float2(-offset.x, -offset.y), 0);
	float3 color2 = _TexColor.SampleLevel(s_LinearClampSampler, uv + float2( offset.x, -offset.y), 0);
	float3 color3 = _TexColor.SampleLevel(s_LinearClampSampler, uv + float2(-offset.x,  offset.y), 0);
	float3 color4 = _TexColor.SampleLevel(s_LinearClampSampler, uv + float2( offset.x,  offset.y), 0);

	float luma1 = RGBToLuminance(color1);
	float luma2 = RGBToLuminance(color2);
	float luma3 = RGBToLuminance(color3);
	float luma4 = RGBToLuminance(color4);

	const float kSmallEpsilon = 0.0001;

	float scaledThreshold = _BloomThreshold;

	// we perform a brightness filter pass, where lone(单独的) bright pixels will contribute less
	color1 *= max(kSmallEpsilon, luma1 - scaledThreshold) / (luma1 + kSmallEpsilon);
	color2 *= max(kSmallEpsilon, luma2 - scaledThreshold) / (luma2 + kSmallEpsilon);
	color3 *= max(kSmallEpsilon, luma3 - scaledThreshold) / (luma3 + kSmallEpsilon);
	color4 *= max(kSmallEpsilon, luma4 - scaledThreshold) / (luma4 + kSmallEpsilon);

	// the shimmer filter helps remove stray bright pixels from the bloom buffer by inversely weighting
	// them by their luminance. The overall effect is to shrink bright pixel regions around the border
	// Lone pixels are likely to dissolve completely. This effect can be tuned by adjusting the shimmer
	// filter inverse strength. The bigger it is, the less a pixel's luminace will matter.
	const float kShimmerFilterInverseStrength = 1.0;
	float weight1 = 1.0 / (luma1 + kShimmerFilterInverseStrength);
	float weight2 = 1.0 / (luma2 + kShimmerFilterInverseStrength);
	float weight3 = 1.0 / (luma3 + kShimmerFilterInverseStrength);
	float weight4 = 1.0 / (luma4 + kShimmerFilterInverseStrength);
	float weightSum = weight1 + weight2 + weight3 + weight4;

	bloomResult[dispatchThreadId.xy] = (color1 * weight1 + color2 * weight2 + color3 * weight3 + color4 * weight4) / weightSum;
}
