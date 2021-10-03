#include "TemporalAA.h"
#include "Graphics.h"
#include "GfxCommon.h"
#include "CommandContext.h"

// compiled shaders
#include "TemporalBlendCS.h"
#include "BoundNeighborhoodCS.h"
#include "SharpenTAACS.h"
#include "ResolveTAACS.h"
#include "ResolveTAACS_Intel.h" // Intel TAA

using namespace MyDirectX;
using namespace Math;

void TemporalAA::Init(ID3D12Device* pDevice)
{
	// root signature
	{
		m_RootSignature.Reset((UINT)RSId::Count, 2);
		m_RootSignature[(UINT)RSId::CBConstants].InitAsConstants(0, 4);
		m_RootSignature[(UINT)RSId::CBV].InitAsConstantBuffer(1);
		m_RootSignature[(UINT)RSId::SRVTable].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 10);
		m_RootSignature[(UINT)RSId::UAVTable].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 10);
		
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
		CreatePSO(m_ResolveTAACS_Intel, ResolveTAACS_Intel);
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

		// INTEL's TAA
		// following work of Vaidyanathan et all:
		// https://software.intel.com/content/www/us/en/develop/articles/coarse-pixel-shading-with-temporal-supersampling.html
		static const float Halton23_16[16][2] = 
		{ 
			{ 0.0f, 0.0f },			{ 0.5f, 0.333333f },	{ 0.25f, 0.666667f },	{ 0.75f, 0.111111f }, 
			{ 0.125f, 0.444444f },	{ 0.625f, 0.777778f },	{ 0.375f, 0.222222f },	{ 0.875f, 0.555556f }, 
			{ 0.0625f, 0.888889f }, { 0.5625f, 0.037037f }, { 0.3125f, 0.37037f },	{ 0.8125f, 0.703704f }, 
			{ 0.1875f, 0.148148f },	{ 0.6875f, 0.481481f }, { 0.4375f, 0.814815f }, { 0.9375f, 0.259259f } 
		};
		static const float BlueNoise_16[16][2] = 
		{
			{ 1.5f, 0.59375f },		{ 1.21875f, 1.375f },	{ 1.6875f, 1.90625f },	{ 0.375f, 0.84375f },
			{ 1.125f, 1.875f },		{ 0.71875f, 1.65625f }, { 1.9375f ,0.71875f },	{ 0.65625f ,0.125f }, 
			{ 0.90625f, 0.9375f },	{ 1.65625f, 1.4375f },	{ 0.5f, 1.28125f },		{ 0.21875f, 0.0625f }, 
			{ 1.843750f,0.312500f },{ 1.09375f, 0.5625f },	{ 0.0625f, 1.21875f },	{ 0.28125f, 1.65625f },
		};

		const float* offset = nullptr;
		float scale = 1.0f;

		if (m_TAAMethod == ETAAMethod::INTELTAA)
		{
			if (m_ITAA_PreferBluenoise)
			{
				// 0. blue noise
				scale = 1.0f;
				offset = BlueNoise_16[m_FrameIndex % 16];
			}
			else 
			{
				// 1.
				scale = 1.0f;
				offset = Halton23_16[m_FrameIndex % 16];
			}
		}
		else
		{
			// with CBR, having an odd number of jitter positions is good because odd and even
			// frames can both explore all sample positions. (Also, the latest useful sample is
			// the first one, which is exactly centered between 4 pixels.)
			if (m_EnableCBR)
				offset = Halton23[m_FrameIndex % 7 + 1];
			else
				offset = Halton23[m_FrameIndex % 8];
		}

		m_JitterDeltaX = m_JitterX - offset[0] * scale;
		m_JitterDeltaY = m_JitterY - offset[1] * scale;
		m_JitterX = offset[0] * scale;
		m_JitterY = offset[1] * scale;
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
	static bool s_EnableCBR = false;

	// 重新开启时，清除TemporalColor，关闭时，不做处理 
	//（MS-MiniEngine 只要m_Enabled != s_EnableTAA都要清零，这里改下）
	if ((m_Enabled != s_EnableTAA && m_Enabled == true) || 
		(m_EnableCBR != s_EnableCBR && m_EnableCBR == true) || m_Reset)
	{
		ClearHistory(context);
		s_EnableTAA = m_Enabled;
		s_EnableCBR = m_EnableCBR;
		m_Reset = false;
	}

	uint32_t currIndex = m_FrameIndexMod2;
	uint32_t prevIndex = currIndex ^ 1;
	if (m_Enabled)
	{
		if (m_TAAMethod == ETAAMethod::INTELTAA)
			ApplyTemporalIntelAA(computeContext);
		else
			ApplyTemporalAA(computeContext);
		SharpenImage(computeContext, Graphics::s_BufferManager.m_TemporalColor[currIndex]);
	}
}

// resolve Image
void TemporalAA::ApplyTemporalAA(ComputeContext& context)
{
	struct alignas(16) CSConstants
	{
		float _RcpBufferDim[2];		// 1/width, 1/height
		float _TemporalBlendFactor;
		float _RcpSpeedLimiter;
		float _ViewportJitter[2];
	};

	uint32_t currIndex = m_FrameIndexMod2;
	uint32_t prevIndex = currIndex ^ 1;	// history temporal index

	context.SetRootSignature(m_RootSignature);
	context.SetPipelineState(m_TemporalBlendCS);

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& velocityBuffer = Graphics::s_BufferManager.m_VelocityBuffer;
	auto& currTemporal = Graphics::s_BufferManager.m_TemporalColor[currIndex];		// 写入目标
	auto& prevTemporal = Graphics::s_BufferManager.m_TemporalColor[prevIndex];
	auto& currDepth = Graphics::s_BufferManager.m_LinearDepth[currIndex];
	auto& prevDepth = Graphics::s_BufferManager.m_LinearDepth[prevIndex];

	context.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(velocityBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(prevDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(currDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(prevTemporal, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(currTemporal, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	
	CSConstants csConstants =
	{
		1.0f / colorBuffer.GetWidth(), 1.0f / colorBuffer.GetHeight(),
		m_TemporalMaxLerp, 1.0f / m_TemporalSpeedLimit,
		m_JitterDeltaX, m_JitterDeltaY
	};
	context.SetDynamicConstantBufferView((UINT)RSId::CBV, sizeof(csConstants), &csConstants);
	context.SetDynamicDescriptor((UINT)RSId::SRVTable, 0, velocityBuffer.GetSRV());
	context.SetDynamicDescriptor((UINT)RSId::SRVTable, 1, colorBuffer.GetSRV());
	context.SetDynamicDescriptor((UINT)RSId::SRVTable, 2, prevTemporal.GetSRV());
	context.SetDynamicDescriptor((UINT)RSId::SRVTable, 3, currDepth.GetSRV());
	context.SetDynamicDescriptor((UINT)RSId::SRVTable, 4, prevDepth.GetSRV());
	context.SetDynamicDescriptor((UINT)RSId::UAVTable, 0, currTemporal.GetUAV());

	context.Dispatch2D(colorBuffer.GetWidth(), colorBuffer.GetHeight(), 16, 8);

}

void TemporalAA::ApplyTemporalIntelAA(ComputeContext& context/*, const Math::Matrix4& currProjMat, const Math::Matrix4& prevProjMat*/)
{
	struct alignas(16) CSConstants
	{
		Math::Vector4 _Resolution;	// width, height, 1/width, 1/height
		float _Jitter[2];
		uint32_t _FrameNumber;
		uint32_t _DebugFlags;
	};

	uint32_t currIndex = m_FrameIndexMod2;
	uint32_t prevIndex = currIndex ^ 1;	// history temporal index

	context.SetRootSignature(m_RootSignature);
	context.SetPipelineState(m_ResolveTAACS_Intel);

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& velocityBuffer = Graphics::s_BufferManager.m_VelocityBuffer;
	auto& currTemporal = Graphics::s_BufferManager.m_TemporalColor[currIndex];		// 写入目标
	auto& prevTemporal = Graphics::s_BufferManager.m_TemporalColor[prevIndex];
	auto& currDepth = Graphics::s_BufferManager.m_LinearDepth[currIndex];
	auto& prevDepth = Graphics::s_BufferManager.m_LinearDepth[prevIndex];

	context.TransitionResource(velocityBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(prevDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(currDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(prevTemporal, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(currTemporal, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	const auto width = colorBuffer.GetWidth();
	const auto height= colorBuffer.GetHeight();
	const float rcpWidth = 1.0f / width, rcpHeight = 1.0f / height;

	CSConstants csConstants =
	{
		Math::Vector4((float)width, (float)height, rcpWidth, rcpHeight),
		m_JitterDeltaX, m_JitterDeltaY,
		m_FrameIndexMod2,
	};

	// debug flags
	const uint32_t allowLongestVelocityVector = m_ITAA_AllowLongestVelocityVector;
	const uint32_t allowNeighborhoodSampling = m_ITAA_AllowNeighborhoodSampling;
	const uint32_t allowYCoCg = m_ITAA_AllowYCoCg;
	const uint32_t allowVarianceClipping = m_ITAA_AllowVarianceClipping;
	const uint32_t allowBicubicFilter = m_ITAA_AllowBicubicFilter;
	const uint32_t allowDepthThreshold = m_ITAA_AllowDepthThreshold;
	const uint32_t markNoHistoryPixels = m_ITAA_MarkNoHistoryPixels;
	csConstants._DebugFlags = 
		allowLongestVelocityVector << 6 | 
		allowNeighborhoodSampling << 5 |
		allowYCoCg << 4 |
		allowVarianceClipping << 3 |
		allowBicubicFilter << 2 | 
		allowDepthThreshold << 1 |
		markNoHistoryPixels;

	context.SetDynamicConstantBufferView((UINT)RSId::CBV, sizeof(csConstants), &csConstants);
	context.SetDynamicDescriptor((UINT)RSId::SRVTable, 0, velocityBuffer.GetSRV());
	context.SetDynamicDescriptor((UINT)RSId::SRVTable, 1, colorBuffer.GetSRV());
	context.SetDynamicDescriptor((UINT)RSId::SRVTable, 2, prevTemporal.GetSRV());
	context.SetDynamicDescriptor((UINT)RSId::SRVTable, 3, currDepth.GetSRV());
	context.SetDynamicDescriptor((UINT)RSId::SRVTable, 4, prevDepth.GetSRV());
	context.SetDynamicDescriptor((UINT)RSId::UAVTable, 0, currTemporal.GetUAV());
	
	context.Dispatch2D(width, height, 8, 8);	
}

// sharpen or copy image
void TemporalAA::SharpenImage(ComputeContext& context, ColorBuffer& temporalColor)
{
	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;

	context.TransitionResource(temporalColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	context.SetPipelineState(m_Sharpness >= 0.001f ? m_SharpenTAACS : m_ResolveTAACS);

	context.SetConstants((UINT)RSId::CBConstants, 1.0f + m_Sharpness, 0.25f * m_Sharpness, 
		m_TAAMethod == ETAAMethod::INTELTAA ? 0.0f : 1.0f);
	context.SetDynamicDescriptor((UINT)RSId::SRVTable, 0, temporalColor.GetSRV());
	context.SetDynamicDescriptor((UINT)RSId::UAVTable, 0, colorBuffer.GetUAV());

	context.Dispatch2D(colorBuffer.GetWidth(), colorBuffer.GetHeight());
	
}
