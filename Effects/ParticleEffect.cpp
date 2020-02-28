#include "ParticleEffect.h"
#include "ParticleEffectManager.h"
#include "Graphics.h"
#include "GfxCommon.h"
#include "CommandContext.h"
#include "Math/Random.h"
#include "Effects.h"

using namespace MyDirectX::ParticleEffects;
using namespace MyDirectX;

inline static Color RandColor(Color c0, Color c1)
{
	Color minColor = Min(c0, c1);
	Color maxColor = Max(c0, c1);
	return Color(
		s_RNG.NextFloat(c0.R(), c1.R()),
		s_RNG.NextFloat(c0.G(), c1.G()),
		s_RNG.NextFloat(c0.B(), c1.B()),
		s_RNG.NextFloat(c0.A(), c1.A())
	);
}

inline static XMFLOAT3 RandSpread(const XMFLOAT3& s)
{
	return XMFLOAT3(
		s_RNG.NextFloat(-s.x, s.x),
		s_RNG.NextFloat(-s.y, s.y),
		s_RNG.NextFloat(-s.z, s.z)
	);
}

void ParticleEffect::LoadDeviceResources(ID3D12Device* pDevice)
{
	(pDevice);

	m_OriginalEffectProperties = m_EffectProperties;	// in case we want to reset

	// fill particle spawn data buffer
	// 栈上或者堆上分配内存（区分_alloca）,需要显式调用_freea
	ParticleSpawnData* pSpawnData = (ParticleSpawnData*)_malloca(m_EffectProperties.EmitProperties.MaxParticles * sizeof(ParticleSpawnData));
	for (uint32_t i = 0, imax = m_EffectProperties.EmitProperties.MaxParticles;
		i < imax; ++i)
	{
		ParticleSpawnData& spawnData = pSpawnData[i];
		// 生命周期 倒数
		spawnData.AgeRate = 1.0f / s_RNG.NextFloat(m_EffectProperties.LifeMinMax.x, m_EffectProperties.LifeMinMax.y);
		float horizontalAngle = s_RNG.NextFloat(XM_2PI);
		// 速度
		float horizontalVelocity = s_RNG.NextFloat(m_EffectProperties.Velocity.GetX(), m_EffectProperties.Velocity.GetY());
		spawnData.Velocity.x = horizontalVelocity * cos(horizontalAngle);
		spawnData.Velocity.y = s_RNG.NextFloat(m_EffectProperties.Velocity.GetZ(), m_EffectProperties.Velocity.GetW());
		spawnData.Velocity.z = horizontalVelocity * sin(horizontalAngle);
		// spreadOffset
		spawnData.SpreadOffset = RandSpread(m_EffectProperties.Spread);

		// size
		spawnData.StartSize = s_RNG.NextFloat(m_EffectProperties.Size.GetX(), m_EffectProperties.Size.GetY());
		spawnData.EndSize = s_RNG.NextFloat(m_EffectProperties.Size.GetZ(), m_EffectProperties.Size.GetW());
		// color
		spawnData.StartColor = RandColor(m_EffectProperties.MinStartColor, m_EffectProperties.MaxStartColor);
		spawnData.EndColor = RandColor(m_EffectProperties.MinEndColor, m_EffectProperties.MaxEndColor);
		// mass
		spawnData.Mass = s_RNG.NextFloat(m_EffectProperties.MassMinMax.x, m_EffectProperties.MassMinMax.y);
		// rotation speed
		spawnData.RotationSpeed = s_RNG.NextFloat();	// 暂时没用
		// random
		spawnData.Random = s_RNG.NextFloat();	
	}

	m_RandomStateBuffer.Create(pDevice, L"ParticleSystem::SpawnDataBuffer",
		m_EffectProperties.EmitProperties.MaxParticles, sizeof(ParticleSpawnData), pSpawnData);
	_freea(pSpawnData);	// _malloca 需要显式调用，_alloca不用

	m_StateBuffers[0].Create(pDevice, L"ParticleSystem::Buffer0",
		m_EffectProperties.EmitProperties.MaxParticles, sizeof(ParticleMotion));
	m_StateBuffers[1].Create(pDevice, L"ParticleSystem::Buffer1",
		m_EffectProperties.EmitProperties.MaxParticles, sizeof(ParticleMotion));
	m_CurStateBuffer = 0;

	// DispatchIndirect args buffer / number of thread groups
	// _declspec(align(16)) 
	alignas(16) UINT dispatchIndirectData[3] = { 0, 1, 1 };
	m_DispatchIndirectArgs.Create(pDevice, L"ParticleSystem::DispatchIndirectArgs", 1, sizeof(D3D12_DISPATCH_ARGUMENTS), dispatchIndirectData);
}

void ParticleEffect::Update(ComputeContext& computeContext, float deltaTime)
{
	m_ElapsedTime += deltaTime;
	m_EffectProperties.EmitProperties.LastEmitPosW = m_EffectProperties.EmitProperties.EmitPosW;

	// CPU side random num gen
	for (uint32_t i = 0; i < 64; ++i)
	{
		uint32_t random = s_RNG.NextUint(m_EffectProperties.EmitProperties.MaxParticles - 1);
		m_EffectProperties.EmitProperties.RandIndex[i].x = random;
	}
	computeContext.SetDynamicConstantBufferView(2, sizeof(EmissionProperties), &m_EffectProperties.EmitProperties);

	auto& particleEffectManager = Effects::s_ParticleEffectManager;
	// particle update
	{
		uint32_t curBufferIndex = m_CurStateBuffer, updateBufferIndex = m_CurStateBuffer ^ 1;
		
		// 重置计数器，会改变m_StateBuffers[updateBufferIndex] ResourceState
		computeContext.ResetCounter(m_StateBuffers[updateBufferIndex]);

		computeContext.TransitionResource(m_StateBuffers[curBufferIndex], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(m_StateBuffers[updateBufferIndex], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		computeContext.TransitionResource(m_DispatchIndirectArgs, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);		

		computeContext.SetPipelineState(particleEffectManager.m_ParticleUpdateCS);
		computeContext.SetDynamicDescriptor(3, 0, m_RandomStateBuffer.GetSRV());
		computeContext.SetDynamicDescriptor(3, 1, m_StateBuffers[curBufferIndex].GetSRV());
		computeContext.SetDynamicDescriptor(4, 2, m_StateBuffers[updateBufferIndex].GetUAV());
		computeContext.DispatchIndirect(m_DispatchIndirectArgs, 0);		// dispatch args = (x, y, z)

		m_CurStateBuffer = updateBufferIndex;
	}

	/**
		Why need a barrier here so long as we are artificially clamping particle count
		This allows living particles to take precedence over new particles. The current system always spawns
	a multiple of 64 particles (To Be Fixed) until the total particle count reaches maximum
	*/
	computeContext.InsertUAVBarrier(m_StateBuffers[m_CurStateBuffer]);

	// spawn to replace dead ones
	{
		computeContext.SetPipelineState(particleEffectManager.m_ParticleSpawnCS);
		// computeContext.SetDynamicDescriptor(3, 0, m_RandomStateBuffer.GetSRV());
		uint32_t numSpawnThreads = (uint32_t)(m_EffectProperties.EmitRate * deltaTime);
		if (numSpawnThreads == 0)
			++numSpawnThreads;
		computeContext.Dispatch((numSpawnThreads + 63) / 64, 1, 1);
	}

	// output number of thread groups into m_DispatchIndirectArgs
	{
		computeContext.TransitionResource(m_DispatchIndirectArgs, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		computeContext.TransitionResource(m_StateBuffers[m_CurStateBuffer], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		computeContext.SetPipelineState(particleEffectManager.m_ParticleDispatchIndirectArgsCS);

		computeContext.SetDynamicDescriptor(3, 0, m_StateBuffers[m_CurStateBuffer].GetCounterSRV(computeContext));
		computeContext.SetDynamicDescriptor(4, 1, m_DispatchIndirectArgs.GetUAV());
		computeContext.Dispatch(1, 1, 1);
	}
}

void ParticleEffect::Reset()
{
	m_EffectProperties = m_OriginalEffectProperties;
}

/**
	https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/malloca?view=vs-2019
	>> _malloca
	allocates memory on the stack. This is a version of _alloca with security enhancements.
	the _malloca routine returns a void pointer to the allocated space, which is guaranteed to be suitably
aligned for storage of any type of object. If size is 0, _malloca allocates a 0-length item and returns 
a valid pointer to that item.
	If size is greater than _ALLOCA_S_THRESHOLD, then _malloca attempts to allocate on the heap, and 
returns a null pointer if the space can't be allocated. If size is less than or equal to _ALLOCA_S_THRESHOLD,
then _malloca attempts to allocate on the stack, and a stack overflow exception is generated if the space
can't be allocated. The stack overflow exception isn't a C++ exception; it's a structured exception. 
	Remarks:
	_malloca allocates size bytes from the program stack or the heap if the request exceeds a certain size
in bytes given by _ALLOCA_S_THRESHOLD. The difference between _malloca and _alloca is that _alloca always
allocates on the stack, regardless of the size. Unlike _alloca, which does not require or permit a call 
to free to free the memory so allocated, _malloca requires the use of _freea to free memory. In debug mode,
_malloca always allocates memory from the heap.

*/

/**
	https://docs.microsoft.com/en-us/cpp/cpp/align-cpp?view=vs-2019
	align(C++)
	in Visual Studio 2015 and later, use the C++ 11 standard alignas specifier to control alignment,
	Microsoft Specific
	use __declspec(align(#)) to precisely control the alignment of user-defined data
	...
	you can use __declspec(align(#)) when you define a struct, union, or class, or when you declare a variable

	#define CACHE_LINE  32
	#define CACHE_ALIGN __declspec(align(CACHE_LINE))
	the following 3 variable declarations also use __declspec(align(#)). in each case, the variable must be
32-byte aligned. In the case of the array, the base address of the array, not each array member, is 32-byte
aligned. The "sizeof" value for each array member is not affected when you use __declspec(align(#))
	CACHE_ALIGN int i;
	CACHE_ALIGN int array[128];
	CACHE_ALIGN struct s2 s;
*/
