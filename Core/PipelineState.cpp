#include "PipelineState.h"
#include "RootSignature.h"
#include "Hash.h"
#include <thread>
#include <mutex>

namespace MyDirectX
{

	using Microsoft::WRL::ComPtr;

	static std::map<size_t, ComPtr<ID3D12PipelineState>> s_GraphicsPSOHashMap;
	static std::map<size_t, ComPtr<ID3D12PipelineState>> s_ComputePSOHashMap;

	/// PSO
	void PSO::DestroyAll()
	{
		s_GraphicsPSOHashMap.clear();
		s_ComputePSOHashMap.clear();
	}

	/// GraphicsPSO
	GraphicsPSO::GraphicsPSO(const wchar_t *name) : PSO(name)
	{
		ZeroMemory(&m_PSODesc, sizeof(m_PSODesc));
		m_PSODesc.NodeMask = 1;
		m_PSODesc.SampleMask = 0xFFFFFFFFu;
		m_PSODesc.SampleDesc.Count = 1;
		m_PSODesc.InputLayout.NumElements = 0;
	}

	void GraphicsPSO::SetRasterizerState(const D3D12_RASTERIZER_DESC& rasterizerDesc)
	{
		m_PSODesc.RasterizerState = rasterizerDesc;
	}

	void GraphicsPSO::SetBlendState(const D3D12_BLEND_DESC& blendDesc)
	{
		m_PSODesc.BlendState = blendDesc;
	}

	void GraphicsPSO::SetDepthStencilState(const D3D12_DEPTH_STENCIL_DESC& depthStencilDesc)
	{
		m_PSODesc.DepthStencilState = depthStencilDesc;
	}

	void GraphicsPSO::SetSampleMask(UINT sampleMask)
	{
		m_PSODesc.SampleMask = sampleMask;
	}

	void GraphicsPSO::SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType)
	{
		ASSERT(topologyType != D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED, "Can't draw with undefined topology");
		m_PSODesc.PrimitiveTopologyType = topologyType;
	}

	void GraphicsPSO::SetDepthTargetFormat(DXGI_FORMAT dsvFormat, UINT msaaCount, UINT msaaQuality)
	{
		SetRenderTargetFormats(0, nullptr, dsvFormat, msaaCount, msaaQuality);
	}

	void GraphicsPSO::SetRenderTargetFormat(DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, UINT msaaCount, UINT msaaQuality)
	{
		SetRenderTargetFormats(1, &rtvFormat, dsvFormat, msaaCount, msaaQuality);
	}

	void GraphicsPSO::SetRenderTargetFormats(UINT numRTVs, const DXGI_FORMAT* rtvFormats, DXGI_FORMAT dsvFormat, UINT msaaCount, UINT msaaQuality)
	{
		ASSERT(numRTVs == 0 || rtvFormats != nullptr, "Null format array conflicts with non-zero length");
		for (UINT i = 0; i < numRTVs; ++i)
		{
			ASSERT(rtvFormats[i] != DXGI_FORMAT_UNKNOWN);
			m_PSODesc.RTVFormats[i] = rtvFormats[i];
		}
		for (UINT i = numRTVs; i < m_PSODesc.NumRenderTargets; ++i)
			m_PSODesc.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
		m_PSODesc.NumRenderTargets = numRTVs;
		m_PSODesc.DSVFormat = dsvFormat;
		m_PSODesc.SampleDesc.Count = msaaCount;
		m_PSODesc.SampleDesc.Quality = msaaQuality;
	}

	void GraphicsPSO::SetInputLayout(UINT numElements, const D3D12_INPUT_ELEMENT_DESC* pInputElementDesc)
	{
		m_PSODesc.InputLayout.NumElements = numElements;

		if (numElements > 0)
		{
			D3D12_INPUT_ELEMENT_DESC* newElements = (D3D12_INPUT_ELEMENT_DESC*)malloc(sizeof(D3D12_INPUT_ELEMENT_DESC) * numElements);
			memcpy(newElements, pInputElementDesc, numElements * sizeof(D3D12_INPUT_ELEMENT_DESC));
			m_InputLayouts.reset((const D3D12_INPUT_ELEMENT_DESC*)newElements);
		}
		else
			m_InputLayouts = nullptr;
	}

	void GraphicsPSO::SetPrimitiveRestart(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE ibProps)
	{
		m_PSODesc.IBStripCutValue = ibProps;
	}

	void GraphicsPSO::Finalize(ID3D12Device* pDevice)
	{
		// -mf
		ASSERT(pDevice != nullptr);

		// make sure the root signature is finalized first
		m_PSODesc.pRootSignature = m_RootSignature->GetSignature();
		ASSERT(m_PSODesc.pRootSignature != nullptr);

		m_PSODesc.InputLayout.pInputElementDescs = nullptr;
		size_t hashCode = Utility::HashState(&m_PSODesc);
		hashCode = Utility::HashState(m_InputLayouts.get(), m_PSODesc.InputLayout.NumElements, hashCode);
		m_PSODesc.InputLayout.pInputElementDescs = m_InputLayouts.get();

		ID3D12PipelineState** PSORef = nullptr;
		bool firstCompile = false;
		{
			static std::mutex s_HashMapMutex;
			std::lock_guard<std::mutex> lockGuard(s_HashMapMutex);

			auto iter = s_GraphicsPSOHashMap.find(hashCode);
			// reserve space so the next inquiry will find that someone got here first
			if (iter == s_GraphicsPSOHashMap.end())
			{
				firstCompile = true;
				PSORef = s_GraphicsPSOHashMap[hashCode].GetAddressOf();
			}
			else
				PSORef = iter->second.GetAddressOf();
		}

		if (firstCompile)
		{
			// ASSERT(m_PSODesc.DepthStencilState.DepthEnable != (m_PSODesc.DSVFormat == DXGI_FORMAT_UNKNOWN));	// MS settings
			ASSERT_SUCCEEDED(pDevice->CreateGraphicsPipelineState(&m_PSODesc, IID_PPV_ARGS(&m_PSO)));
			s_GraphicsPSOHashMap[hashCode].Attach(m_PSO);
			m_PSO->SetName(m_Name);
		}
		else
		{
			while (*PSORef == nullptr)
				std::this_thread::yield();
			m_PSO = *PSORef;
		}
	}

	/// ComputePSO
	ComputePSO::ComputePSO(const wchar_t *name) : PSO(name)
	{
		ZeroMemory(&m_PSODesc, sizeof(m_PSODesc));
		m_PSODesc.NodeMask = 1;
	}

	void ComputePSO::Finalize(ID3D12Device* pDevice)
	{
		ASSERT(pDevice != nullptr);

		// make sure the root signature is finalized first
		m_PSODesc.pRootSignature = m_RootSignature->GetSignature();
		ASSERT(m_PSODesc.pRootSignature != nullptr);

		size_t hashCode = Utility::HashState(&m_PSODesc);

		ID3D12PipelineState** PSORef = nullptr;
		bool firstCompile = false;
		{
			static std::mutex s_HashMapMutex;
			std::lock_guard<std::mutex> lockGuard(s_HashMapMutex);

			auto iter = s_ComputePSOHashMap.find(hashCode);
			// reserve space so the next inquiry will find that someone got here first
			if (iter == s_ComputePSOHashMap.end())
			{
				firstCompile = true;
				PSORef = s_ComputePSOHashMap[hashCode].GetAddressOf();
			}
			else
				PSORef = iter->second.GetAddressOf();

			if (firstCompile)
			{
				ASSERT_SUCCEEDED(pDevice->CreateComputePipelineState(&m_PSODesc, IID_PPV_ARGS(&m_PSO)));
				s_ComputePSOHashMap[hashCode].Attach(m_PSO);
				m_PSO->SetName(m_Name);
			}
			else
			{
				while (*PSORef == nullptr)
					std::this_thread::yield();
				m_PSO = *PSORef;
			}
		}
	}
}
