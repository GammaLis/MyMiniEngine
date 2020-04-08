#include "DynamicUploadBuffer.h"

using namespace MyDirectX;

void DynamicUploadBuffer::Create(ID3D12Device* pDevice, const std::wstring& name, uint32_t numElements, uint32_t elementSize, 
	bool bConstantBuffer)
{
	if (bConstantBuffer)
		elementSize = Math::AlignUp(elementSize, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

	D3D12_HEAP_PROPERTIES heapProps;
	heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProps.CreationNodeMask = 1;
	heapProps.VisibleNodeMask = 1;
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC resourceDesc;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Alignment = 0;
	resourceDesc.Width = numElements * elementSize;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	ThrowIfFailed(pDevice->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_pResource)));
	m_pResource->SetName(name.c_str());

	m_GpuVirtualAddress = m_pResource->GetGPUVirtualAddress();
	m_CpuVirtualAddress = nullptr;

	m_NumElement = numElements;
	m_ElementSize = elementSize;
}

void DynamicUploadBuffer::Destroy()
{
	if (m_pResource.Get() != nullptr)
	{
		if (m_CpuVirtualAddress != nullptr)
			Unmap();

		m_pResource = nullptr;
		m_GpuVirtualAddress = D3D12_GPU_VIRTUAL_ADDRESS_NULL;
	}
}

void* DynamicUploadBuffer::Map()
{
	assert(m_CpuVirtualAddress == nullptr);
	ThrowIfFailed(m_pResource->Map(0, nullptr, &m_CpuVirtualAddress));
	return m_CpuVirtualAddress;
}

void DynamicUploadBuffer::Unmap()
{
	assert(m_CpuVirtualAddress != nullptr);
	m_pResource->Unmap(0, nullptr);
	m_CpuVirtualAddress = nullptr;
}

D3D12_VERTEX_BUFFER_VIEW DynamicUploadBuffer::VertexBufferView(uint32_t numVertices, uint32_t stride, uint32_t offset) const
{
	D3D12_VERTEX_BUFFER_VIEW vbv;
	vbv.BufferLocation = m_GpuVirtualAddress +offset;
	vbv.SizeInBytes = numVertices * stride;
	vbv.StrideInBytes = stride;
	return vbv;
}

D3D12_INDEX_BUFFER_VIEW DynamicUploadBuffer::IndexBufferView(uint32_t numIndices, bool _32bit, uint32_t offset) const
{
	D3D12_INDEX_BUFFER_VIEW ibv;
	ibv.BufferLocation = m_GpuVirtualAddress + offset;
	ibv.Format = _32bit ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
	ibv.SizeInBytes = numIndices * (_32bit ? 4 : 2);
	return ibv;
}

void DynamicUploadBuffer::CopyToGpu(void* pSrc, uint32_t memSize, uint32_t instanceIndex)
{
	if (pSrc == nullptr || memSize == 0)
		return;

	if (m_CpuVirtualAddress == nullptr)
		Map();

	memcpy((uint8_t*)m_CpuVirtualAddress + instanceIndex * m_ElementSize, pSrc, memSize);
}
