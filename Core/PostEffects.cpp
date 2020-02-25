#include "PostEffects.h"
#include "Graphics.h"
#include "GfxCommon.h"
#include "CommandContext.h"
#include "TextureManager.h"

// compiled shader byte code
// bloom
#include "BloomExtractAndDownsampleHdrCS.h"
#include "BloomExtractAndDownsampleLdrCS.h"
#include "DownsampleBloomCS.h"
#include "DownsampleBloomAllCS.h"
#include "BlurCS.h"
#include "UpsampleAndBlurCS.h"
#include "ApplyBloomCS.h"
#include "ApplyBloom2CS.h"		// SUPPORT_TYPED_UAV_LOADS

// tone map
#include "ToneMapCS.h"
#include "ToneMap2CS.h"
#include "ToneMapHDRCS.h"
#include "ToneMapHDR2CS.h"

// adaptation
#include "ExtractLumaCS.h"
#include "GenerateHistogramCS.h"
#include "AdaptExposureCS.h"

#include "CopyBackPostBufferCS.h"

namespace MyDirectX
{
	const float kInitialMinLog = -12.0f;
	const float kInitialMaxLog = 4.0f;

	//template <int N>
	//void CreatePSO(ID3D12Device* pDevice, ComputePSO& pso, const RootSignature &rs, const BYTE(&shaderByteCode)[N])
	//{
	//	pso.SetRootSignature(rs);
	//	pso.SetComputeShader(shaderByteCode, sizeof(shaderByteCode));
	//	pso.Finalize(pDevice);
	//}

	///
	/// bloom
	///
	void BloomEffect::Init(ID3D12Device* pDevice, const RootSignature* postEffectRS)
	{
		//template function
		//CreatePSO(pDevice, m_BloomExtractAndDownsampleHdrCS, *postEffectRS, BloomExtractAndDownsampleHdrCS);
		//CreatePSO(pDevice, m_BloomExtractAndDownsampleLdrCS, *postEffectRS, BloomExtractAndDownsampleLdrCS);

#define CreatePSO(pso, shaderByteCode) \
	pso.SetRootSignature(*postEffectRS); \
	pso.SetComputeShader(shaderByteCode, sizeof(shaderByteCode)); \
	pso.Finalize(pDevice);

		if (GfxStates::s_bTypedUAVLoadSupport_R11G11B10_FLOAT)
		{
			CreatePSO(m_ApplyBloomCS, ApplyBloom2CS);
		}
		else
		{
			CreatePSO(m_ApplyBloomCS, ApplyBloomCS);
		}

		CreatePSO(m_BloomExtractAndDownsampleHdrCS, BloomExtractAndDownsampleHdrCS);
		CreatePSO(m_BloomExtractAndDownsampleLdrCS, BloomExtractAndDownsampleLdrCS);
		
		CreatePSO(m_DownsampleBloom4CS, DownsampleBloomAllCS);
		CreatePSO(m_DownsampleBloom2CS, DownsampleBloomCS);

		CreatePSO(m_BlurCS, BlurCS);
		CreatePSO(m_UpsampleAndBlurCS, UpsampleAndBlurCS);

#undef CreatePSO
	}

	void BloomEffect::Shutdown()
	{
	}

	void BloomEffect::Render(ComputeContext& context, PostEffects& postEffects)
	{
		const auto& postSettings = postEffects.m_CommonStates;
		// caching bloom settings
		m_EnableHDR = postSettings.EnableHDR;
		m_bHighQualityBloom = postSettings.HighQualityBloom;
		m_BloomThreshold = postSettings.BloomThreshold;
		m_BloomUpsampleFactor = postSettings.BloomUpsampleFactor;
		m_BloomStrength = postSettings.BloomStrength;

		GenerateBloom(context, postEffects);
		// 如果没有开启HDR，直接将BloomEffect混合到SceneColorBuffer
		// 否则，需要进行Tonemapping，交由后续处理
		if (!postSettings.EnableHDR)
			ApplyBloom(context, m_BloomStrength);
	}

	// bloom effect in CS path
	void BloomEffect::GenerateBloom(ComputeContext& context, PostEffects& postEffects)
	{
		// we can generate a bloom buffer up to 1/4 smaller in each dimension without undersampling
		// if only downsizing by 1/2 or less, a faster shader can be used which only does one bilinear sample
		auto& lumaLRBuffer = Graphics::s_BufferManager.m_LumaLR;
		uint32_t kBloomWidth = lumaLRBuffer.GetWidth(), kBloomHeight = lumaLRBuffer.GetHeight();

		// these bloom buffer dimensions were chosen for their impressive divisibility by 128 and because they can roughly 16:9. 
		// the blurring algorithm is exactly 9 pixels by 9 pixels, so if the aspect ratio of each pixel is not square,
		// the blur will be oval(椭圆的) in appearance rather than circular. Coincidentally, they are close to 1/2 of
		// a 720p buffer and 1/3 of 1080p. This is a common size for a bloom buffer on consoles.
		ASSERT(kBloomWidth % 16 == 0 && kBloomHeight % 16 == 0, "Bloom buffer dimensions must be multiples of 16");

		auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
		// context.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		// 不设置也可以吧，初始状态就行了 -20-2-23
		// context.TransitionResource(postEffects.m_ExposureBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		context.TransitionResource(lumaLRBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV1[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		// extract bloom and downsample
		{
			context.SetPipelineState(m_EnableHDR ? m_BloomExtractAndDownsampleHdrCS : m_BloomExtractAndDownsampleLdrCS);

			// set parameters
			context.SetConstants(0, 1.0f / kBloomWidth, 1.0f / kBloomWidth, m_BloomThreshold);
			context.SetDynamicDescriptor(2, 0, colorBuffer.GetSRV());
			context.SetDynamicDescriptor(2, 1, postEffects.m_ExposureBuffer.GetSRV());
			context.SetDynamicDescriptor(3, 0, Graphics::s_BufferManager.m_aBloomUAV1[0].GetUAV());
			context.SetDynamicDescriptor(3, 1, lumaLRBuffer.GetUAV());

			context.Dispatch2D(kBloomWidth, kBloomHeight);
		}

		context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV1[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		// the difference between high and low quality bloom is that high quality sums 5 octaves with a 2x frequency scale,
		// and the low quality sums octaves with a 4x frequency scale
		if (m_bHighQualityBloom)
		{
			// downsampling
			{
				context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV2[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV3[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV4[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV5[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				// set the UAVs
				D3D12_CPU_DESCRIPTOR_HANDLE uavs[4] =
				{
					Graphics::s_BufferManager.m_aBloomUAV2[0].GetUAV(),
					Graphics::s_BufferManager.m_aBloomUAV3[0].GetUAV(),
					Graphics::s_BufferManager.m_aBloomUAV4[0].GetUAV(),
					Graphics::s_BufferManager.m_aBloomUAV5[0].GetUAV(),
				};

				context.SetPipelineState(m_DownsampleBloom4CS);
				// context.SetConstants(0, 1.0f / kBloomWidth, 1.0f / kBloomWidth);		// 没有改动，不用设置
				context.SetDynamicDescriptor(2, 0, Graphics::s_BufferManager.m_aBloomUAV1[0].GetSRV());
				context.SetDynamicDescriptors(3, 0, _countof(uavs), uavs);

				// each dispatch group is 8x8 threads, but each thread reads in 2x2 source texels (bilinear filter)
				context.Dispatch2D(kBloomWidth / 2, kBloomHeight / 2);
			}

			// upsampling 
			{
				context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV2[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV3[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV4[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV5[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

				float upsampleBlendFactor = m_BloomUpsampleFactor;

				// blur then upsample and blur 4 times
				BlurBuffer(context, Graphics::s_BufferManager.m_aBloomUAV5, Graphics::s_BufferManager.m_aBloomUAV5[0], 1.0f);
				BlurBuffer(context, Graphics::s_BufferManager.m_aBloomUAV4, Graphics::s_BufferManager.m_aBloomUAV5[1], upsampleBlendFactor);
				BlurBuffer(context, Graphics::s_BufferManager.m_aBloomUAV3, Graphics::s_BufferManager.m_aBloomUAV4[1], upsampleBlendFactor);
				BlurBuffer(context, Graphics::s_BufferManager.m_aBloomUAV2, Graphics::s_BufferManager.m_aBloomUAV3[1], upsampleBlendFactor);
				BlurBuffer(context, Graphics::s_BufferManager.m_aBloomUAV1, Graphics::s_BufferManager.m_aBloomUAV2[1], upsampleBlendFactor);
			}
		}
		else
		{
			// downsampling
			{
				context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV3[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV5[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				D3D12_CPU_DESCRIPTOR_HANDLE uavs[2] =
				{
					Graphics::s_BufferManager.m_aBloomUAV3[0].GetUAV(),
					Graphics::s_BufferManager.m_aBloomUAV5[0].GetUAV(),
				};

				context.SetPipelineState(m_DownsampleBloom2CS);

				context.SetDynamicDescriptor(2, 0, Graphics::s_BufferManager.m_aBloomUAV1[0].GetSRV());
				context.SetDynamicDescriptors(3, 0, _countof(uavs), uavs);
				
				context.Dispatch2D(kBloomWidth, kBloomHeight);
			}

			// upsampling
			{
				context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV3[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV5[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

				float upsampleBlendFactor = m_BloomUpsampleFactor * 2.0f / 3.0f;

				// blur the upsample and blur 2 times
				BlurBuffer(context, Graphics::s_BufferManager.m_aBloomUAV5, Graphics::s_BufferManager.m_aBloomUAV5[0], 1.0f);
				BlurBuffer(context, Graphics::s_BufferManager.m_aBloomUAV3, Graphics::s_BufferManager.m_aBloomUAV5[1], upsampleBlendFactor);
				BlurBuffer(context, Graphics::s_BufferManager.m_aBloomUAV1, Graphics::s_BufferManager.m_aBloomUAV3[1], upsampleBlendFactor);
			}
		}
	}

	// buffer[0] - downsample, buffer[1] - upsample
	void BloomEffect::BlurBuffer(ComputeContext& context, ColorBuffer buffer[2], const ColorBuffer& lowerResBuf, float upsampleBlendFactor)
	{
		uint32_t bufferWidth = buffer[0].GetWidth();
		uint32_t bufferHeight = buffer[0].GetHeight();

		context.TransitionResource(buffer[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		// set the shader: upsample and blur or just blur
		// 最后一次downsample对应buffer不用混合，直接模糊
		context.SetPipelineState(&buffer[0] == &lowerResBuf ? m_BlurCS : m_UpsampleAndBlurCS);

		context.SetConstants(0, 1.0f / bufferWidth, 1.0f / bufferHeight, upsampleBlendFactor);
		
		D3D12_CPU_DESCRIPTOR_HANDLE srvs[] =
		{
			buffer[0].GetSRV(), lowerResBuf.GetSRV()
		};
		context.SetDynamicDescriptors(2, 0, _countof(srvs), srvs);

		context.SetDynamicDescriptor(3, 0, buffer[1].GetUAV());

		// dispatch the compute shader with default 8x8 thread groups
		context.Dispatch2D(bufferWidth, bufferHeight);

		context.TransitionResource(buffer[1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}

	void BloomEffect::ApplyBloom(ComputeContext& context, float bloomStrength)
	{
		auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
		auto& postEffectsBuffer = Graphics::s_BufferManager.m_PoseEffectsBuffer;
		uint32_t width = colorBuffer.GetWidth(), height = colorBuffer.GetHeight();
		
		if (GfxStates::s_bTypedUAVLoadSupport_R11G11B10_FLOAT)	// 支持 R11G11B10_FLOAT格式UAV加载
			context.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		else
			context.TransitionResource(postEffectsBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV1[1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		context.TransitionResource(Graphics::s_BufferManager.m_LumaBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		context.SetPipelineState(m_ApplyBloomCS);
		context.SetConstants(0, 1.0f / width, 1.0f / height, bloomStrength);
		context.SetDynamicDescriptor(2, 0, Graphics::s_BufferManager.m_aBloomUAV1[1].GetSRV());
		if (GfxStates::s_bTypedUAVLoadSupport_R11G11B10_FLOAT)
		{
			context.SetDynamicDescriptor(3, 0, colorBuffer.GetUAV());
		}
		else
		{
			context.SetDynamicDescriptor(2, 1, colorBuffer.GetSRV());
			context.SetDynamicDescriptor(3, 0, postEffectsBuffer.GetUAV());
		}
		context.SetDynamicDescriptor(3, 1, Graphics::s_BufferManager.m_LumaBuffer.GetUAV());

		context.Dispatch2D(width, height);

		context.TransitionResource(Graphics::s_BufferManager.m_LumaBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}

	///
	/// tone map
	///
	void ToneMapper::Init(ID3D12Device* pDevice, const RootSignature* postEffectRS)
	{
#define CreatePSO(pso, shaderByteCode) \
	pso.SetRootSignature(*postEffectRS); \
	pso.SetComputeShader(shaderByteCode, sizeof(shaderByteCode)); \
	pso.Finalize(pDevice);

		if (GfxStates::s_bTypedUAVLoadSupport_R11G11B10_FLOAT)
		{
			CreatePSO(m_ToneMapCS, ToneMap2CS);
			CreatePSO(m_ToneMapHDRCS, ToneMapHDR2CS);
		}
		else
		{
			CreatePSO(m_ToneMapCS, ToneMapCS);
			CreatePSO(m_ToneMapHDRCS, ToneMapHDRCS);
		}

#undef CreatePSO
	}

	void ToneMapper::Shutdown()
	{
	}

	void ToneMapper::Render(ComputeContext& context, PostEffects& postEffects)
	{
		const auto& postSettings = postEffects.m_CommonStates;

		// tonemapping
		auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
		auto& postEffectsBuffer = Graphics::s_BufferManager.m_PoseEffectsBuffer;
		uint32_t width = colorBuffer.GetWidth(), height = colorBuffer.GetHeight();

		if (GfxStates::s_bTypedUAVLoadSupport_R11G11B10_FLOAT)
			context.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		else
			context.TransitionResource(postEffectsBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		context.TransitionResource(postEffects.m_ExposureBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		// 可能没有生成Bloom，开启时设置
		//context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV1[1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		context.TransitionResource(Graphics::s_BufferManager.m_LumaBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		context.SetPipelineState(GfxStates::s_bEnableHDROutput ? m_ToneMapHDRCS : m_ToneMapCS);

		context.SetConstants(0, 1.0f / width, 1.0f / height, postSettings.BloomStrength);
		context.SetDynamicDescriptor(2, 0, postEffects.m_ExposureBuffer.GetSRV());
		context.SetDynamicDescriptor(2, 1, postSettings.EnableBloom ?
			Graphics::s_BufferManager.m_aBloomUAV1[1].GetSRV() : TextureManager::GetBlackTex2D().GetSRV());

		if (GfxStates::s_bTypedUAVLoadSupport_R11G11B10_FLOAT)
		{
			context.SetDynamicDescriptor(3, 0, colorBuffer.GetUAV());
		}
		else
		{
			context.SetDynamicDescriptor(2, 2, colorBuffer.GetSRV());
			context.SetDynamicDescriptor(3, 0, postEffectsBuffer.GetUAV());
		}
		context.SetDynamicDescriptor(3, 1, Graphics::s_BufferManager.m_LumaBuffer.GetUAV());

		context.Dispatch2D(width, height);

		// do this last so that the bright pass uses the same exposure as tone mapping
		// UpdateExposure(context);
	}

	void PostEffects::Init(ID3D12Device* pDevice)
	{
		// root signature
		m_PostEffectsRS.Reset(4, 2);
		m_PostEffectsRS[0].InitAsConstants(0, 4);
		m_PostEffectsRS[1].InitAsConstantBuffer(1);
		m_PostEffectsRS[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 4);
		m_PostEffectsRS[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 4);
		m_PostEffectsRS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearClampDesc);
		m_PostEffectsRS.InitStaticSampler(1, Graphics::s_CommonStates.SamplerLinearBorderDesc);
		m_PostEffectsRS.Finalize(pDevice, L"Post Effects");

		// PSOs
#define CreatePSO(pso, shaderByteCode) \
	pso.SetRootSignature(m_PostEffectsRS); \
	pso.SetComputeShader(shaderByteCode, sizeof(shaderByteCode)); \
	pso.Finalize(pDevice);

		CreatePSO(m_ExtractLumaCS, ExtractLumaCS);

		CreatePSO(m_GenerateHistogramCS, GenerateHistogramCS);

		CreatePSO(m_AdaptExposureCS, AdaptExposureCS);

		CreatePSO(m_CopyBackPostBufferCS, CopyBackPostBufferCS);

#undef CreatePSO
		
		// 
		float exposure = m_CommonStates.Exposure;
		float initExposure[8] =
		{
			exposure, 1.0f / exposure, exposure, 0.0f,
			kInitialMinLog, kInitialMaxLog, kInitialMaxLog - kInitialMinLog, 1.0f / (kInitialMaxLog - kInitialMinLog)
		};
		m_ExposureBuffer.Create(pDevice, L"Exposure", 8, 4, initExposure);

		// bloom
		m_BloomEffect.Init(pDevice, &m_PostEffectsRS);

		// tone map
		m_ToneMapper.Init(pDevice, &m_PostEffectsRS);
	}

	void PostEffects::Shutdown()
	{
		m_BloomEffect.Shutdown();

		m_ToneMapper.Shutdown();

		m_ExposureBuffer.Destroy();
	}

	void PostEffects::Render()
	{
		ComputeContext& context = ComputeContext::Begin(L"Post Effects");

		context.SetRootSignature(m_PostEffectsRS);

		auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
		context.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		if (m_CommonStates.EnableHDR)
			ProcessHDR(context);
		else
			ProcessLDR(context);

		bool bGeneratedLumaBuffer = m_CommonStates.EnableHDR || m_CommonStates.EnableBloom;

		/**
			in the case where we've been doing post processing in a separate buffer, we need to copy it
		back to the original buffer. It is possible to skip this step if the next shader knows to do
		the manual format decode from UINT, but there are several code paths that need to be changed,
		and some of them rely on texture filtering, which won't work with UINT. Since this is only to 
		support legacy hardware and a single buffer copy isn't that big of a deal, this is the most 
		economical solution.
		*/
		if (!GfxStates::s_bTypedUAVLoadSupport_R11G11B10_FLOAT)
			CopyBackPostBuffer(context);

		context.Finish();
	}

	// HDR tone mapping
	void PostEffects::ProcessHDR(ComputeContext& context)
	{
		if (m_CommonStates.EnableBloom)
		{
			m_BloomEffect.Render(context, *this);
			context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV1[1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		}
		else if (m_CommonStates.EnableAdaption)
			ExtractLuma(context);

		//// tonemapping
		//auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
		//auto& postEffectsBuffer = Graphics::s_BufferManager.m_PoseEffectsBuffer;

		//if (GfxStates::s_bTypedUAVLoadSupport_R11G11B10_FLOAT)
		//	context.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		//else
		//	context.TransitionResource(postEffectsBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		m_ToneMapper.Render(context, *this);

		// Do this last so that the bright pass uses the same exposure as tone mapping
		UpdateExposure(context);

	}

	// SDR Processing
	void PostEffects::ProcessLDR(CommandContext& baseContext)
	{
		ComputeContext& context = baseContext.GetComputeContext();

		bool bGenerateBloom = m_CommonStates.EnableBloom;
		// bloom
		if (bGenerateBloom)
		{
			m_BloomEffect.Render(context, *this);
		}		
	}

	// extract luma
	void PostEffects::ExtractLuma(ComputeContext& context)
	{
		auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
		auto& lumaBuffer = Graphics::s_BufferManager.m_LumaLR;
		uint32_t width = lumaBuffer.GetWidth(), height = lumaBuffer.GetHeight();

		context.TransitionResource(m_ExposureBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		context.TransitionResource(lumaBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				
		context.SetPipelineState(m_ExtractLumaCS);
		context.SetConstants(0, width, height);
		context.SetDynamicDescriptor(2, 0, colorBuffer.GetSRV());
		context.SetDynamicDescriptor(2, 1, m_ExposureBuffer.GetSRV());
		context.SetDynamicDescriptor(3, 0, lumaBuffer.GetUAV());

		context.Dispatch2D(width, height);

	}

	// update exposure
	struct alignas(16) CSConstants
	{
		float _TargetLuminance;
		float _AdaptationRate;
		float _MinExposure;
		float _MaxExposure;
		uint32_t _PixelCount;
	};
	void PostEffects::UpdateExposure(ComputeContext& context)
	{
		if (!m_CommonStates.EnableAdaption)
		{
			float exposure = m_CommonStates.Exposure;
			float initExposure[8] =
			{
				exposure, 1.0f / exposure, exposure, 0.0f,
				kInitialMinLog, kInitialMaxLog, kInitialMaxLog - kInitialMinLog, 1.0f / (kInitialMaxLog - kInitialMinLog)
			};
			context.WriteBuffer(m_ExposureBuffer, 0, initExposure, sizeof(initExposure));
			context.TransitionResource(m_ExposureBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

			return;
		}

		// generate an HDR histogram
		auto& lumaBuffer = Graphics::s_BufferManager.m_LumaLR;
		auto& histogramBuffer = Graphics::s_BufferManager.m_Histogram;
		{
			context.TransitionResource(histogramBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
			context.ClearUAV(histogramBuffer);
			context.TransitionResource(lumaBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

			context.SetPipelineState(m_GenerateHistogramCS);

			context.SetDynamicDescriptor(2, 0, lumaBuffer.GetSRV());
			context.SetDynamicDescriptor(3, 0, histogramBuffer.GetUAV());

			context.Dispatch2D(lumaBuffer.GetWidth(), lumaBuffer.GetHeight(), 16, 384);
		}

		// adapt exposure (自适应曝光)
		{
			context.TransitionResource(histogramBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.TransitionResource(m_ExposureBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			
			context.SetPipelineState(m_AdaptExposureCS);

			CSConstants csConstants{ m_CommonStates.TargetLuminance, m_CommonStates.AdaptationRate,
				m_CommonStates.MinExposure, m_CommonStates.MaxExposure, 
				lumaBuffer.GetWidth() * lumaBuffer.GetHeight()
			};
			context.SetDynamicConstantBufferView(1, sizeof(csConstants), &csConstants);
			context.SetDynamicDescriptor(2, 0, histogramBuffer.GetSRV());
			context.SetDynamicDescriptor(3, 0, m_ExposureBuffer.GetUAV());
			context.Dispatch();

			context.TransitionResource(m_ExposureBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		}
	}

	// copy Post back to Scene
	void PostEffects::CopyBackPostBuffer(ComputeContext& context)
	{
		context.SetPipelineState(m_CopyBackPostBufferCS);

		auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
		auto& postEffectsBuffer = Graphics::s_BufferManager.m_PoseEffectsBuffer;
		uint32_t width = colorBuffer.GetWidth(), height = colorBuffer.GetHeight();

		context.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.TransitionResource(postEffectsBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		context.SetDynamicDescriptor(2, 0, postEffectsBuffer.GetSRV());
		context.SetDynamicDescriptor(3, 0, colorBuffer.GetUAV());

		context.Dispatch2D(width, height);
	}

}
