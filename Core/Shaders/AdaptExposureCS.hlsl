#include "PostEffectsRS.hlsli"
#include "ShaderUtility.hlsli"

cbuffer CSConstants	: register(b1)
{
	float _TargetLuminance;
	float _AdaptationRate;
	float _MinExposure;
	float _MaxExposure;
	uint _PixelCount;
};

ByteAddressBuffer _Histogram	: register(t0);

RWStructuredBuffer<float> Exposure 	: register(u0);

groupshared float sh_Accum[256];

[RootSignature(PostEffects_RootSig)]
[numthreads(256, 1, 1)]
void main( uint gindex : SV_GroupIndex )
{
	float weightedSum = (float)gindex * (float)_Histogram.Load(gindex * 4);

	// 计算整体luma - index * Histogram[index]
	// I.
	[unroll]
	for (uint i = 1; i < 256; i *= 2)
	{
		sh_Accum[gindex] = weightedSum;		// write
		GroupMemoryBarrierWithGroupSync();	// sync
		weightedSum += sh_Accum[(gindex + i) % 256];	// read                              
		// GroupMemoryBarrierWithGroupSync();	// sync 	这次同步感觉不需要?? -20-2-25
	}

	// II. -mf
	// sh_Accum[gindex] = weightedSum;		// write
	// GroupMemoryBarrierWithGroupSync();	// sync
	// for (uint i = 1; i < 256; i *= 2)
	// {
	// 	if ((gindex & (2 * i - 1)) == 0) 	// gindex % (2 & i) == 0
	// 	{
	// 		sh_Accum[gindex] += sh_Accum[gindex + i];
	// 		// GroupMemoryBarrierWithGroupSync();	// 在有分支处，不能在分支里面同步（非活动分支没有执行，永远无法同步！）
	// 	}
	// 	GroupMemoryBarrierWithGroupSync();
	// }
	// weightedSum = sh_Accum[0];

	float minLog = Exposure[4];
	float maxLog = Exposure[5];
	float logRange = Exposure[6];
	float rcpLogRange = Exposure[7];

	// average histogram value is the weighted sum of all pixels divided by 
	// the total number of pixels minus those pixels which provided no weight (i.e. black pixels)
	float weightedHistAvg = weightedSum / (max(1, _PixelCount - _Histogram.Load(0))) - 1.0;	// [1, 255] - 1 -> [0, 254]
	float logAvgLuminance = exp2(weightedHistAvg / 254.0 * logRange + minLog);
	float targetExposure = _TargetLuminance / logAvgLuminance;
	// float targetExposure = -log2(1 - _TargetLuminance) / logAvgLuminance;
	
	float exposure = Exposure[0];
	exposure = lerp(exposure, targetExposure, _AdaptationRate);
	exposure = clamp(exposure, _MinExposure, _MaxExposure);

	if (gindex == 0)
	{
		Exposure[0] = exposure;
		Exposure[1] = 1.0 / exposure;
		Exposure[2] = exposure;
		Exposure[3] = weightedHistAvg;

		// first attempt to recenter our histogram around the log-average
		float biasToCenter = (floor(weightedHistAvg) - 128.0) / 255.0;
		if (abs(biasToCenter) > 0.1)
		{
			minLog += biasToCenter * rcpLogRange;
			maxLog += biasToCenter * rcpLogRange;
		}

		// TODO: increase or decrease the log range to better fit the range of values
		// (idea) look at intermediate log-weighted sums for under- or over-represented
		// extreme bounds. i.e. break the for loop into 2 pieces to compute the sum of
		// groups of 16, check the groups on each end, then finish the recursive sum
		
		Exposure[4] = minLog;
		Exposure[5] = maxLog;
		Exposure[6] = logRange;
		Exposure[7] = 1.0 / logRange;
	}	

}
