#include "PostEffects.h"
#include "Graphics.h"
#include "GfxCommon.h"
#include "CommandContext.h"

// compiled shader byte code
#include "BloomExtractAndDownsampleHdrCS.h"
#include "BloomExtractAndDownsampleLdrCS.h"
#include "DownsampleBloomCS.h"
#include "DownsampleBloomAllCS.h"
#include "BlurCS.h"
#include "UpsampleAndBlurCS.h"

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

	void BloomEffect::Init(ID3D12Device* pDevice, const RootSignature* postEffectRS)
	{
		//template function
		//CreatePSO(pDevice, m_BloomExtractAndDownsampleHdrCS, *postEffectRS, BloomExtractAndDownsampleHdrCS);
		//CreatePSO(pDevice, m_BloomExtractAndDownsampleLdrCS, *postEffectRS, BloomExtractAndDownsampleLdrCS);

#define CreatePSO(pso, shaderByteCode) \
	pso.SetRootSignature(*postEffectRS); \
	pso.SetComputeShader(shaderByteCode, sizeof(shaderByteCode)); \
	pso.Finalize(pDevice);

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

	// bloom effect in CS path
	void BloomEffect::GenerateBloom(ComputeContext& context, PostEffects& postEffects)
	{
		const auto& postSettings = postEffects.m_CommonStates;

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
			context.SetPipelineState(postSettings.EnableHDR ? m_BloomExtractAndDownsampleHdrCS : m_BloomExtractAndDownsampleLdrCS);

			// set parameters
			context.SetConstants(0, 1.0f / kBloomWidth, 1.0f / kBloomWidth, postSettings.BloomThreashold);
			context.SetDynamicDescriptor(1, 0, colorBuffer.GetSRV());
			context.SetDynamicDescriptor(1, 1, postEffects.m_ExposureBuffer.GetSRV());
			context.SetDynamicDescriptor(2, 0, Graphics::s_BufferManager.m_aBloomUAV1[0].GetUAV());
			context.SetDynamicDescriptor(2, 1, lumaLRBuffer.GetUAV());

			context.Dispatch2D(kBloomWidth, kBloomHeight);
		}

		context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV1[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		// the difference between high and low quality bloom is that high quality sums 5 octaves with a 2x frequency scale,
		// and the low quality sums octaves with a 4x frequency scale
		if (postSettings.HighQualityBloom)
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
				context.SetDynamicDescriptor(1, 0, Graphics::s_BufferManager.m_aBloomUAV1[0].GetSRV());
				context.SetDynamicDescriptors(2, 0, _countof(uavs), uavs);

				// each dispatch group is 8x8 threads, but each thread reads in 2x2 source texels (bilinear filter)
				context.Dispatch2D(kBloomWidth / 2, kBloomHeight / 2);
			}

			// upsampling 
			{
				context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV2[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV3[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV4[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV5[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

				float upsampleBlendFactor = postSettings.BloomUpsampleFactor;

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

				context.SetDynamicDescriptor(1, 0, Graphics::s_BufferManager.m_aBloomUAV1[0].GetSRV());
				context.SetDynamicDescriptors(2, 0, _countof(uavs), uavs);
				
				context.Dispatch2D(kBloomWidth, kBloomHeight);
			}

			// upsampling
			{
				context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV3[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.TransitionResource(Graphics::s_BufferManager.m_aBloomUAV5[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

				float upsampleBlendFactor = postSettings.BloomUpsampleFactor * 2.0f / 3.0f;

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
		context.SetDynamicDescriptors(1, 0, _countof(srvs), srvs);

		context.SetDynamicDescriptor(2, 0, buffer[1].GetUAV());

		// dispatch the compute shader with default 8x8 thread groups
		context.Dispatch2D(bufferWidth, bufferHeight);

		context.TransitionResource(buffer[1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
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
	}

	void PostEffects::ProcessHDR(ComputeContext& context)
	{
	}

	// SDR Processing
	void PostEffects::ProcessLDR(CommandContext& baseContext)
	{
		ComputeContext& context = baseContext.GetComputeContext();

		// bloom
		if (m_CommonStates.EnableBloom)
		{
			m_BloomEffect.GenerateBloom(context, *this);
		}
	}
}


