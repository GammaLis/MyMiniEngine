#include "ParticleUpdateCommon.hlsli"
#include "ParticleUtility.hlsli"

Texture2DArray<float4> _ColorTex	: register(t2);
Texture2D<float> _LinearDepthTex	: register(t3);

[RootSignature(Particle_RootSig)]
float4 main(ParticleVSOutput i) : SV_TARGET
{
	float3 uv = float3(i.uv.xy, i.texId);
	float4 texColor = _ColorTex.Sample(s_LinearBorderSampler, uv);
	// reversed linear Z (z / FarClip [n/f, 1])
	texColor.a *= saturate(1000.0 * (_LinearDepthTex[(uint2)i.pos.xy] - i.linearZ));
	texColor.rgb *= texColor.a;

	//for debug
	//return float4(1.0, 0.0, 0.0, 1.0);
	
	return texColor * i.color;
}
