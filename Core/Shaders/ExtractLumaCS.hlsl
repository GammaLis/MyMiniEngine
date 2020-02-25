// The CS for extracting bright pixels and saving a log-luminance map (quantized to 8 bits).  
// This is then used to generate an 8-bit histogram.
#include "PostEffectsRS.hlsli"
#include "ShaderUtility.hlsli"

cbuffer CSConstants	: register(b0)
{
	float2 _InverseOutputSize;
};

Texture2D<float3> _TexColor		: register(t0);
StructuredBuffer<float> _Exposure	: register(t1);

RWTexture2D<uint> lumaResult	: register(u0);

SamplerState s_BilinearSampler	: register(s0);

[RootSignature(PostEffects_RootSig)]
[numthreads(8, 8, 1)]
void main( uint3 dtid : SV_DispatchThreadID )
{
	// we need the scale factor and the size of one pixel so that our 4 samples
	// are right in the middle of the quadrant they are covering
	float2 uv = dtid.xy * _InverseOutputSize;
	float2 offset = _InverseOutputSize * 0.25;
	// 1x ->| 0.25 | 0.25 |
	// 2x ->| 0.5  | 0.5  |	间距一个像素

	// use 4 bilinear samples to guarantee we don't undersample when downsizing by
	// more than 2x 
	float3 color1 = _TexColor.SampleLevel(s_BilinearSampler, uv + float2(-offset.x, -offset.y), 0);
	float3 color2 = _TexColor.SampleLevel(s_BilinearSampler, uv + float2( offset.x, -offset.y), 0);
	float3 color3 = _TexColor.SampleLevel(s_BilinearSampler, uv + float2(-offset.x,  offset.y), 0);
	float3 color4 = _TexColor.SampleLevel(s_BilinearSampler, uv + float2( offset.x,  offset.y), 0);

	// compute average luminance
	float luma = RGBToLuminance(color1 + color2 + color3 + color4) * 0.25;

	// prevent log(0) and put only pure black pixels in Histogram[0]
	if (luma == 0.0)
	{
		lumaResult[dtid.xy]	 = 0;
	}
	else
	{
		const float MinLog = _Exposure[4];
		const float RcpLogRange = _Exposure[7];
		float logLuma = saturate((log2(luma) - MinLog) * RcpLogRange);	// rescale to [0.0, 1.0]
		lumaResult[dtid.xy] = logLuma * 254.0 + 1.0;
	}
}
