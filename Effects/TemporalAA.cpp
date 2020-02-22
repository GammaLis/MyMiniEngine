#include "TemporalAA.h"
#include "Graphics.h"
#include "GfxCommon.h"
#include "CommandContext.h"

// compiled shaders
#include "TemporalBlendCS.h"
#include "BoundNeighborhoodCS.h"
#include "SharpenTAACS.h"
#include "ResolveTAACS.h"

using namespace MyDirectX;
using namespace Math;

struct alignas(16) CSConstants
{
	float _RcpBufferDim[2];		// 1/width, 1/height
	float _TemporalBlendFactor;
	float _RcpSpeedLimiter;
	float _ViewportJitter[2];
};

void TemporalAA::Init(ID3D12Device* pDevice)
{
	// root signature
	{
		m_RootSignature.Reset(4, 2);
		m_RootSignature[0].InitAsConstants(0, 4);
		m_RootSignature[1].InitAsConstantBuffer(1);
		m_RootSignature[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 10);
		m_RootSignature[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 10);
		
		m_RootSignature.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearBorderDesc);
		m_RootSignature.InitStaticSampler(1, Graphics::s_CommonStates.SamplerPointBorderDesc);
		m_RootSignature.Finalize(pDevice, L"Temporal RS");
	}

	// PSOs
	{
		// shaderByteCode[]类型不完整，需要指定数组长度，这里改用#define
		//auto CreatePSO = [&](ComputePSO& pso, const BYTE (&shaderByteCode)[N])
		//{
		//	pso.SetRootSignature(m_RootSignature);
		//	pso.SetComputeShader(shaderByteCode, sizeof(shaderByteCode));
		//}
#define CreatePSO(pso, shaderByteCode) \
	pso.SetRootSignature(m_RootSignature); \
	pso.SetComputeShader(shaderByteCode, sizeof(shaderByteCode)); \
	pso.Finalize(pDevice);

		CreatePSO(m_TemporalBlendCS, TemporalBlendCS);
		CreatePSO(m_BoundNeighborhoodCS, BoundNeighborhoodCS);
		CreatePSO(m_SharpenTAACS, SharpenTAACS);
		CreatePSO(m_ResolveTAACS, ResolveTAACS);
#undef CreatePSO
	}
}

void TemporalAA::Shutdown()
{
}

void TemporalAA::Update(uint64_t frameIndex)
{
	m_FrameIndex = (uint32_t)frameIndex;
	m_FrameIndexMod2 = m_FrameIndex % 2;

	if (m_Enabled)
	{
		static const float Halton23[8][2] =
		{
			{0.0f / 8.0f, 0.0f / 9.0f}, {4.0f / 8.0f, 3.0f / 9.0f},
			{2.0f / 8.0f, 6.0f / 9.0f}, {6.0f / 8.0f, 1.0f / 9.0f},
			{1.0f / 8.0f, 4.0f / 9.0f}, {5.0f / 8.0f, 7.0f / 9.0f},
			{3.0f / 8.0f, 2.0f / 9.0f}, {7.0f / 8.0f, 5.0f / 9.0f},
		};

		const float* offset = Halton23[m_FrameIndex % 8];

		m_JitterDeltaX = m_JitterX - offset[0];
		m_JitterDeltaY = m_JitterY - offset[1];
		m_JitterX = offset[0];
		m_JitterY = offset[1];
	}
	else
	{
		m_JitterDeltaX = m_JitterX - 0.5f;
		m_JitterDeltaY = m_JitterY - 0.5f;
		m_JitterX = 0.5f;
		m_JitterY = 0.5f;
	}
}

void TemporalAA::ClearHistory(CommandContext& context)
{
	GraphicsContext& gfxContext = context.GetGraphicsContext();

	if (m_Enabled)
	{
		auto& temporalColor0 = Graphics::s_BufferManager.m_TemporalColor[0];
		auto& temporalColor1 = Graphics::s_BufferManager.m_TemporalColor[1];
		gfxContext.TransitionResource(temporalColor0, D3D12_RESOURCE_STATE_RENDER_TARGET);
		gfxContext.TransitionResource(temporalColor1, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfxContext.ClearColor(temporalColor0);
		gfxContext.ClearColor(temporalColor1);
	}
}

// temporal resolve
void TemporalAA::ResolveImage(CommandContext& context)
{
	auto& computeContext = context.GetComputeContext();

	static bool s_EnableTAA = false;

	// 重新开启时，清除TemporalColor，关闭时，不做处理 
	//（MS-MiniEngine 只要m_Enabled != s_EnableTAA都要清零，这里改下）
	if ((m_Enabled != s_EnableTAA && m_Enabled == true) || m_Reset)
	{
		ClearHistory(context);
		s_EnableTAA = m_Enabled;
		m_Reset = false;
	}

	uint32_t curIndex = m_FrameIndexMod2;
	uint32_t prevIndex = curIndex ^ 1;
	if (m_Enabled)
	{
		ApplyTemporalAA(computeContext);
		SharpenImage(computeContext, Graphics::s_BufferManager.m_TemporalColor[curIndex]);
	}
}

// resolve Image
void TemporalAA::ApplyTemporalAA(ComputeContext& context)
{
	uint32_t curIndex = m_FrameIndexMod2;
	uint32_t prevIndex = curIndex ^ 1;

	context.SetRootSignature(m_RootSignature);
	context.SetPipelineState(m_TemporalBlendCS);

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& velocityBuffer = Graphics::s_BufferManager.m_VelocityBuffer;
	auto& curTemporal = Graphics::s_BufferManager.m_TemporalColor[curIndex];		// 写入目标
	auto& prevTemporal = Graphics::s_BufferManager.m_TemporalColor[prevIndex];
	auto& curDepth = Graphics::s_BufferManager.m_LinearDepth[curIndex];
	auto& prevDepth = Graphics::s_BufferManager.m_LinearDepth[prevIndex];

	context.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(velocityBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(curTemporal, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.TransitionResource(prevTemporal, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(curDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(prevDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	
	CSConstants csConstants =
	{
		1.0f / colorBuffer.GetWidth(), 1.0f / colorBuffer.GetHeight(),
		m_TemporalMaxLerp, 1.0f / m_TemporalSpeedLimit,
		m_JitterDeltaX, m_JitterDeltaY
	};
	context.SetDynamicConstantBufferView(1, sizeof(csConstants), &csConstants);
	context.SetDynamicDescriptor(2, 0, velocityBuffer.GetSRV());
	context.SetDynamicDescriptor(2, 1, colorBuffer.GetSRV());
	context.SetDynamicDescriptor(2, 2, prevTemporal.GetSRV());
	context.SetDynamicDescriptor(2, 3, curDepth.GetSRV());
	context.SetDynamicDescriptor(2, 4, prevDepth.GetSRV());
	context.SetDynamicDescriptor(3, 0, curTemporal.GetUAV());

	context.Dispatch2D(colorBuffer.GetWidth(), colorBuffer.GetHeight(), 16, 8);

}

// sharpen or copy image
void TemporalAA::SharpenImage(ComputeContext& context, ColorBuffer& temporalColor)
{
	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;

	context.TransitionResource(temporalColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	context.SetPipelineState(m_Sharpness >= 0.001f ? m_SharpenTAACS : m_ResolveTAACS);

	context.SetConstants(0, 1.0f + m_Sharpness, 0.25f * m_Sharpness);
	context.SetDynamicDescriptor(2, 0, temporalColor.GetSRV());
	context.SetDynamicDescriptor(3, 0, colorBuffer.GetUAV());

	context.Dispatch2D(colorBuffer.GetWidth(), colorBuffer.GetHeight());
	
}
