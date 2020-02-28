#include "ParticleUtility.hlsli"

ByteAddressBuffer _FinalInstanceCounter	: register(t0);

RWByteAddressBuffer NumThreadGroups		: register(u0);
RWByteAddressBuffer DrawIndirectArgs	: register(u1);

[RootSignature(Particle_RootSig)]
[numthreads(1, 1, 1)]
void main( uint3 dtid : SV_DispatchThreadID )
{
	uint particleCount = _FinalInstanceCounter.Load(0);
	NumThreadGroups.Store3(0, uint3((particleCount + 63) / 64, 1, 1));
	DrawIndirectArgs.Store(4, particleCount);
}
