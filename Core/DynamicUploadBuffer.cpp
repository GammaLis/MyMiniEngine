#include "DynamicUploadBuffer.h"
#include "Graphics.h"

using namespace MyDirectX;

void DynamicUploadBuffer::Create(ID3D12Device* pDevice, const std::wstring& name, uint32_t numElements, uint32_t elementSize, 
	bool bConstantBuffer, bool bframed)
{
	if (bConstantBuffer)
		elementSize = Math::AlignUp(elementSize, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	m_bConstantBuffer = bConstantBuffer;

	m_NumElement = numElements;
	m_ElementSize = elementSize;

	if (bframed)
		numElements *= MaxFrameBufferCount;
	m_bFramed = bframed;

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

#if 0
	if (m_bConstantBuffer)
	{
		m_CBV = CreateConstantBufferView(pDevice, 0, numElements);
	}
	else
	{
		m_SRV = CreateShaderResourceView(pDevice, 0, numElements);
	}
#endif
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
	vbv.BufferLocation = m_GpuVirtualAddress + offset;
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

const D3D12_CPU_DESCRIPTOR_HANDLE& DynamicUploadBuffer::GetSRV(uint32_t frame, uint32_t offset, uint32_t size)
{
	if (!m_bDescriptorInited && !m_bConstantBuffer)
	{
		if (size == INVALID_INDEX) size = m_NumElement;
		uint32_t numFrames = m_bFramed ? MaxFrameBufferCount : 1;
		for (uint32_t i = 0; i < numFrames; i++)
		{
			m_SRV[i] = CreateShaderResourceView(Graphics::s_Device, offset + i * m_NumElement, size);	
		}
		m_bDescriptorInited = true;
	}
	return m_SRV[frame];
}

const D3D12_CPU_DESCRIPTOR_HANDLE& DynamicUploadBuffer::GetCBV(uint32_t frame, uint32_t offset, uint32_t size)
{
	if (!m_bDescriptorInited && m_bConstantBuffer)
	{
		if (size == INVALID_INDEX) size = m_NumElement;
		uint32_t numFrames = m_bFramed ? MaxFrameBufferCount : 1;
		for (uint32_t i = 0; i < numFrames; i++)
		{
			m_CBV[i] = CreateConstantBufferView(Graphics::s_Device, offset + i * m_NumElement, size);
		}
		m_bDescriptorInited = true;
	}
	return m_CBV[frame];
}

D3D12_CPU_DESCRIPTOR_HANDLE DynamicUploadBuffer::CreateConstantBufferView(ID3D12Device* pDevice, uint32_t offset, uint32_t size) const
{
	uint32_t numElement = m_bFramed ? m_NumElement * MaxFrameBufferCount : m_NumElement;
	ASSERT(offset + size <= numElement);

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
	cbvDesc.BufferLocation = m_GpuVirtualAddress + offset * m_ElementSize;
	cbvDesc.SizeInBytes = size * m_ElementSize;

	D3D12_CPU_DESCRIPTOR_HANDLE hCBV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	pDevice->CreateConstantBufferView(&cbvDesc, hCBV);
	return hCBV;
}

// Dynamic StructuredBuffer
D3D12_CPU_DESCRIPTOR_HANDLE DynamicUploadBuffer::CreateShaderResourceView(ID3D12Device* pDevice, uint32_t offset, uint32_t size) const
{
	uint32_t numElement = m_bFramed ? m_NumElement * MaxFrameBufferCount : m_NumElement;
	ASSERT(offset + size <= numElement);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Buffer.FirstElement = offset;
	srvDesc.Buffer.NumElements = size;
	srvDesc.Buffer.StructureByteStride = m_ElementSize;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	D3D12_CPU_DESCRIPTOR_HANDLE hSRV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	pDevice->CreateShaderResourceView(m_pResource.Get(), &srvDesc, hSRV);
	return hSRV;
}
