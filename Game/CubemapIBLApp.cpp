#include "CubemapIBLApp.h"
#include "GfxCommon.h"
#include "TextureManager.h"
#include "CommandContext.h"

// compiled shader bytecode
#include "CubemapIBL.h"

using namespace MyDirectX;

void CubemapIBLApp::Init(const std::wstring& fileName, UINT width, UINT height, DXGI_FORMAT format)
{
	IComputingApp::Init();

	auto pDevice = Graphics::s_Device;

	// Root signatures
	{
		D3D12_SAMPLER_DESC samplerDesc = {};
		samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.MaxAnisotropy = 16;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		samplerDesc.MinLOD = 0.0f;
		samplerDesc.MipLODBias = 0.0f;
		samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

		m_IrradianceMapRS.Reset(3, 1);
		m_IrradianceMapRS[0].InitAsConstants(0, 2);
		m_IrradianceMapRS[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
		m_IrradianceMapRS[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		m_IrradianceMapRS.InitStaticSampler(0, samplerDesc);
		m_IrradianceMapRS.Finalize(pDevice, L"IrradianceMapRS");
	}

	// PSOs
	{
		m_IrradianceMapPSO.SetRootSignature(m_IrradianceMapRS);
		m_IrradianceMapPSO.SetComputeShader(CubemapIBL, sizeof(CubemapIBL));
		m_IrradianceMapPSO.Finalize(pDevice);
	}

	// Resources
	{
		auto filePath = fileName;
		auto pos = filePath.rfind('.');
		if (pos != std::wstring::npos)
			filePath = filePath.substr(0, pos);	// È¥³ýÀ©Õ¹Ãû
		const auto texture = Graphics::s_TextureManager.LoadFromFile(pDevice, filePath);
		m_SRV = texture->GetSRV();

		m_Width = width, m_Height = height;
		m_IrradianceMap.Create(pDevice, L"IrradianceMap", width, height, 1, format);
	}
}

void CubemapIBLApp::Run()
{
	auto& computeContext = ComputeContext::Begin();

	computeContext.TransitionResource(m_IrradianceMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	computeContext.SetPipelineState(m_IrradianceMapPSO);
	computeContext.SetRootSignature(m_IrradianceMapRS);
	computeContext.SetConstants(0, m_Width, m_Height);
	computeContext.SetDynamicDescriptor(1, 0, m_SRV);
	computeContext.SetDynamicDescriptor(2, 0, m_IrradianceMap.GetUAV());

	computeContext.Dispatch2D(m_Width, m_Height, GroupSize, GroupSize);

	computeContext.Finish(true);
}

int CubemapIBLApp::SaveToFile(const std::string& fileName)
{
	return m_IrradianceMap.ExportToImage(Graphics::s_Device, fileName);
}
