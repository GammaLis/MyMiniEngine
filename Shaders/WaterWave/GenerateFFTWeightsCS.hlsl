#define FFT_HORIZONTAL
#include "WaterWaveRS.hlsli"

RWTexture2D<float2> FFTWeights	: register(u0);

float2 WNK(uint N, uint k)
{
	float x = M_2PI * (float)k / (float)N;
	float cw, sw;
	sincos(x, sw, cw);
	return float2(cw, sw);
}

[numthreads(GroupSizeX, GroupSizeY, 1)]	// Precision * 1 * 1
void main( uint3 dtid : SV_DispatchThreadID )
{
#ifdef FFT_HORIZONTAL
	uint index = dtid.x;
	uint passIndex = dtid.y;
#else
	uint index = dtid.y;
	uint passIndex = dtid.x;
#endif
	uint butterflySize = 1 << (passIndex + 1);	// 每个蝶形单元元素个数
	uint k = index & (butterflySize - 1);	// 余数
	float2 wnk = WNK(butterflySize, k);
	FFTWeights[dtid.xy] = wnk;
}

// 注： W(k + N/2, N) = W(k, N),	 0 <= k < N/2
// 目前计算时没有用到这个性质	-2020-7-30
