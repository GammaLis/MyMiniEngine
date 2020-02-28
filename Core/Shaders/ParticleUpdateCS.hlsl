#include "ParticleUpdateCommon.hlsli"
#include "ParticleUtility.hlsli"

cbuffer CSConstants	: register(b0)
{
	float _ElapsedTime;	// delta time 间隔时间
};

StructuredBuffer<ParticleSpawnData> _ResetData	: register(t0);
StructuredBuffer<ParticleMotion> _InputBuffer	: register(t1);

RWStructuredBuffer<ParticleVertex> VertexBuffer	: register(u0);
RWStructuredBuffer<ParticleMotion> OutputBuffer	: register(u2);

[RootSignature(Particle_RootSig)]
[numthreads(64, 1, 1)]
void main( uint3 dtid : SV_DispatchThreadID )
{
	if (dtid.x >= _MaxParticles)
		return;

	ParticleMotion particleState = _InputBuffer[dtid.x];
	ParticleSpawnData rd = _ResetData[particleState.resetDataIndex];

	// update age. If normalized age exceeds 1, the particle does not renew its lease on life
	particleState.age += _ElapsedTime * rd.ageRate;	// ageRate - 1/LifeTime
	if (particleState.age >= 1.0)
		return;

	// update position. Compute 2 deltas to support rebounding off the ground plane
	float stepSize = (particleState.position.y > 0.0 && particleState.velocity.y < 0.0) ?
		min(_ElapsedTime, particleState.position.y / -particleState.velocity.y) : _ElapsedTime;
	particleState.position += particleState.velocity * stepSize;
	particleState.velocity += _Gravity * particleState.mass * stepSize;

	// rebound off the ground if we didn't consume all of the elapsed time
	// 反弹
	stepSize = _ElapsedTime - stepSize;
	if (stepSize > 0.0)
	{
		particleState.velocity = reflect(particleState.velocity, float3(0, 1, 0)) * _Restitution;
		particleState.position += particleState.velocity * stepSize;
		particleState.velocity += _Gravity * particleState.mass * stepSize;
	}

	// ??? 为什么是添加，不是直接更新对应位置
	// the spawn dispatch will be simultaneously adding particles as well. It's possible to overflow
	uint index = OutputBuffer.IncrementCounter();
	if (index >= _MaxParticles)
		return;

	OutputBuffer[index] = particleState;

	/// 
	/// generate a sprite vertex
	/// 
	ParticleVertex spriteVertex;
	spriteVertex.position = particleState.position;
	spriteVertex.textureId = _TextureID;

	// update size and color
	spriteVertex.size = lerp(rd.startSize, rd.endSize, particleState.age);
	spriteVertex.color = lerp(rd.startColor, rd.endColor, particleState.age);

	// ...originally from Reflex...
	// use a trinomial(三项式) to smoothly fade in a particle at birth and fade it out at deate
	spriteVertex.color *= particleState.age * (1.0 - particleState.age) * (1.0 - particleState.age) * 6.7;

	VertexBuffer[VertexBuffer.IncrementCounter()] = spriteVertex;
}
