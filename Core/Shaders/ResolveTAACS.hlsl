#include "TemporalRS.hlsli"

Texture2D<float4> _TemporalColor: register(t0);

RWTexture2D<float3> outColor	: register(u0);

[RootSignature(Temporal_RootSig)]
[numthreads(8, 8, 1)]
void main( uint3 dispatchThreadId : SV_DispatchThreadID )
{
	float4 temporalColor = _TemporalColor[dispatchThreadId.xy];
	outColor[dispatchThreadId.xy] = temporalColor.rgb / max(temporalColor.w, 1e-6);
}