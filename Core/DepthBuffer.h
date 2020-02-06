#pragma once
#include "PixelBuffer.h"

namespace MyDirectX
{
	class DepthBuffer : public PixelBuffer
	{
	public:
		// reversed-Z: near - 1.0f, far - 0.0f
		DepthBuffer(float clearDepth = 0.0f, uint8_t clearStencil = 0)
			: m_ClearDepth(clearDepth), m_ClearStencil(clearStencil)
		{
			m_hDSV[0].ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
			m_hDSV[1].ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
			m_hDSV[2].ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
			m_hDSV[3].ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
			m_hDepthSRV.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
			m_hStencilSRV.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
		}

		// create a depth buffer. If an address is supplied, memory will not be allocated. The vmem address
		// allows you to alias buffers (which can be especially useful for reusing ESRAM acroos a frame.)
		void Create(ID3D12Device* pDevice, const std::wstring& name, uint32_t width, uint32_t height, DXGI_FORMAT format,
			D3D12_GPU_VIRTUAL_ADDRESS vidMemPtr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN);

		void Create(ID3D12Device* pDevice, const std::wstring& name, uint32_t width, uint32_t height, uint32_t numSamples,
			DXGI_FORMAT format, D3D12_GPU_VIRTUAL_ADDRESS vidMemPtr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN);

		// get pre-created CPU-visible descriptor handles
		const D3D12_CPU_DESCRIPTOR_HANDLE& GetDSV() const { return m_hDSV[0]; }
		const D3D12_CPU_DESCRIPTOR_HANDLE& GetDSV_DepthReadOnly() const { return m_hDSV[1]; }
		const D3D12_CPU_DESCRIPTOR_HANDLE& GetDSV_StencilReadOnly() const { return m_hDSV[2]; }
		const D3D12_CPU_DESCRIPTOR_HANDLE& GetDSV_ReadOnly() const { return m_hDSV[3]; }
		const D3D12_CPU_DESCRIPTOR_HANDLE& GetDepthSRV() const { return m_hDepthSRV; }
		const D3D12_CPU_DESCRIPTOR_HANDLE& GetStencilSRV() const { return m_hStencilSRV; }

		float GetClearDepth() const { return m_ClearDepth; }
		uint8_t GetClearStencil() const { return m_ClearStencil; }

	private:
		void CreateDerivedViews(ID3D12Device* pDevice, DXGI_FORMAT format);

		float m_ClearDepth;
		uint8_t m_ClearStencil;
		// flag - NONE, ReadOnlyDepth, ReadOnlyStenil, ReadOnlyDepth | ReadOnlyStencil
		D3D12_CPU_DESCRIPTOR_HANDLE m_hDSV[4];
		D3D12_CPU_DESCRIPTOR_HANDLE m_hDepthSRV;
		D3D12_CPU_DESCRIPTOR_HANDLE m_hStencilSRV;

	};

}