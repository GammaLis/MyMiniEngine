#pragma once
#include "CoreMinimal.h"
#include "GpuBuffer.h"
#include "ParticleShaderStructs.h"
#include "ParticleEffectProperties.h"

namespace Math
{
	class RandomNumberGenerator;
}

namespace MyDirectX
{
	class ComputeContext;

	namespace ParticleEffects
	{
		using namespace Math;

		class ParticleEffect
		{
		public:
			ParticleEffect(ParticleEffectProperties& effectProperties)
				: m_ElapsedTime(0.0f), m_EffectProperties(effectProperties)
			{  }

			void LoadDeviceResources(ID3D12Device* pDevice);
			void Update(ComputeContext& computeContext, float deltaTime);
			
			float GetLifeTime() { return m_EffectProperties.TotalActiveLifeTime; }
			float GetElapsedTime() { return m_ElapsedTime; }

			void Reset();

		private:
			StructuredBuffer m_StateBuffers[2];		// ǰ�� ״̬buffer����ǰ�˶�״̬�������˶�״̬��
			uint32_t m_CurStateBuffer{0};
			StructuredBuffer m_RandomStateBuffer;	// ��ʼ�������״̬buffer
			IndirectArgsBuffer m_DispatchIndirectArgs;
			IndirectArgsBuffer m_DrawIndirectArgs;

			ParticleEffectProperties m_EffectProperties;
			ParticleEffectProperties m_OriginalEffectProperties;	// for reset
			float m_ElapsedTime = 0.0f;
			UINT m_EffectID = 0;
		};
	}
}

