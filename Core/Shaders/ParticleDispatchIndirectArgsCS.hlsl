#include "ParticleRS.hlsli"

ByteAddressBuffer _ParticleInstance	: register(t0);

RWByteAddressBuffer NumThreadGroups : register(u1);

[RootSignature(Particle_RootSig)]
[numthreads(1, 1, 1)]
void main( uint3 dtid : SV_DispatchThreadID )
{
	NumThreadGroups.Store(0, (_ParticleInstance.Load(0) + 63) / 64);
}
