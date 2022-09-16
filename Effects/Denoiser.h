#pragma once
#include "pch.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "ColorBuffer.h"
#include "DynamicUploadBuffer.h"

namespace MyDirectX
{
	class CommandContext;
	class ComputeContext;

	class Denoiser
	{
	public:
		enum class RSId
		{
			ViewUniforms = 0,
			DenosingUniforms,
			CurrentInputs,
			HistoryInputs,
			Outputs,

			Num
		};
		static bool s_bEnabled;

		void Init(ID3D12Device* pDevice);
		void Shutdown();

		void Update(uint64_t frameIndex);
		void Render(CommandContext &context);

		bool HasInited() const { return m_bInited; }
		bool IsActive() const { return s_bEnabled && m_bActive; }
		void SetActive(bool bActive);
		D3D12_CPU_DESCRIPTOR_HANDLE GetDebugOutput() const;

	private:
		void TemporalSupersamplingReverseReproject(ComputeContext& context);
		void TemporalSupersamplingBlendWithCurrentFrame(ComputeContext &context);
		void CalculateLocalMeanVariance(ComputeContext& context);
		void ApplyAtrouWaveletFilter(ComputeContext& context);

		RootSignature m_RootSig;

		ComputePSO m_TemporalReprojectPSO{ L"Temporal Reproject PSO" };
		ComputePSO m_CalculdateMeanVariancePSO{ L"Calculate MeanVariance PSO" };
		ComputePSO m_TemporalBlendFramePSO{ L"Temporal Blend Frame PSO" };
		ComputePSO m_AtrousWaveletFilterPSO{ L"Atrous Wavelet Filter PSO" };

		DynamicUploadBuffer m_DenoisingCommonParameters;

		ColorBuffer m_Reprojected;
		ColorBuffer m_LocalMeanVariance;
		ColorBuffer m_Variance;
		ColorBuffer m_LumaMoments[2];
		ColorBuffer m_Tspp[2];
		ColorBuffer m_Intermediate;

		int m_FrameIndexMod2 = 0;
		int m_CurrFrameIndex = 0;
		int m_PrevFrameIndex = 0;

		bool m_bInited = false;
		bool m_bActive = false;
		bool m_bClearHistory = true;
	};
}
