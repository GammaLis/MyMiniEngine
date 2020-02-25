#pragma once
#include "pch.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "GpuBuffer.h"

namespace MyDirectX
{
	class CommandContext;
	class ComputeContext;
	class ColorBuffer;

	class PostEffects;

	class BloomEffect
	{
	public:
		void Init(ID3D12Device *pDevice, const RootSignature *postEffectRS = nullptr);
		void Shutdown();

		void Render(ComputeContext &context, PostEffects& postEffects);

		void GenerateBloom(ComputeContext &context, PostEffects& postEffects);

		void BlurBuffer(ComputeContext& context, ColorBuffer buffer[2], const ColorBuffer& lowerResBuf, float upsampleBlendFactor);

		void ApplyBloom(ComputeContext& context, float bloomStrength = 1.0f);

		ComputePSO m_BloomExtractAndDownsampleHdrCS;
		ComputePSO m_BloomExtractAndDownsampleLdrCS;
		
		ComputePSO m_DownsampleBloom2CS;
		ComputePSO m_DownsampleBloom4CS;

		ComputePSO m_ApplyBloomCS;
		ComputePSO m_BlurCS;
		ComputePSO m_UpsampleAndBlurCS;

		// cached properties
		bool m_EnableHDR = false;
		bool m_bHighQualityBloom = false;
		float m_BloomThreshold = 0.0f;
		float m_BloomUpsampleFactor = 0.0f;
		float m_BloomStrength = 1.0f;

	};

	class ToneMapper
	{
	public:
		void Init(ID3D12Device* pDevice, const RootSignature* postEffectRS = nullptr);
		void Shutdown();

		void Render(ComputeContext& context, PostEffects& postEffects);

		ComputePSO m_ToneMapCS;
		ComputePSO m_ToneMapHDRCS;
	};

	class PostEffects
	{
		friend class BloomEffect;
		friend class ToneMapper;

	public:
		void Init(ID3D12Device *pDevice);
		void Shutdown();

		void Render();

		void CopyBackPostBuffer(ComputeContext& context);

		struct CommonStates
		{
			bool EnableHDR = true;		// turn on tone mapping features

			// tone mapping parameters
			float Exposure = 2.0f;		// brightness scaler when adaptive exposure is disabled
			bool EnableAdaption = true;	// automatically adjust brightness based on perceived luminance

			// adaption parameters
			float MinExposure = 1.0f / 64.0f;	// [1/256, 1]
			float MaxExposure = 64.0f;			// [1, 256]
			float TargetLuminance = 0.08f;		// [0.01f, 0.99f]
			float AdaptationRate = 0.05f;		// [0.01f, 1.0f]

			// bloom parameters
			bool EnableBloom = true;
			// the threshold luminance above which a pixel will start to bloom
			float BloomThreshold = 4.0f;		// [0.0f, 8.0f]
			// a modulator controlling how much bloom is added back into the image
			float BloomStrength = 0.1f;			// [0.0f, 2.0f]
			// controls the "focus" of the blur. High values spread out more causing a haze
			float BloomUpsampleFactor = 0.65f;	// [0.0f, 1.0f]
			// high quality blurs 5 octaves of bloom, low quality only blurs 3
			bool HighQualityBloom = true;
		};
		CommonStates m_CommonStates;

	private:
		void ProcessHDR(ComputeContext& context);
		void ProcessLDR(CommandContext& baseContext);

		// Adaption其实也可单独作为一个effect，但是这里直接写在PostEffects里	-20-2-24
		void ExtractLuma(ComputeContext& context);
		void UpdateExposure(ComputeContext& context);

		RootSignature m_PostEffectsRS;
	
		ComputePSO m_ExtractLumaCS;
		ComputePSO m_GenerateHistogramCS;
		ComputePSO m_AdaptExposureCS;
		ComputePSO m_CopyBackPostBufferCS;

		StructuredBuffer m_ExposureBuffer;

		BloomEffect m_BloomEffect;
		ToneMapper m_ToneMapper;
	};

}