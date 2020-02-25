#include "PostEffectsRS.hlsli"
#include "ToneMappingUtility.hlsli"
#include "PixelPacking.hlsli"

cbuffer CSConstants	: register(b0)
{
	float2 _InverseOutputSize;
	float _BloomStrength;
};

StructuredBuffer<float> _Exposure	: register(t0);
Texture2D<float3> _Bloom 	: register(t1);
#if SUPPORT_TYPED_UAV_LOADS
RWTexture2D<float3> rwColor	: register(u0);
#else
Texture2D<float3> srcColor	: register(t2);
RWTexture2D<float3> dstColor: register(u0);
#endif

RWTexture2D<float> outLuma	: register(u1);

SamplerState s_LinearSampler: register(s0);

[RootSignature(PostEffects_RootSig)]
[numthreads(8, 8, 1)]
void main( uint3 dtid : SV_DispatchThreadID )
{
	float2 uv = (float2(dtid.xy) + 0.5 ) * _InverseOutputSize;

	// load HDR and bloom
#if SUPPORT_TYPED_UAV_LOADS
	float3 hdrColor = rwColor[dtid.xy];
#else
	float3 hdrColor = srcColor[dtid.xy];
#endif

	hdrColor += _BloomStrength * _Bloom.SampleLevel(s_LinearSampler, uv, 0);
	hdrColor *= _Exposure[0];

#if ENABLE_HDR_DISPLAY_MAPPING
	
	// write the HDR color as-is and defer display mapping until we composite with UI
#if SUPPORT_TYPED_UAV_LOADS
	rwColor[dtid.xy] = hdrColor;
#else
	dstColor[dtid.xy] = Pack_R11G11B10_FLOAT(hdrColor);
#endif	//  SUPPORT_TYPED_UAV_LOADS
	outLuma[dtid.xy] = LinearToLogLuminance(ToneMapLuma(RGBToLuminance(hdrColor)));

#else
	
	// tone map to SDR
	float3 sdrColor = TM_Stanard(hdrColor);
#if SUPPORT_TYPED_UAV_LOADS
	rwColor[dtid.xy] = sdrColor;
#else
	dstColor[dtid.xy] = Pack_R11G11B10_FLOAT(sdrColor);
#endif	//  SUPPORT_TYPED_UAV_LOADS
	outLuma[dtid.xy] = RGBToLogLuminance(sdrColor);

#endif	// ENABLE_HDR_DISPLAY_MAPPING
}
