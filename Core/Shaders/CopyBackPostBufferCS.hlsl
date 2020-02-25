#include "PostEffectsRS.hlsli"
#include "PixelPacking.hlsli"

Texture2D<uint> _PostBuffer	: register(t0);

RWTexture2D<float3> sceneColor	: register(u0);

[RootSignature(PostEffects_RootSig)]
[numthreads(8, 8, 1)]
void main( uint3 dtid : SV_DispatchThreadID )
{
	sceneColor[dtid.xy] = Unpack_R11G11B10_FLOAT(_PostBuffer[dtid.xy]);
}
