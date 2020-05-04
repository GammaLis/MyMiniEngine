#pragma once
#include "PixelBuffer.h"
#include "Color.h"

namespace MyDirectX
{
	class Graphics;
	class CommandContext;

	class ColorBuffer : public PixelBuffer
	{
	public:
		ColorBuffer(Color clearColor = Color(0.0f, 0.0f, 0.0f, 0.0f))
			: m_ClearColor(clearColor), m_NumMipmaps(0), m_FragmentCount(1), m_SampleCount(1)
		{
			m_SRVHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
			m_RTVHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
			std::memset(m_UAVHandle, 0xFF, sizeof(m_UAVHandle));
		}

		// create a color buffer from a swap chain buffer. Unordered access is restricted
		void CreateFromSwapChain(ID3D12Device *pDevice, const std::wstring& name, ID3D12Resource* pResource);

		// create a color buffer. If an address is supplied, memory will not be allocated. The vmem address
		// allows you to alias buffers (which can be especially useful for reusing ESRAM across a frame)
		// ESRAM - XBOX
		void Create(ID3D12Device* pDevice, const std::wstring& name, uint32_t width, uint32_t height, uint32_t numMips, DXGI_FORMAT format,
			D3D12_GPU_VIRTUAL_ADDRESS vidMemPtr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN);

		void CreateArray(ID3D12Device* pDevice, const std::wstring& name, uint32_t width, uint32_t height, uint32_t arrayCount,
			DXGI_FORMAT format, D3D12_GPU_VIRTUAL_ADDRESS vidMemPtr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN);

		// get pre-created CPU-visible descriptor handles
		const D3D12_CPU_DESCRIPTOR_HANDLE& GetSRV() const { return m_SRVHandle; }
		const D3D12_CPU_DESCRIPTOR_HANDLE& GetRTV() const { return m_RTVHandle; }
		const D3D12_CPU_DESCRIPTOR_HANDLE& GetUAV() const { return m_UAVHandle[0]; }
		const D3D12_CPU_DESCRIPTOR_HANDLE* GetMipUAVs() const { return m_UAVHandle; }

		void SetClearColor(Color clearColor) { m_ClearColor = clearColor; }

		void SetMsaaMode(uint32_t numColorSamples, uint32_t numCoverageSamples)
		{
			ASSERT(numCoverageSamples >= numColorSamples);
			m_FragmentCount = numColorSamples;
			m_SampleCount = numCoverageSamples;
		}

		Color GetClearColor() const { return m_ClearColor; }

		// this will work for all texture size, but it's recommended for speed and quality that you use
		// dimensions with power of 2 (but not necessarily square.) Pass 0 for arrayCount to reserve
		// space for mips at creation time
		void GenerateMipMaps(CommandContext& context, Graphics &gfxCore);

	protected:
		D3D12_RESOURCE_FLAGS CombineResourceFlags() const
		{
			D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

			if (flags == D3D12_RESOURCE_FLAG_NONE && m_FragmentCount == 1)
			{
				flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			}

			return D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | flags;
		}

		// compute the number of texture levels needed to reduce to 1x1. This uses _BitScanReverse
		// to find the highest set bit. Each dimension reduces by half and truncates bits. 
		// The dimension 256 (0x100) has 9 mip levels, same as the dimension 511 (0x1FF)
		static inline uint32_t ComputeNumMips(uint32_t width, uint32_t height)
		{
			uint32_t highBit;
			_BitScanReverse((unsigned long*)&highBit, width | height);
			return highBit + 1;
		}

		void CreateDerivedViews(ID3D12Device* pDevice, DXGI_FORMAT format, uint32_t arraySize, uint32_t numMips = 1);
		
		Color m_ClearColor;
		D3D12_CPU_DESCRIPTOR_HANDLE m_SRVHandle;
		D3D12_CPU_DESCRIPTOR_HANDLE m_RTVHandle;
		D3D12_CPU_DESCRIPTOR_HANDLE m_UAVHandle[12];
		uint32_t m_NumMipmaps;		// number of texture sublevels
		uint32_t m_FragmentCount;
		uint32_t m_SampleCount;
	};

}
