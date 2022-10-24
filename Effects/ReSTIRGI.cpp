#include "ReSTIRGI.h"
#include "Graphics.h"
#include "GfxCommon.h"
#include "CommandContext.h"
#include "ProfilingScope.h"

// Compiled shader bytecode
#include "ReSTIR_TraceDiffuse.h"
#include "ReSTIR_Temporal.h"
#include "ReSTIR_Resolve.h"

using namespace MyDirectX;

void ReSTIRGI::Init(ID3D12Device* pDevice, const RootSignature& rootSig)
{
	// Shaders
#define CreatePSO(pso, shaderByteCode) \
	pso.SetRootSignature(rootSig); \
	pso.SetComputeShader(shaderByteCode, sizeof(shaderByteCode)); \
	pso.Finalize(pDevice);

	CreatePSO(m_TemporalResamplingPSO, ReSTIR_Temporal);
	CreatePSO(m_ResolvePSO, ReSTIR_Resolve);

#undef CreatePSO
	 
	// Resources
	const uint32_t W = GfxStates::s_NativeWidth, H = GfxStates::s_NativeHeight;
	uint32_t width = W, height = H;
	if (m_bHalfSize)
	{
		width >>= 1;
		height >>= 1;
	}

	{
		m_SampleRadiance.Create(pDevice, L"Sample Radiance", width, height, 1, DXGI_FORMAT_R11G11B10_FLOAT);
		m_SampleNormal.Create(pDevice, L"Sample Normal", width, height, 1, DXGI_FORMAT_R8G8B8A8_SNORM);
		m_SampleHitInfo.Create(pDevice, L"Sample HitInfo", width, height, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);

		m_TemporalSampleRadiance[0].Create(pDevice, L"Temporal Sample Radiance 0", width, height, 1, DXGI_FORMAT_R11G11B10_FLOAT);
		m_TemporalSampleRadiance[1].Create(pDevice, L"Temporal Sample Radiance 1", width, height, 1, DXGI_FORMAT_R11G11B10_FLOAT);
		m_TemporalSampleNormal[0].Create(pDevice, L"Temporal Sample Normal 0", width, height, 1, DXGI_FORMAT_R8G8B8A8_SNORM);
		m_TemporalSampleNormal[1].Create(pDevice, L"Temporal Sample Normal 1", width, height, 1, DXGI_FORMAT_R8G8B8A8_SNORM);
		m_TemporalSampleHitInfo[0].Create(pDevice, L"Temporal Sample HitInfo 0", width, height, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
		m_TemporalSampleHitInfo[1].Create(pDevice, L"Temporal Sample HitInfo 1", width, height, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
		m_TemporalRayOrigin[0].Create(pDevice, L"Temporal Ray Origin 0", width, height, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
		m_TemporalRayOrigin[1].Create(pDevice, L"Temporal Ray Origin 1", width, height, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
		m_TemporalReservoir[0].Create(pDevice, L"Temporal Reservoir 0", width, height, 1, DXGI_FORMAT_R32G32_UINT);
		m_TemporalReservoir[1].Create(pDevice, L"Temporal Reservoir 1", width, height, 1, DXGI_FORMAT_R32G32_UINT);

		m_Irradiance.Create(pDevice, L"Irradiance", W, H, 1, DXGI_FORMAT_R11G11B10_FLOAT);
	}

	// Views
	const UINT DescriptorStepSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	// SRVs
	{
		auto descHandle = m_SampleRadianceSRV;
		pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), m_SampleRadiance.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		descHandle += DescriptorStepSize;
		pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), m_SampleNormal.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		descHandle += DescriptorStepSize;
		pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), m_SampleHitInfo.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	// UAVs
	{
		auto descHandle = m_SampleRadianceUAV;
		pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), m_SampleRadiance.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		descHandle += DescriptorStepSize;
		pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), m_SampleNormal.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		descHandle += DescriptorStepSize;
		pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), m_SampleHitInfo.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		pDevice->CopyDescriptorsSimple(1, m_IrradianceUAV.GetCpuHandle(), m_Irradiance.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	// Params
	{
		m_bInited = true;
		m_bClearHistory = true;
	}

}

void ReSTIRGI::Shutdown()
{
	m_SampleRadiance.Destroy();
	m_SampleNormal.Destroy();
	m_SampleHitInfo.Destroy();

	for (int i = 0; i < 2; ++i)
	{
		m_TemporalSampleRadiance[i].Destroy();
		m_TemporalSampleNormal[i].Destroy();
		m_TemporalSampleHitInfo[i].Destroy();
		m_TemporalRayOrigin[i].Destroy();
		m_TemporalReservoir[i].Destroy();
	}

	m_Irradiance.Destroy();
}

void ReSTIRGI::Update()
{
}

// In ModelViewer.cpp actually
void ReSTIRGI::Render(ComputeContext& context)
{
}

D3D12_DISPATCH_RAYS_DESC ReSTIRGI::GetDispatchRayDesc(UINT width, UINT height)
{
	if (m_bHalfSize)
	{
		width /= 2;
		height /= 2;
	}
	return m_ReSTIRGIInputs.GetDispatchRayDesc(width, height);
}

void ReSTIRGI::InitUAVs(ID3D12Device* pDevice, UserDescriptorHeap& descHeap)
{
	// New samples
	// Candidate radiance/normal/hitInfo
	m_SampleRadianceUAV = descHeap.Alloc(3);
	// Temporal
	m_TemporalSampleRadianceUAV = descHeap.Alloc(5);
	// Spatial
	m_SpatialSampleRadianceUAV = descHeap.Alloc(5);

	m_IrradianceUAV = descHeap.Alloc();

}

void ReSTIRGI::InitSRVs(ID3D12Device* pDevice, UserDescriptorHeap& descHeap)
{
	// New samples
	m_SampleRadianceSRV = descHeap.Alloc(3);
	// Temporal
	m_TemporalSampleRadianceSRV = descHeap.Alloc(5);
	// Spatial
	m_SpatialSampleRadianceSRV = descHeap.Alloc(5);
	// Resolve
	m_ResolveSampleRadianceSRV = descHeap.Alloc(5);
}
