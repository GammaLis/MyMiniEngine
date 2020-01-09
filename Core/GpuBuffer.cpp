#include "GpuBuffer.h"

using namespace MyDirectX;

void GpuBuffer::Create(ID3D12Device* pDevice, const std::wstring& name, uint32_t numElements, uint32_t elementSize, const void* initialData)
{
	// -mf
	ASSERT(pDevice != nullptr);

	Destroy();

	m_ElementCount = numElements;
	m_ElementSize = elementSize;
	m_BufferSize = numElements * elementSize;

	D3D12_RESOURCE_DESC resourceDesc = DescribeBuffer();

	m_UsageState = D3D12_RESOURCE_STATE_COMMON;

	D3D12_HEAP_PROPERTIES heapProps;
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProps.CreationNodeMask = 1;
	heapProps.VisibleNodeMask = 1;

	ASSERT_SUCCEEDED(pDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
		&resourceDesc, m_UsageState, nullptr, IID_PPV_ARGS(&m_pResource)));

	m_GpuVirtualAddress = m_pResource->GetGPUVirtualAddress();

	if (initialData)
	{
		// CommandContextÉÐÎ´¶¨Òå -20-1-8
		// CommandContext::InitializeBuffer(*this, initialData, m_BufferSize);
	}

#ifdef RELEASE
	(name)
#else
	m_pResource->SetName(name.c_str());
#endif

	CreateDerivedViews();
}

// sub-allocate a buffer out of a pre-allocated heap. If initial data is provided, it will be copied into
// the buffer using the default command context
// 
void GpuBuffer::CreatePlaced(ID3D12Device* pDevice, const std::wstring& name, ID3D12Heap* pBackingHeap, uint32_t heapOffset, uint32_t numElements, uint32_t elementSize, const void* initialData)
{
	m_ElementCount = numElements;
	m_ElementSize = elementSize;
	m_BufferSize = numElements * elementSize;

	D3D12_RESOURCE_DESC resourceDesc = DescribeBuffer();

	m_UsageState = D3D12_RESOURCE_STATE_COMMON;

	ASSERT_SUCCEEDED(pDevice->CreatePlacedResource(pBackingHeap, heapOffset, &resourceDesc, m_UsageState,
		nullptr, IID_PPV_ARGS(&m_pResource)));

	m_GpuVirtualAddress = m_pResource->GetGPUVirtualAddress();

	if (initialData)
	{
		//
		// CommandContext::InitializeBuffer(*this, initialData, m_BufferSize);
	}

#ifdef RELEASE
	(name)
#else
	m_pResource->SetName(name.c_str());
#endif

	CreateDerivedViews();
}

D3D12_CPU_DESCRIPTOR_HANDLE GpuBuffer::CreateConstantBufferView(uint32_t offset, uint32_t size) const
{
	ASSERT(offset + size <= m_BufferSize);

	size = Math::AlignUp(size, 16);

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
	cbvDesc.BufferLocation = m_GpuVirtualAddress + (size_t)offset;
	cbvDesc.SizeInBytes = size;

	// D3D12_GPU_DESCRIPTOR_HANDLE hCBV = alloca
}

D3D12_RESOURCE_DESC GpuBuffer::DescribeBuffer()
{
	ASSERT(m_BufferSize);

	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Flags = m_ResourceFlags;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.Height = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Width = (UINT64)m_BufferSize;
	
	return desc;
}
