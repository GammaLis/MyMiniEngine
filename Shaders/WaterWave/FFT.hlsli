#ifndef FFT_HLSLI
#define FFT_HLSLI

#include "WaterWaveRS.hlsli"

cbuffer CSConstants	: register(b0)
{
	uint _N;
};

Texture2D<float2> _InputSpectrum	: register(t0);
Texture2D<float2> _ButterflyTexture	: register(t1);

RWTexture2D<float2> Output	: register(u0);

groupshared float2 sh_Buffer0[Precision];	// Ping-pong buffers
groupshared float2 sh_Buffer1[Precision];

void ButterflyPass(float2 weight, uint tIndex, uint passIndex)
{
	uint indexA;
	uint indexB;
	uint offset = 1 << passIndex;
	if ((tIndex / offset) & 1)	// 奇 部分
	{
		indexA = tIndex - offset;
		indexB = tIndex;
	}
	else
	{
		indexA = tIndex;
		indexB = tIndex + offset;
	}

	// uint nBit = ceil(log2(Precision));	// 直接改成 static const 变量
	if (passIndex == 0)	// 初始pass， bit逆序排列
	{
		indexA = reversebits(indexA) >> (32 - c_nBit);
		indexB = reversebits(indexB) >> (32 - c_nBit);
	}

	float2 valueA;
	float2 valueB;
	if (passIndex & 1)
	{
		valueA = sh_Buffer1[indexA];
		valueB = sh_Buffer1[indexB];
	}
	else
	{
		valueA = sh_Buffer0[indexA];
		valueB = sh_Buffer0[indexB];
	}

	valueB = ComplexMultiply(weight, valueB);
	if (passIndex == c_nBit - 1)	// 最后一个pass 特殊处理
	{
		valueB *= -1;
	}

	float2 value = valueA + valueB;

	if (passIndex & 1)
	{
		sh_Buffer0[tIndex] = value;
	}
	else
	{
		sh_Buffer1[tIndex] = value;
	}

}

float2 FinalResult(int2 tid, float2 val)
{
	float2 res = val * (1.0f / _N);
#ifdef FFT_HORIZONTAL
	float s = (tid.x % 2) ? -1.0f : 1.0f;
	res *= s;
#else
	float s = (tid.y % 2) ? -1.0f : 1.0f;
	res *= s;
#endif
	return res;
}

[RootSignature(RootSig_WaterWave)]
[numthreads(GroupSizeX, GroupSizeY, 1)]
void main(uint3 dtid : SV_DispatchThreadID, uint gtIndex : SV_GroupIndex)
{
	// 避免传入数据出错
	// if (gtIndex >= _N)
	// 	return;
	// 结合下面，不能进行if判断

	sh_Buffer0[gtIndex] = _InputSpectrum[dtid.xy];
	GroupMemoryBarrierWithGroupSync();
	// Error: 这条语句其实是在 If语句里面，等待所有threads同步，但是可能永远无法等到（if语句可能只有部分threads激活）

	uint nPass = ceil(log2(_N));
	for (uint i = 0; i < nPass; ++i)
	{
		float2 weight = _ButterflyTexture[uint2(gtIndex, i)];
		ButterflyPass(weight, gtIndex, i);

		GroupMemoryBarrierWithGroupSync();
	}

	if (nPass & 1)
	{
		float2 butterflyResult = sh_Buffer1[gtIndex];
		int2 tid = int2(dtid.xy) - int(_N / 2);
		Output[dtid.xy] = FinalResult(tid, butterflyResult);
	}
	else
	{
		float2 butterflyResult = sh_Buffer0[gtIndex];
		int2 tid = int2(dtid.xy) - int(_N / 2);
		Output[dtid.xy] = FinalResult(tid, butterflyResult);
	}

}

#endif	// FFT_HLSLI

/**
 * 	GroupMemoryBarrierWithGroupSync();
	GroupMemoryBarrier();

	https://www.gamedev.net/forums/topic/580937-d3d11-compute-shader-memory-barriers/
 *	MemoryBarrier are useful as they guarantee all access to a memory is completed and is thus
 * visible to other threads. This is because although we may say update a particular X value,
 * it does not necessary means that when other thread attempt to read it, the value is already
 * updated. As the GPU may queue the write process or deferred it slightly later. What MemoryBarrier
 * does is then ensure that all updates to the memory are done before we access it.
 * 	The benefit of MemoryBarrier is that it only ensure a memory barrier. Hence only access to a memory
 * needs to be synchronize, but not the actual thread instructions.
 */

/**
 * 	https://github.com/speps/GX-EncinoWaves
 */
