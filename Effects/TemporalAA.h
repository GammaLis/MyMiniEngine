#pragma once
#include "pch.h"
#include "RootSignature.h"
#include "PipelineState.h"

namespace MyDirectX
{
	class CommandContext;
	class ComputeContext;
	class ColorBuffer;

	// Temporal antialiasing involves jittering sample positions and accumulating color over time to 
	// effectively supersample the image.
	class TemporalAA
	{
		enum class RSId
		{
			CBConstants = 0,
			CBV,
			SRVTable,
			UAVTable,

			Count
		};
		
	public:
		enum class ETAAMethod
		{
			MSTAA,
			INTELTAA
		};

		void Init(ID3D12Device *pDevice);

		void Shutdown();

		// call once per frame to increment the internal frame counter and, in the case of TAA, 
		// choosing the next jittered sample position
		void Update(uint64_t frameIndex);

		// returns whether the frame is odd or even, relavant to checkerboard rendering
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

		// common options
		bool m_Enabled = true;
		float m_Sharpness = 0.5f;			// sharpness 0.0f - 1.0f
		float m_TemporalMaxLerp = 1.0f;		// blend factor 0.0f - 1.0f
		float m_TemporalSpeedLimit = 64.0f;	// speed limit 1.0f - 1024.0f
		bool m_Reset = false;
		bool m_EnableCBR = false;

		// Intel - options
		bool m_ITAA_AllowLongestVelocityVector = true;
		bool m_ITAA_AllowDepthThreshold = true;
		bool m_ITAA_AllowVarianceClipping = true;
		bool m_ITAA_AllowYCoCg = true;
		bool m_ITAA_AllowBicubicFilter = true;
		bool m_ITAA_AllowNeighborhoodSampling = true;
		bool m_ITAA_MarkNoHistoryPixels = false;
		bool m_ITAA_PreferBluenoise = false;
		
		void SetTAAMethod(ETAAMethod method) { m_TAAMethod = method; }

		uint32_t GetFrameIndexMod2() { return m_FrameIndexMod2; }
		void GetJitterOffset(float &jitterX, float &jitterY) { jitterX = m_JitterX; jitterY = m_JitterY; }

	private:
		void ApplyTemporalAA(ComputeContext& context);
		void ApplyTemporalIntelAA(ComputeContext &context/*, const Math::Matrix4 &currProjMat, const Math::Matrix4 &prevProjMat*/);
		void SharpenImage(ComputeContext& context, ColorBuffer& temporalColor);

		// root signature
		RootSignature m_RootSignature;
		// PSOs
		ComputePSO m_TemporalBlendCS { L"TAA: Temporal Blend CS" };
		ComputePSO m_BoundNeighborhoodCS { L"TAA: Bound Neighborhood CS"};
		ComputePSO m_SharpenTAACS { L"TAA: Sharpen TAA CS"};
		ComputePSO m_ResolveTAACS { L"TAA: Resolve TAA CS"};
		ComputePSO m_ResolveTAACS_Intel { L"TAA: Intel Resolve TAA"};

		uint32_t m_FrameIndex = 0;
		uint32_t m_FrameIndexMod2 = 0;
		float m_JitterX = 0.5f;
		float m_JitterY = 0.5f;
		float m_JitterDeltaX = 0.0f;
		float m_JitterDeltaY = 0.0f;

		ETAAMethod m_TAAMethod = ETAAMethod::MSTAA;
	};

}
