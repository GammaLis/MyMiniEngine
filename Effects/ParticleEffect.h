#pragma once
#include "pch.h"
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
			StructuredBuffer m_StateBuffers[2];		// 前后 状态buffer（当前运动状态，更新运动状态）
			uint32_t m_CurStateBuffer;
			StructuredBuffer m_RandomStateBuffer;	// 初始随机生成状态buffer
			IndirectArgsBuffer m_DispatchIndirectArgs;
			IndirectArgsBuffer m_DrawIndirectArgs;

			ParticleEffectProperties m_EffectProperties;
			ParticleEffectProperties m_OriginalEffectProperties;	// for reset
			float m_ElapsedTime = 0.0f;
			UINT m_EffectID = 0;
		};
	}
}

