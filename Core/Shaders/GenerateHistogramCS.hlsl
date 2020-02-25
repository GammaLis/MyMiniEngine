// The group size is 16x16, but one group iterates over an entire 16-wide column of pixels
// (384 pixels tall).	Assuming the total workspace is 640x384, there will be 40 thread groups
// computing the histogram in parallel.
// The histogram measures logarithmic luminance ranging from 2^-12 up to 2^4.  
// This should provide a nice window where the exposure would range from 2^-4 up to 2^4.
#include "PostEffectsRS.hlsli"

// logLuma [1.0, 255.0]
Texture2D<uint> _LumaBuffer		: register(t0);

RWByteAddressBuffer Histogram	: register(u0);

groupshared uint sh_TileHistogram[256];

[RootSignature(PostEffects_RootSig)]
[numthreads(16, 16, 1)]
void main( uint3 dtid : SV_DispatchThreadID, uint gindex : SV_GroupIndex)
{
	sh_TileHistogram[gindex] = 0;

	GroupMemoryBarrierWithGroupSync();

	// loop 24 times until the entire column has been processed
	for (uint y = 0; y < 384; y += 16)
	{
		uint quantizedLogLuma = _LumaBuffer[dtid.xy + uint2(0, y)];
		InterlockedAdd(sh_TileHistogram[quantizedLogLuma], 1);
	}

	GroupMemoryBarrierWithGroupSync();

	Histogram.InterlockedAdd(gindex * 4, sh_TileHistogram[gindex]);
}
