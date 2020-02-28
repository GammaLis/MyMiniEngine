#include "ParticleRS.hlsli"
#include "ParticleUpdateCommon.hlsli"

StructuredBuffer<ParticleSpawnData> _ResetData	: register(t0);

RWStructuredBuffer<ParticleMotion> OutputBuffer	: register(u2);

[RootSignature(Particle_RootSig)]
[numthreads(64, 1, 1)]
void main( uint3 dtid : SV_DispatchThreadID )
{
	// UAV Counters, like InterlockedAdd ???
	// like Consume/Append in DX11 ??
	uint index = OutputBuffer.IncrementCounter();
	if (index >= _MaxParticles)
		return;

	uint resetDataIndex = _RandIndex[dtid.x].x;	// _RandIndex - uint[64]
	ParticleSpawnData rd = _ResetData[resetDataIndex];

	float3 emitterVelocity = _EmitPosW - _LastEmitPosW;	// 发射器速度
	float3 randDir = rd.velocity.x * _EmitRightW + rd.velocity.y * _EmitUpW + rd.velocity.z * _EmitDirW;
	float3 newVelocity = emitterVelocity * _EmitterVelocitySensitivity + randDir;
	float3 adjustedPosition = _EmitPosW - emitterVelocity * rd.random + rd.spreadOffset;

	ParticleMotion newParticle;
	newParticle.position = adjustedPosition;
	newParticle.rotation = 0.0;
	newParticle.velocity = newVelocity + _EmitDirW * _EmitSpeed;

	newParticle.mass = rd.mass;
	newParticle.age = 0.0;
	newParticle.resetDataIndex = resetDataIndex;
	OutputBuffer[index] = newParticle;
}

/**
 * https://docs.microsoft.com/en-us/windows/win32/direct3d12/uav-counters
 * UAV Counters
 * 	you can use unordered-access-view (UAV) counters to associate a 32-bit atomic counter
 * with an unordered-access-view (UAV)
 * 	> IncrementCounter, DecrementCounter, Append, Consume
 *
 * Using UAV Counters
 * 		your app is responsible for allocating 32-bits of storage for UAV counters. This storage
 * can be allocated in a different resource as the one that contains data accessible via the UAV
 * 		if pCounterResource is specified in the call to CreateUnorderedAcessView, then
 * there is a counter associated with the UAV. In this case:
 * 		1. StructuredByteStride must be greater than 0;
 * 		2. format must be DXGI_FORMAT_UNKNOWN
 * 		3. the RAW flag must not be set
 * 		4. both of the resource must be buffers
 * 		5. CounterOffsetInBytes must be a multiple of 4 bytes
 * 		6. CounterOffsetInBytes must be within the range of the counter resource
 * 		7. pDesc cannot be NULL
 * 		8. pResource cannot be NULL
 * 		and note the following use cases:
 * 		1. if pCounterResource is not specified, then CounterOffsetInBytes must be 0
 * 		2. if the RAW flag is set then the format must be DXGI_FORMAT_R32_TYPELESS and 
 * the UAV resource must be a buffer
 * 		3. if pCounterResource is not set, then CounterOffsetInBytes must be 0
 * 		4. if the RAW flag is not set and StructuredByteStride = 0, then the format must be
 * a valid UAV format
 *
 * 		during Draw/Dispatch, the counter resource must be in the state D3D12_RESOURCE_STATE_UNORDERED_ACCESS.
 * Also, within a single Draw/Dispatch call, it is invalid for an application to access the same
 * 32-bit memory location via 2 separate UAV counters. 
 * 		If a shader attempts to access the counter of a UAV that does not have an associated 
 * counter, then the debug layer will issue a warning, and a GPU page fault will occur causing
 *  the apps’s device to be removed.	
 *  	UAV counters are supported in all heap types (default, upload, readback).		
 */
