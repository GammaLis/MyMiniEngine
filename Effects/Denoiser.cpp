#include "Denoiser.h"
#include "Graphics.h"
#include "CommandContext.h"
#include "TemporalEffects.h"
#include "UniformBuffers.h"
#include "ProfilingScope.h"

// Compiled shader bytecode
#include "TemporalReverseReprojectCS.h"
#include "CalculateMeanVarianceCS.h"
#include "TemporalBlendFrameCS.h"
#include "AtrousWaveletBilateralFilterCS.h"

using namespace MyDirectX;
using namespace MyDirectX::UniformBuffers;

/// Globals & Statics

struct alignas(16) DenoisingCommonParameters
{
	XMFLOAT4 BufferSizeAndInvSize;
	int FilterStep;
	float MinSmoothFactor;
	float StdDevGamma;
	float MinStdDevTolerance;
	float ClampDifferenceToTsppScale;
	int MinTsppToUseTemporalVariance;
	float MinVarianceToDenoise;
	float DepthWeightCutoff;
	int BlurStrength_MaxTspp;
	float BlurDecayStrength;
};
static DenoisingCommonParameters s_DenosingParams;
static uint32_t s_FilterPasses = 2;

static uint32_t s_MaxTspp = 33; // [1: 1: 100]
static bool s_bEnableTemporalClamp = true;
// Std dev gamma - scales std dev on clamping. Larger values gives more clamp tolerance, lower values give less tolerance (i.e. clamp quicker, better for motion)
static float s_StdDevGamma = 0.6f; // [0.1f: 0.1f: 10.0f]
// Minimum std.dev used in clamping
// - higher values helps prevent clamping, especially on checkerboard 1spp sampling of ~0.1 prevent random clamping
// - higher values limit clamping due to true change and increase ghosting
static float s_MinStdDev = 0.05f; // [0.f: 0.01f: 1.f]
static float s_ClampDiffToTsppScale = 4.0f; // [0.f: 0.05f: 10.0f]
static float s_TemporalDepthSigma = 1.0f; // [0.f: 0.01f: 10.0f]

static bool s_bEnableAdaptiveKernelSize = true;
static uint32_t s_NumCycles = 3; // [1: 1: 10]
static uint32_t s_MinKernelWidth = 3; // [3: 101]
static float s_ValueSigma = 1.0f; // [0.0f: 0.1f: 30.0f]
static float s_SpatioDepthSigma = 1.0f; // [0.0f: 0.02f; 10.0f]
static float s_DepthWeightCutoff = 0.2f; // [0.0f: 0.01f: 2.0f]
static float s_NormalSigma = 64; // [0: 4: 256]
static float s_MinVarianceToDenoise = 0.0f; // [0.0f: 0.01f: 1.f]
static bool s_bUseSmoothedVariance = false;

static uint32_t s_LocalVarianceKernelWidth = 9; // [3: 2: 9]
static uint32_t s_MinTsppForTemporalVariance = 4; // [1: 40]

bool Denoiser::s_bEnabled = true;

void Denoiser::Init(ID3D12Device* pDevice)
{
	// Root signature
	{
		m_RootSig.Reset((UINT)RSId::Num, 2);
		m_RootSig[(UINT)RSId::ViewUniforms].InitAsConstantBuffer(g_ViewUniformBufferParamsRegister, g_UniformBufferParamsSpace);
		m_RootSig[(UINT)RSId::DenosingUniforms].InitAsConstantBuffer(0);
		m_RootSig[(UINT)RSId::CurrentInputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,  0, 10);
		m_RootSig[(UINT)RSId::HistoryInputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 10);
		m_RootSig[(UINT)RSId::Outputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 10);
		m_RootSig.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearClampDesc);
		m_RootSig.InitStaticSampler(1, Graphics::s_CommonStates.SamplerPointClampDesc);
		m_RootSig.Finalize(pDevice, L"Denoising RS");
	}

	// Pipelines
	{

#define CreatePSO(pso, shaderByteCode) \
	pso.SetRootSignature(m_RootSig); \
	pso.SetComputeShader(shaderByteCode, sizeof(shaderByteCode)); \
	pso.Finalize(pDevice);

		// Calculate mean&variance
		CreatePSO(m_TemporalReprojectPSO, TemporalReverseReprojectCS);
		CreatePSO(m_CalculdateMeanVariancePSO, CalculateMeanVarianceCS);
		CreatePSO(m_TemporalBlendFramePSO, TemporalBlendFrameCS);
		CreatePSO(m_AtrousWaveletFilterPSO, AtrousWaveletBilateralFilterCS);


#undef CreatePSO
	}

	const auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	const uint32_t width = colorBuffer.GetWidth(), height = colorBuffer.GetHeight();
	// Resources
	{
		m_DenoisingCommonParameters.Create(pDevice, L"Denosing Common Params", 1, sizeof(DenoisingCommonParameters));

		m_Reprojected.Create(pDevice, L"Temporal Reprojedted", width, height, 1, DXGI_FORMAT_R16G16B16A16_UINT);
		m_LocalMeanVariance.Create(pDevice, L"Mean Variance", width, height, 1, DXGI_FORMAT_R16G16_FLOAT);
		m_Variance.Create(pDevice, L"Variance", width, height, 1, DXGI_FORMAT_R16_FLOAT);

		m_LumaMoments[0].Create(pDevice, L"Luma Moments 0", width, height, 1, DXGI_FORMAT_R16G16_FLOAT);
		m_LumaMoments[1].Create(pDevice, L"Luma Moments 1", width, height, 1, DXGI_FORMAT_R16G16_FLOAT);

		m_Tspp[0].Create(pDevice, L"Tspp 0", width, height, 1, DXGI_FORMAT_R8_UINT);
		m_Tspp[1].Create(pDevice, L"Tspp 1", width, height, 1, DXGI_FORMAT_R8_UINT);

		m_Intermediate.Create(pDevice, L"Intermediate", width, height, 1, colorBuffer.GetFormat()); // GfxStates::s_DefaultHdrColorFormat
	}

	m_bInited = true;
	m_bActive = true;
	m_bClearHistory = true;
}

void Denoiser::Shutdown()
{
	m_DenoisingCommonParameters.Destroy();

	m_Reprojected.Destroy();
	m_LocalMeanVariance.Destroy();
	m_Variance.Destroy();
	m_Intermediate.Destroy();

	m_LumaMoments[0].Destroy();
	m_LumaMoments[1].Destroy();

	m_Tspp[0].Destroy();
	m_Tspp[1].Destroy();	
}

void Denoiser::Update(uint64_t frameIndex)
{
	const uint32_t W = GfxStates::s_NativeWidth, H = GfxStates::s_NativeHeight;

	// Update uniforms
	{
		m_FrameIndexMod2 = (int)(frameIndex & 1);

		s_DenosingParams.BufferSizeAndInvSize = XMFLOAT4((float)W, (float)H, 1.0f / W, 1.0f / H);
		s_DenosingParams.MinSmoothFactor = 1.0f / s_MaxTspp;
		s_DenosingParams.ClampDifferenceToTsppScale = s_ClampDiffToTsppScale;
		s_DenosingParams.StdDevGamma = s_StdDevGamma;
		s_DenosingParams.MinStdDevTolerance = s_MinStdDev;
		s_DenosingParams.MinTsppToUseTemporalVariance = s_MinTsppForTemporalVariance;
		s_DenosingParams.FilterStep = 1;
		s_DenosingParams.MinVarianceToDenoise = s_MinVarianceToDenoise;
		s_DenosingParams.DepthWeightCutoff = s_DepthWeightCutoff;
		// ...

		uint32_t filterStep = 1;
		int pass = 0;
		// for (int pass = 0; pass < s_FilterPasses; ++pass)
		{
			s_DenosingParams.FilterStep = filterStep;
			m_DenoisingCommonParameters.CopyToGpu(&s_DenosingParams, sizeof(s_DenosingParams), pass);

			filterStep *= 2;
		}
	}
}

void Denoiser::Render(CommandContext& context)
{
	ProfilingScope profilingScope(L"Denoising", context);

	ComputeContext &computeContext = context.GetComputeContext();

	computeContext.SetRootSignature(m_RootSig);
	computeContext.SetConstantBuffer((UINT)RSId::ViewUniforms, g_ViewUniformBuffer.GetGpuPointer());
	computeContext.SetConstantBuffer((UINT)RSId::DenosingUniforms, m_DenoisingCommonParameters.GetGpuPointer());

	{
		auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
		auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
		auto& normalBuffer = Graphics::s_BufferManager.m_SceneNormalBuffer;
		auto& velocityBuffer = Graphics::s_BufferManager.m_VelocityBuffer;

		auto& colorHistory = Graphics::s_BufferManager.m_ColorHistory;
		auto& depthHistory = Graphics::s_BufferManager.m_DepthHistory;
		auto& normalHistory = Graphics::s_BufferManager.m_NormalHistory;

		computeContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(normalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(velocityBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		computeContext.TransitionResource(colorHistory, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(depthHistory, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(normalHistory, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		
		computeContext.FlushResourceBarriers();
		
		computeContext.SetDynamicDescriptor((UINT)RSId::CurrentInputs, 0, colorBuffer.GetSRV());
		computeContext.SetDynamicDescriptor((UINT)RSId::CurrentInputs, 1, depthBuffer.GetDepthSRV());
		computeContext.SetDynamicDescriptor((UINT)RSId::CurrentInputs, 2, normalBuffer.GetSRV());
		computeContext.SetDynamicDescriptor((UINT)RSId::CurrentInputs, 3, velocityBuffer.GetSRV());

		computeContext.SetDynamicDescriptor((UINT)RSId::HistoryInputs, 0, colorHistory.GetSRV());
		computeContext.SetDynamicDescriptor((UINT)RSId::HistoryInputs, 1, depthHistory.GetSRV());
		computeContext.SetDynamicDescriptor((UINT)RSId::HistoryInputs, 2, normalHistory.GetSRV());
	}

	TemporalSupersamplingReverseReproject(computeContext);
	TemporalSupersamplingBlendWithCurrentFrame(computeContext);
}

void Denoiser::SetActive(bool bActive)
{
	if (s_bEnabled)
	{
		if (bActive != m_bActive && bActive == true)
			m_bClearHistory = true;

		m_bActive = bActive;
	}
}

// DEBUG
D3D12_CPU_DESCRIPTOR_HANDLE Denoiser::GetDebugOutput() const
{
	auto output = m_LocalMeanVariance.GetSRV();

	output = m_Reprojected.GetSRV();

	output = m_Intermediate.GetSRV();

	return output;
}

void Denoiser::TemporalSupersamplingReverseReproject(ComputeContext& context)
{
	ProfilingScope profilingScope(L"Temporal Supersampling Reproject", context);

	const uint32_t W = GfxStates::s_NativeWidth, H = GfxStates::s_NativeHeight;

	int prevFrameIndex = m_FrameIndexMod2 ^ 1;
	auto& lumaMomentsHistory = m_LumaMoments[prevFrameIndex];
	auto& tsppHistory = m_Tspp[prevFrameIndex];

	int currFrameIndex = m_FrameIndexMod2;
	auto& updateTspp = m_Tspp[currFrameIndex];

	if (m_bClearHistory)
	{
		context.TransitionResource(lumaMomentsHistory, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.TransitionResource(tsppHistory, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.ClearUAV(lumaMomentsHistory);
		context.ClearUAV(tsppHistory);
		m_bClearHistory = false;
	}

	context.TransitionResource(lumaMomentsHistory, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(tsppHistory, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	context.TransitionResource(m_Reprojected, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.TransitionResource(updateTspp, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	
	context.SetDynamicDescriptor((UINT)RSId::HistoryInputs, 3, lumaMomentsHistory.GetSRV());
	context.SetDynamicDescriptor((UINT)RSId::HistoryInputs, 4, tsppHistory.GetSRV());

	context.SetDynamicDescriptor((UINT)RSId::Outputs, 0, m_Reprojected.GetUAV());
	context.SetDynamicDescriptor((UINT)RSId::Outputs, 3, updateTspp.GetUAV());

	context.SetPipelineState(m_TemporalReprojectPSO);
	context.Dispatch2D(W, H);

	context.TransitionResource(m_Reprojected, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.InsertUAVBarrier(updateTspp);
}

void Denoiser::TemporalSupersamplingBlendWithCurrentFrame(ComputeContext& context)
{
	ProfilingScope profilingScope(L"Temporal Supersampling Blend", context);

	const uint32_t W = GfxStates::s_NativeWidth, H = GfxStates::s_NativeHeight;

	int currFrameIndex = m_FrameIndexMod2;
	auto& updateLumaMoments = m_LumaMoments[currFrameIndex];
	auto& updateTspp = m_Tspp[currFrameIndex];

	// Calculate local mean variance
	{
		context.TransitionResource(m_LocalMeanVariance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		CalculateLocalMeanVariance(context);
		context.TransitionResource(m_LocalMeanVariance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		// context.InsertUAVBarrier(m_LocalMeanVariance);
	}

	// Temporal blend with current frame
	{
		context.TransitionResource(m_Variance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.TransitionResource(m_Intermediate, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.TransitionResource(updateLumaMoments, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.TransitionResource(updateTspp, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		context.SetPipelineState(m_TemporalBlendFramePSO);

		context.SetDynamicDescriptor((UINT)RSId::CurrentInputs, 5, m_LocalMeanVariance.GetSRV());
		context.SetDynamicDescriptor((UINT)RSId::CurrentInputs, 6, m_Reprojected.GetSRV());

		context.SetDynamicDescriptor((UINT)RSId::Outputs, 0, m_Variance.GetUAV());
		context.SetDynamicDescriptor((UINT)RSId::Outputs, 1, m_Intermediate.GetUAV());
		context.SetDynamicDescriptor((UINT)RSId::Outputs, 2, updateLumaMoments.GetUAV());
		context.SetDynamicDescriptor((UINT)RSId::Outputs, 3, updateTspp.GetUAV());

		context.Dispatch2D(W, H);

		context.TransitionResource(m_Variance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		context.TransitionResource(m_Intermediate, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}

	{
		ApplyAtrouWaveletFilter(context);
	}

	// TODO...
	// context.FlushResourceBarriers();
}

void Denoiser::CalculateLocalMeanVariance(ComputeContext& context)
{
	ProfilingScope profilingScope(L"Calculate Local Mean Variance", context);

	context.SetPipelineState(m_CalculdateMeanVariancePSO);

	context.SetDynamicDescriptor((UINT)RSId::Outputs, 0, m_LocalMeanVariance.GetUAV());

	const uint32_t W = GfxStates::s_NativeWidth, H = GfxStates::s_NativeHeight;
	context.Dispatch2D(W, H);
}

// Applies a single pass of Atrous wavelet transform filter
void Denoiser::ApplyAtrouWaveletFilter(ComputeContext& context)
{
	ProfilingScope profilingScope(L"Apply Atrou Wavelet Filter", context);

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	context.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	context.SetPipelineState(m_AtrousWaveletFilterPSO);

	context.SetDynamicDescriptor((UINT)RSId::CurrentInputs, 0, m_Intermediate.GetSRV());
	context.SetDynamicDescriptor((UINT)RSId::CurrentInputs, 7, m_Variance.GetSRV());

	context.SetDynamicDescriptor((UINT)RSId::Outputs, 0, colorBuffer.GetUAV());

	const uint32_t W = colorBuffer.GetWidth(), H = colorBuffer.GetHeight();
	context.Dispatch2D(W, H);
}
