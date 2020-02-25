#include "PostEffectsRS.hlsli"
#include "ShaderUtility.hlsli"
#include "PixelPacking.hlsli"

cbuffer CSConstants	: register(b0)
{
	float2 _RcpBufferDim;
	float _BloomStrength;
};

Texture2D<float3> _Bloom: register(t0);
#if SUPPORT_TYPED_UAV_LOADS
RWTexture2D<float3> srcColor: register(u0);
#else
Texture2D<float3> srcColor 	: register(t1);
RWTexture2D<uint> dstColor	: register(u0);
#endif
RWTexture2D<float> outLuma	: register(u1);

SamplerState s_LinearSampler: register(s0);

[RootSignature(PostEffects_RootSig)]
[numthreads(8, 8, 1)]
void main( 
	uint3 dtid 	: SV_DispatchThreadID,
	uint3 gtid	: SV_GroupThreadID,
	uint3 gid 	: SV_GroupID,
	uint gindx	: SV_GroupIndex)
{
	float2 uv = (dtid.xy + 0.5) * _RcpBufferDim;

	// load LDR and bloom;
	float3 ldrColor = srcColor[dtid.xy] + _BloomStrength * _Bloom.SampleLevel(s_LinearSampler, uv, 0);

#if SUPPORT_TYPED_UAV_LOADS
	srcColor[dtid.xy] = ldrColor;
#else
	dstColor[dtid.xy] = Pack_R11G11B10_FLOAT(ldrColor);
#endif
	outLuma[dtid.xy] = RGBToLogLuminance(ldrColor);
}
