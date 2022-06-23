#include "Texture3D.h"
#include "Graphics.h"
#include "CommandContext.h"
#include "Math.h"

using namespace MyDirectX;

void Texture3D::Create(ID3D12Device* pDevice, const std::wstring& name, uint32_t width, uint32_t height, uint32_t depth, uint32_t numMips, DXGI_FORMAT format)
{
	ASSERT(Math::IsPowerOfTwo(width) && Math::IsPowerOfTwo(height) && Math::IsPowerOfTwo(depth),
		"Now only support PowerOfTwo 3D textures!");

	numMips = (numMips == 0 ? ComputeNumMips(width, height, depth) : numMips);
	D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	D3D12_RESOURCE_DESC resourceDesc = DescribeTex3D(width, height, depth, numMips, format, flags);

	CreateTextureResource(pDevice, name, resourceDesc);
	CreateDerivedViews(pDevice, format, numMips);

}

void Texture3D::GenerateMipMaps(CommandContext& context)
{
	if (m_NumMipmaps == 0)
		return;

	ComputeContext& computeContext = context.GetComputeContext();

	computeContext.TransitionResource(*this, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	computeContext.SetRootSignature(Graphics::s_CommonStates.Generate3DTexMipsRS);
	computeContext.SetDynamicDescriptor(1, 0, m_SRVHandle);

	for (uint32_t topMip = 0; topMip < m_NumMipmaps; )
	{
		uint32_t srcWidth = m_Width >> topMip;
		uint32_t srcHieght = m_Height >> topMip;
		uint32_t srcDepth = m_Depth >> topMip;
		uint32_t dstWidth = srcWidth >> 1;
		uint32_t dstHeight = srcHieght >> 1;
		uint32_t dstDepth = srcDepth >> 1;

		// determine if the first downsample is more than 2: 1. This happens whenever the source
		// width or height is odd.
		uint32_t NonPowerOfTwo = (srcWidth & 1) | (srcHieght & 1) << 1;
		//if (m_Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
		//	computeContext.SetPipelineState(gfxCore.m_GenerateMipsGammaPSO);
		//else
		//	computeContext.SetPipelineState(gfxCore.m_GenerateMipsLinearPSO);
		computeContext.SetPipelineState(Graphics::s_CommonStates.Generate3DTexMipsPSO);

		// we can downsample up to 4 times, but if the ratio between levels is not exactly 2:1, we have to
		// shift out blend weights, which gets complicated or expensive. Maybe we can update the code later
		// to compute sample weights for each successive downsample. We use _BitScanForward to count number
		// of zeros in the low bits. Zeros indicate we can divide by 2 without truncating.
		uint32_t AdditionalMips;
		_BitScanForward((unsigned long*)&AdditionalMips,
			(dstWidth == 1 ? dstHeight : dstWidth) | (dstHeight == 1 ? dstWidth : dstHeight));
		uint32_t numMips = 1 + (AdditionalMips > 2 ? 2 : AdditionalMips);
		if ((topMip + numMips) > m_NumMipmaps)
			numMips = m_NumMipmaps - topMip;

		// these are clamped to 1 after computing additional mips because clamped dimensions should 
		// not limit us from downsampling multiple times. (E.g. 16x1 -> 8x1 -> 4x1 -> 2x1 -> 1x1)
		if (dstWidth == 0)
			dstWidth = 1;
		if (dstHeight == 0)
			dstHeight = 1;

		computeContext.SetConstants(0, topMip, numMips, 1.0f / dstWidth, 1.0f / dstHeight);
		computeContext.SetDynamicDescriptors(2, 0, numMips, m_UAVHandle + topMip + 1);
		computeContext.Dispatch2D(dstWidth, dstHeight);

		computeContext.InsertUAVBarrier(*this);

		topMip += numMips;
	}

	computeContext.TransitionResource(*this, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

DXGI_FORMAT Texture3D::GetBaseFormat(DXGI_FORMAT format)
{
	switch (format)
	{
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		return DXGI_FORMAT_R8G8B8A8_TYPELESS;

	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		return DXGI_FORMAT_B8G8R8A8_TYPELESS;

	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		return DXGI_FORMAT_B8G8R8X8_TYPELESS;

		// 以下主要针对Pixelbuffer,Texture3D应该不用	-2020-5-9
		// 32-bit Z w/ Stencil
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
		return DXGI_FORMAT_R32G8X24_TYPELESS;

		// No Stencil
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
		return DXGI_FORMAT_R32_TYPELESS;

		// 24-bit Z
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		return DXGI_FORMAT_R24G8_TYPELESS;

		// 16-bit Z w/o Stencil
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_R16_UNORM:
		return DXGI_FORMAT_R16_TYPELESS;

	default:
		return format;
	}
}

DXGI_FORMAT Texture3D::GetUAVFormat(DXGI_FORMAT format)
{
	switch (format)
	{
		// R8G8B8A8
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		return DXGI_FORMAT_R8G8B8A8_UNORM;

		// B8G8R8A8
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		return DXGI_FORMAT_B8G8R8A8_UNORM;

		// B8G8R8
	case DXGI_FORMAT_B8G8R8X8_TYPELESS:
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		return DXGI_FORMAT_B8G8R8X8_UNORM;

		// R32
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_R32_FLOAT:
		return DXGI_FORMAT_R32_FLOAT;

#if defined(DEBUG) || defined(_DEBUG)
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
	case DXGI_FORMAT_D16_UNORM:

		ASSERT(false, "Requested a UAV format for a depth stencil format");
#endif

	default:
		return format;
	}
}

D3D12_RESOURCE_DESC Texture3D::DescribeTex3D(uint32_t width, uint32_t height, uint32_t depth, uint32_t numMips, DXGI_FORMAT format, UINT flags)
{
	m_Width = width;
	m_Height = height;
	m_Depth = depth;
	m_Format = format;
	
	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Alignment = 0;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
	resourceDesc.Format = GetBaseFormat(format);
	resourceDesc.Width = (UINT64)width;
	resourceDesc.Height = (UINT)height;
	resourceDesc.DepthOrArraySize = (UINT16)depth;
	resourceDesc.MipLevels = (UINT16)numMips;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;

	return resourceDesc;
}

void Texture3D::CreateTextureResource(ID3D12Device* pDevice, const std::wstring& name, const D3D12_RESOURCE_DESC& resourceDesc)
{
	Destroy();

	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
	ASSERT_SUCCEEDED(pDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
		&resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_pResource)));

	m_UsageState = D3D12_RESOURCE_STATE_COMMON;
	m_GpuVirtualAddress = D3D12_GPU_VIRTUAL_ADDRESS_NULL;

#ifndef RELEASE
	m_pResource->SetName(name.c_str());
#else
	(name);
#endif
}

void Texture3D::CreateDerivedViews(ID3D12Device* pDevice, DXGI_FORMAT format, uint32_t numMips)
{
	m_NumMipmaps = numMips - 1;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

	uavDesc.Format = GetUAVFormat(format);
	srvDesc.Format = format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
	srvDesc.Texture3D.MipLevels = numMips;
	srvDesc.Texture3D.MostDetailedMip = 0;
	
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
	uavDesc.Texture3D.FirstWSlice = 0;
	uavDesc.Texture3D.WSize = 0xFFFFFFFFu;
	uavDesc.Texture3D.MipSlice = 0;

	if (m_SRVHandle.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
	{
		m_SRVHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	ID3D12Resource *pResource = m_pResource.Get();

	// create the shader resource view
	pDevice->CreateShaderResourceView(pResource, &srvDesc, m_SRVHandle);

	for (uint32_t i = 0; i < numMips; ++i)
	{
		if (m_UAVHandle[i].ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
		{
			m_UAVHandle[i] = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		pDevice->CreateUnorderedAccessView(pResource, nullptr, &uavDesc, m_UAVHandle[i]);

		uavDesc.Texture3D.MipSlice++;
	}
}

/**
	D3D12_UNORDERED_ACCESS_VIEW_DESC.D3D12_TEX3D_UAV 
	MipSlice
	The mipmap slice index.

	FirstWSlice
	The zero-based index of the first depth slice to be accessed.

	WSize
	The number of depth slices.
*/
