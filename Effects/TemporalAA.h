#pragma once
#include "pch.h"
#include "RootSignature.h"
#include "PipelineState.h"

namespace MyDirectX
{
	class CommandContext;
	class ComputeContext;
	class ColorBuffer;

	class TemporalAA
	{
	public:
		void Init(ID3D12Device *pDevice);

		void Shutdown();

		// call once per frame to increment the internal frame counter and, in the case of TAA, 
		// choosing the next jittered sample position
		void Update(uint64_t frameIndex);

		// returns whether the frame is odd or even
		uint32_t GetFrameIndexMod2() const { return m_FrameIndexMod2; }

		// jitter values are neutral at 0.5 and vary from [0, 1). Jittering only occurs when temporal antialiasing
		// is enabled. You can use these values to jitter your viewport or projection matrix.
		void GetJitterOffset(float& jitterX, float& jitterY) const
		{
			jitterX = m_JitterX;
			jitterY = m_JitterY;
		}

		void ClearHistory(CommandContext& context);

		void ResolveImage(CommandContext& context);

		// options
		bool m_Enabled = true;
		float m_Sharpness = 0.5f;			// sharpness 0.0f - 1.0f
		float m_TemporalMaxLerp = 1.0f;		// blend factor 0.0f - 1.0f
		float m_TemporalSpeedLimit = 64.0f;	// speed limit 1.0f - 1024.0f
		bool m_Reset = false;

	private:
		void ApplyTemporalAA(ComputeContext& context);
		void SharpenImage(ComputeContext& context, ColorBuffer& temporalColor);

		// root signature
		RootSignature m_RootSignature;
		// PSOs
		ComputePSO m_TemporalBlendCS;
		ComputePSO m_BoundNeighborhoodCS;
		ComputePSO m_SharpenTAACS;
		ComputePSO m_ResolveTAACS;

		uint32_t m_FrameIndex = 0;
		uint32_t m_FrameIndexMod2 = 0;
		float m_JitterX = 0.5f;
		float m_JitterY = 0.5f;
		float m_JitterDeltaX = 0.0f;
		float m_JitterDeltaY = 0.0f;
	};

}