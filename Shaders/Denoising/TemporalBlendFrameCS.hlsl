//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

// 2nd stage of temporal supersampling. Blends current frame values
// with values reprojected from previous frame in stage 1.

#define HLSL
#define MULTI_COMPONENTS 1
#define OUTFORMAT float

#define GROUP_SIZE 8

#include "DenoisingCommon.hlsl"

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(uint2 dtid : SV_DispatchThreadID)
{
	uint4 reprojected = _ReprojectedValue[dtid];
	float3 cachedValue = f16tof32(reprojected.xyz);
	uint packed = reprojected.w;
	float cachedValueSquareMean = f16tof32(packed & 0xFFFF);
	// uint tspp = (packed >> 16);
	uint tspp = RWTspp[dtid];

	bool bCurrentFrameValueActive = true;
#if 0
	if (_DenoisingCommonParams.bCheckerboard)
	{
		bool bEvenPixel = ((dtid.x + dtid.y) & 1) == 0;
		bCurrentFrameValueActive = _DenoisingCommonParams.bEvenPixelActive == bEvenPixel;
	}
#endif

#if !MULTI_COMPONENTS
	float value = bCurrentFrameValueActive ? _CurrentFrameValue[dtid] : s_InvalidValue;
	bool bValidValue = value != s_InvalidValue;
	float valueSquaredMean = bValidValue ? value * value : s_InvalidValue;
	float variance = s_InvalidValue;
#else
	float4 currentColor = _ColorBuffer[dtid];
	float3 value = bCurrentFrameValueActive ? currentColor.rgb : s_InvalidColor;
	bool  bValidValue = all(value != s_InvalidColor);

#if 0
	float3 valueSquaredMean = bValidValue ? value * value : s_InvalidColor;
	float3 variance = s_InvalidColor;
#else
	float valueLuma = Luminance(value);
	float valueSquaredMean = bValidValue ? valueLuma * valueLuma : 0;
	float variance = s_InvalidValue;
#endif

#endif

	if (tspp > 0)
	{
		uint maxTspp = 1.0f / _DenoisingCommonParams.MinSmoothFactor;
		tspp = bValidValue ? min(tspp + 1, maxTspp) : tspp;

		float cachedValueLuma = Luminance(cachedValue);
		float2 localMeanVariance = _CurrentFrameMeanVariance[dtid];
		float localMean = localMeanVariance.x;
		float localVariance = localMeanVariance.y;
		// if (_DenoisingCommonParams.bClampCachedValues)
		{
			float localStdDev = max(_DenoisingCommonParams.StdDevGamma * sqrt(localVariance),
				_DenoisingCommonParams.MinStdDevTolerance);
			float nonClampedCachedValueLuma = cachedValueLuma;

			// Clamp value to mean  +/- std.dev of local neighborhood to suppress ghosting on value changing due to other occluder movements.
			// Ref: Salvi2016, Temporal Super-Sampling
			cachedValueLuma = clamp(cachedValueLuma, localMean - localStdDev, localMean + localStdDev);
			cachedValue *= (cachedValueLuma / nonClampedCachedValueLuma);

			// Scale down the tspp based on how strongly the cached value got clamped to give more weight to new samples
			float tsppScale = saturate(_DenoisingCommonParams.ClampDifferenceToTsppScale * abs(cachedValueLuma - nonClampedCachedValueLuma));
			tspp = lerp(tspp, 0, tsppScale);
		}

		float invTspp = 1.0f / max(tspp, 1);
		float a = max(invTspp, _DenoisingCommonParams.MinSmoothFactor);
		const float MaxSmoothFactor = 1.0f;
		a = min(a, MaxSmoothFactor);

		// TODO: use average weighting instead of exponential for the first few samples
		// to even out the weights for the noisy start instead of giving first samples much more weight than the rest.
		// Ref: Koskela2019, Blockwise Multi-Order Feature Regression for Real-Time Path-Tracing Reconstruction

		value = bValidValue ? lerp(cachedValue, value, a) : cachedValue;

		// Value Squared Mean
		float cachedSquareMean = cachedValueSquareMean;
		valueSquaredMean = bValidValue ? lerp(cachedSquareMean, valueSquaredMean, a) : cachedSquareMean;

		// Variance
		valueLuma = Luminance(value);
		float temporalVariance = valueSquaredMean - valueLuma * valueLuma;
		temporalVariance = max(0, temporalVariance); // ensure variance doesn't go negative due to imprecision
		variance = tspp >= _DenoisingCommonParams.MinTsppToUseTemporalVariance ? temporalVariance : localVariance;
		variance = max(0.1, variance);
	}
	else if (bValidValue)
	{
		tspp = 1;
		variance = _CurrentFrameMeanVariance[dtid].y;
	}

	float tsppRadio = min(tspp, _DenoisingCommonParams.BlurStrength_MaxTspp) / float(_DenoisingCommonParams.BlurStrength_MaxTspp);
	float blurStrength = pow(1.0 - tsppRadio, _DenoisingCommonParams.BlurDecayStrength);

	Output[dtid] = variance;
	RWColorBuffer[dtid] = float4(value, currentColor.a);
	RWLumaMoments[dtid] = float2(valueLuma, valueSquaredMean);
	RWTspp[dtid] = tspp;
}
