#include "ColorBuffer.h"
#include "Graphics.h"

using namespace MyDirectX;

void ColorBuffer::CreateFromSwapChain(ID3D12Device* pDevice, const std::wstring& name, ID3D12Resource* pResource)
{
	AssociateWithResource(pDevice, name, pResource, D3D12_RESOURCE_STATE_PRESENT);

	// m_UAVHandle[0] = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	// pDevice->CreateUnorderedAccessView(m_pResource.Get(), nullptr, nullptr, m_UAVHandle[0]);

	m_RTVHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	pDevice->CreateRenderTargetView(pResource, nullptr, m_RTVHandle);
}

void ColorBuffer::Create(ID3D12Device* pDevice, const std::wstring& name, uint32_t width, uint32_t height, uint32_t numMips, DXGI_FORMAT format, D3D12_GPU_VIRTUAL_ADDRESS vidMemPtr)
{
	numMips = (numMips == 0 ? ComputeNumMips(width, height) : numMips);
	D3D12_RESOURCE_FLAGS flags = CombineResourceFlags();
	D3D12_RESOURCE_DESC resourceDesc = DescribeTex2D(width, height, 1, numMips, format, flags);

	resourceDesc.SampleDesc.Count = m_FragmentCount;
	resourceDesc.SampleDesc.Quality = 0;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = format;
	clearValue.Color[0] = m_ClearColor.R();
	clearValue.Color[1] = m_ClearColor.G();
	clearValue.Color[2] = m_ClearColor.B();
	clearValue.Color[3] = m_ClearColor.A();

	CreateTextureResource(pDevice, name, resourceDesc, clearValue, vidMemPtr);
	CreateDerivedViews(pDevice, format, 1, numMips);
}

void ColorBuffer::CreateArray(ID3D12Device* pDevice, const std::wstring& name, uint32_t width, uint32_t height, uint32_t arrayCount, DXGI_FORMAT format, D3D12_GPU_VIRTUAL_ADDRESS vidMemPtr)
{
	D3D12_RESOURCE_FLAGS flags = CombineResourceFlags();
	D3D12_RESOURCE_DESC resourceDesc = DescribeTex2D(width, height, arrayCount, 1, format, flags);

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = format;
	clearValue.Color[0] = m_ClearColor.R();
	clearValue.Color[1] = m_ClearColor.G();
	clearValue.Color[2] = m_ClearColor.B();
	clearValue.Color[3] = m_ClearColor.A();

	CreateTextureResource(pDevice, name, resourceDesc, clearValue, vidMemPtr);
	CreateDerivedViews(pDevice, format, arrayCount, 1);
}

void ColorBuffer::CreateDerivedViews(ID3D12Device* pDevice, DXGI_FORMAT format, uint32_t arraySize, uint32_t numMips)
{
	ASSERT(arraySize == 1 || numMips == 1, "We don't support auto-mips on texture arrays");

	m_NumMipmaps = numMips - 1;

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

	rtvDesc.Format = format;
	uavDesc.Format = GetUAVFormat(format);
	srvDesc.Format = format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	if (arraySize > 1)
	{
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
		rtvDesc.Texture2DArray.MipSlice = 0;
		rtvDesc.Texture2DArray.FirstArraySlice = 0;
		rtvDesc.Texture2DArray.ArraySize = (UINT)arraySize;

		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		uavDesc.Texture2DArray.MipSlice = 0;
		uavDesc.Texture2DArray.FirstArraySlice = 0;
		uavDesc.Texture2DArray.ArraySize = (UINT)arraySize;

		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		srvDesc.Texture2DArray.MipLevels = numMips;
		srvDesc.Texture2DArray.MostDetailedMip = 0;
		srvDesc.Texture2DArray.FirstArraySlice = 0;
		srvDesc.Texture2DArray.ArraySize = (UINT)arraySize;
	}
	else if (m_FragmentCount > 1)
	{
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
	}
	else
	{
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;

		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;

		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = numMips;
		srvDesc.Texture2D.MostDetailedMip = 0;
	}

	if (m_SRVHandle.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
	{
		m_RTVHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		m_SRVHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	ID3D12Resource* pResource = m_pResource.Get();

	// create the render target view
	pDevice->CreateRenderTargetView(pResource, &rtvDesc, m_RTVHandle);

	// create the shder resource view
	pDevice->CreateShaderResourceView(pResource, &srvDesc, m_SRVHandle);

	if (m_FragmentCount > 1)
		return;

	// create the UAVs for each mip level (RWTexture2D)
	for (uint32_t i = 0; i < numMips; ++i)
	{
		if (m_UAVHandle[i].ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
		{
			m_UAVHandle[i] = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}

		pDevice->CreateUnorderedAccessView(pResource, nullptr, &uavDesc, m_UAVHandle[i]);

		uavDesc.Texture2D.MipSlice++;
	}
}
