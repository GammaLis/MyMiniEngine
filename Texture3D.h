#pragma once
#include "GpuResource.h"

namespace MyDirectX
{
	class Graphics;
	class CommandContext;

	class Texture3D : public GpuResource
	{
	public:
		Texture3D() : m_Width{1}, m_Height{1}, m_Depth{1}, m_Format{DXGI_FORMAT_UNKNOWN}
		{ 
			m_SRVHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
			std::memset(m_UAVHandle, 0xFF, sizeof(m_UAVHandle));
		}

		void Create(ID3D12Device* pDevice, const std::wstring& name, uint32_t width, uint32_t height, uint32_t depth, uint32_t numMips,
			DXGI_FORMAT format);

		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }
		uint32_t GetDepth() const { return m_Depth; }
		uint32_t GetMipNums() const { return m_NumMipmaps; }
		const DXGI_FORMAT& GetFormat() const { return m_Format; }

		// get pre-created CPU-visible descriptor handles
		const D3D12_CPU_DESCRIPTOR_HANDLE& GetSRV() const { return m_SRVHandle; }
		const D3D12_CPU_DESCRIPTOR_HANDLE& GetUAV() const { return m_UAVHandle[0]; }
		const D3D12_CPU_DESCRIPTOR_HANDLE* GetMipUAVs() const { return m_UAVHandle; }

		void GenerateMipMaps(CommandContext& context, Graphics& gfxCore);

		// PixelBuffer.h
		static DXGI_FORMAT GetBaseFormat(DXGI_FORMAT format);
		static DXGI_FORMAT GetUAVFormat(DXGI_FORMAT format);

		static inline uint32_t ComputeNumMips(uint32_t width, uint32_t height, uint32_t depth)
		{
			uint32_t highBit;
			_BitScanReverse((unsigned long*)&highBit, width | height | depth);
			return highBit + 1;
		}

	protected:
		D3D12_RESOURCE_DESC DescribeTex3D(uint32_t width, uint32_t height, uint32_t depth, uint32_t numMips,
			DXGI_FORMAT format, UINT flags);

		void CreateTextureResource(ID3D12Device* pDevice, const std::wstring& name, const D3D12_RESOURCE_DESC& resourceDesc);
		void CreateDerivedViews(ID3D12Device *pDevice, DXGI_FORMAT format, uint32_t numMips);

	private:

		uint32_t m_Width;
		uint32_t m_Height;
		uint32_t m_Depth;
		uint32_t m_NumMipmaps = 0;	// ³ýÈ¥MipLevel = 0£¬ÆäËüMipLevels
		DXGI_FORMAT m_Format;

		D3D12_CPU_DESCRIPTOR_HANDLE m_SRVHandle;
		D3D12_CPU_DESCRIPTOR_HANDLE m_UAVHandle[12];	// mipmaps

	};

}
