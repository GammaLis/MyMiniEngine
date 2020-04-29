#pragma once
#include "DepthBuffer.h"

namespace MyDirectX
{
	class GraphicsContext;

	class ShadowBuffer : public DepthBuffer
	{
	public:
		ShadowBuffer() {}

		void Create(ID3D12Device* pDevice, const std::wstring& name, uint32_t width, uint32_t height,
			DXGI_FORMAT format = DXGI_FORMAT_D16_UNORM,
			D3D12_GPU_VIRTUAL_ADDRESS vidMemPtr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN);

		D3D12_CPU_DESCRIPTOR_HANDLE GetSRV() const { return GetDepthSRV(); }

		void BeginRendering(GraphicsContext& context);
		void EndRendering(GraphicsContext& context);

	private:
		D3D12_VIEWPORT m_Viewport;
		D3D12_RECT m_Scissor;
	};
}

