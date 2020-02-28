#pragma once
#include "pch.h"
#include "ParticleEffect.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "GpuResource.h"
#include "Math/Random.h"
#include <mutex>

namespace Math
{
	class Camera;
}

namespace MyDirectX
{
	class CommandContext;
	class ComputeContext;
	class GraphicsContext;

	class ColorBuffer;
	class DepthBuffer;

	namespace ParticleEffects
	{
		enum class ParticleResolutions
		{
			kHigh,
			kLow,
			kDynamic
		};

		extern RandomNumberGenerator s_RNG;

		class ParticleEffectManager
		{
			friend ParticleEffect;

		public:
			void Init(ID3D12Device* pDevice, uint32_t maxDisplayWidth, uint32_t maxDisplayHeight);
			void Shutdown();
			void ClearAll();

			using EffectHandle = uint32_t;
			EffectHandle PreLoadEffectResources(ParticleEffectProperties& effectProperties);
			EffectHandle InstantiateEffect(EffectHandle effectHandle);
			EffectHandle InstantiateEffect(ParticleEffectProperties& effectProperties);

			void Update(ComputeContext& context, float deltaTime);
			void Render(CommandContext& context, const Math::Camera& camera,
				ColorBuffer& colorTarget, DepthBuffer& depthBuffer, ColorBuffer& linearDepth);
			void ResetEffect(EffectHandle effectId);
			float GetCurrentLife(EffectHandle effectId);

			// properties
			bool m_Enabled = true;			
			bool m_EnableSpriteSort = false;	// 暂不排序
			bool m_EnableTiledRendering = false;
			bool m_PauseSim = false;

			float m_DynamicResLevel = 0.0f;	// dynamic resolution cutoff [-4.0f, 4.0f]
			float m_MipBias = 0.0f;			// mip bias [-4.0f, 4.0f]

			bool m_Reproducible = false;	// if you want to repro set to true. When true, effect uses the same set of random numbers each run
			UINT m_ReproFrame = 0;	

		private:
			void MaintainTextureList(ParticleEffectProperties& effectProperties);
			void SetFinalBuffers(ComputeContext& computeContext);

			void RenderSprites(GraphicsContext& gfxContext, ColorBuffer& colorTarget, DepthBuffer& depthTarget, ColorBuffer& linearDepth);
			void RenderTiles(ComputeContext& computeContext, ColorBuffer& colorTarget, ColorBuffer& linearDepth);

			std::mutex m_TextureMutex;
			std::mutex m_InstantiateNewEffectMutex;
			std::mutex m_InstantiateEffectFromPoolMutex;
			std::mutex m_EraseEffectMutex;

			std::vector<std::unique_ptr<ParticleEffect>> m_ParticleEffectsPool;
			std::vector<ParticleEffect*> m_ActiveParticleEffects;
			std::vector<std::wstring> m_TextureNameArray;

			// cached values
			ID3D12Device* m_Device;

			// rootsignature
			RootSignature m_ParticleRS;
			// PSOs
			// 粒子生成，更新
			ComputePSO m_ParticleSpawnCS;
			ComputePSO m_ParticleUpdateCS;
			ComputePSO m_ParticleDispatchIndirectArgsCS;

			ComputePSO m_ParticleFinalDispatchIndirectArgsCS;
			GraphicsPSO m_NoTileRasterizationPSO[2];

			bool m_InitCompleted = false;

			// resources
			GpuResource m_TextureArray;
			D3D12_CPU_DESCRIPTOR_HANDLE m_TexArraySRV;

			StructuredBuffer m_SpriteVertexBuffer;

			StructuredBuffer m_SpriteIndexBuffer;

			IndirectArgsBuffer m_DrawIndirectArgs;
			IndirectArgsBuffer m_FinalDispatchIndirectArgs;

			UINT m_TotalElapsedFrames = 0;

		};
	}
}
