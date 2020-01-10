#pragma once
#include "GpuResource.h"

namespace MyDirectX
{
	class PixelBuffer : public GpuResource
	{
	public:
		PixelBuffer() : m_Width(0), m_Height(0), m_ArraySize(0), m_Format(DXGI_FORMAT_UNKNOWN), m_BankRotation(0)
		{  }

		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }
		uint32_t GetDepth() const { return m_ArraySize; }
		const DXGI_FORMAT& GetFormat() const { return m_Format; }

		// has no effect on Windows
		void SetBankRotation(uint32_t rotationAmount) { m_BankRotation = rotationAmount; }

		// write the raw pixel buffer contents to a file
		// note that data is preceded by a 16-byte header: {DXGI_FORMAT, Pitch(in pixels), Width(in pixels), Height}
		void ExportToFile(const std::wstring& filePath);

	protected:
		D3D12_RESOURCE_DESC DescribeTex2D(uint32_t width, uint32_t height, uint32_t depthOrArraySize,
			uint32_t numMips, DXGI_FORMAT format, UINT flags);

		void AssociateWithResource(ID3D12Device* pDevice, const std::wstring& name, ID3D12Resource* pResource, D3D12_RESOURCE_STATES currentState);

		void CreateTextureResource(ID3D12Device* pDevice, const std::wstring& name, const D3D12_RESOURCE_DESC& resourceDesc,
			D3D12_CLEAR_VALUE clearValue, D3D12_GPU_VIRTUAL_ADDRESS vidMemPtr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN);

		static DXGI_FORMAT GetBaseFormat(DXGI_FORMAT format);
		static DXGI_FORMAT GetUAVFormat(DXGI_FORMAT format);
		static DXGI_FORMAT GetDSVFormat(DXGI_FORMAT format);
		static DXGI_FORMAT GetDepthFormat(DXGI_FORMAT format);
		static DXGI_FORMAT GetStencilFormat(DXGI_FORMAT format);
		static size_t BytesPerPixel(DXGI_FORMAT format);

		uint32_t m_Width;
		uint32_t m_Height;
		uint32_t m_ArraySize;
		DXGI_FORMAT m_Format;

		// ò�Ʋ���Windows���õ�
		uint32_t m_BankRotation;
	};

}
