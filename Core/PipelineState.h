#pragma once
#include "pch.h"

namespace MyDirectX
{
	class RootSignature;

	class PSO
	{
	public:
		PSO(const wchar_t* name) : m_Name(name), m_RootSignature(nullptr), m_PSO(nullptr) {  }

		static void DestroyAll();

		void SetRootSignature(const RootSignature &bindMappings)
		{
			m_RootSignature = &bindMappings;
		}

		const RootSignature& GetRootSignature() const
		{
			ASSERT(m_RootSignature != nullptr);
			return *m_RootSignature;
		}

		ID3D12PipelineState* GetPipelineStateObject() const { return m_PSO; }
		
	protected:
		const wchar_t *m_Name;
		const RootSignature* m_RootSignature;
		ID3D12PipelineState* m_PSO;		// 这里并不管理生命周期
	};

	class GraphicsPSO : public PSO
	{
	public:
		// start with empty state
		GraphicsPSO(const wchar_t *name = L"Unnamed Graphics PSO");

		void SetRasterizerState(const D3D12_RASTERIZER_DESC& rasterizerDesc);
		void SetBlendState(const D3D12_BLEND_DESC& blendDesc);
		void SetDepthStencilState(const D3D12_DEPTH_STENCIL_DESC& depthStencilDesc);
		void SetSampleMask(UINT sampleMask);
		void SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType);
		void SetDepthTargetFormat(DXGI_FORMAT dsvFormat, UINT msaaCount = 1, UINT msaaQuality = 0);
		void SetRenderTargetFormat(DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, UINT msaaCount = 1, UINT msaaQuality = 0);
		void SetRenderTargetFormats(UINT numRTVs, const DXGI_FORMAT* rtvFormats, DXGI_FORMAT dsvFormat, UINT msaaCount = 1, UINT msaaQuality = 0);
		void SetInputLayout(UINT numElements, const D3D12_INPUT_ELEMENT_DESC* pInputElementDesc);
		void SetPrimitiveRestart(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE ibProps);

		// these const_casts shouldn't be necessary, but we need to fix the API to accept "const void* pShaderBytecode"  
		void SetVertexShader(const void* binary, size_t size) { m_PSODesc.VS = CD3DX12_SHADER_BYTECODE(const_cast<void*>(binary), size); }
		void SetPixelShader(const void* binary, size_t size) { m_PSODesc.PS = CD3DX12_SHADER_BYTECODE(const_cast<void*>(binary), size); }
		void SetGeometryShader(const void* binary, size_t size) { m_PSODesc.GS = CD3DX12_SHADER_BYTECODE(const_cast<void*>(binary), size); }
		void SetHullShader(const void *binary, size_t size) { m_PSODesc.HS = CD3DX12_SHADER_BYTECODE(const_cast<void*>(binary), size); }
		void SetDomainShader(const void* binary, size_t size) { m_PSODesc.DS = CD3DX12_SHADER_BYTECODE(const_cast<void*>(binary), size); }

		void SetVertexShader(const D3D12_SHADER_BYTECODE& binary) { m_PSODesc.VS = binary; }
		void SetPixelShader(const D3D12_SHADER_BYTECODE& binary) { m_PSODesc.PS = binary; }
		void SetGeometryShader(const D3D12_SHADER_BYTECODE& binary) { m_PSODesc.GS = binary; }
		void SetHullShader(const D3D12_SHADER_BYTECODE& binary) { m_PSODesc.HS = binary; }
		void SetDomainShader(const D3D12_SHADER_BYTECODE& binary) { m_PSODesc.DS = binary; }

		// perform validation and compute a hash value for fast state block comparisons
		void Finalize(ID3D12Device *pDevice);

	private:
		D3D12_GRAPHICS_PIPELINE_STATE_DESC m_PSODesc;
		std::shared_ptr<const D3D12_INPUT_ELEMENT_DESC> m_InputLayouts;
	};

	class ComputePSO : public PSO
	{
	public:
		ComputePSO(const wchar_t *name = L"Unnamed Compute PSO");

		void SetComputeShader(const void* binary, size_t size) { m_PSODesc.CS = CD3DX12_SHADER_BYTECODE(const_cast<void*>(binary), size); }
		void SetComputeShader(const D3D12_SHADER_BYTECODE& binary) { m_PSODesc.CS = binary; }

		void Finalize(ID3D12Device *pDevice);

	private:
		D3D12_COMPUTE_PIPELINE_STATE_DESC m_PSODesc;
	};

}
