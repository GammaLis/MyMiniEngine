#include "ReadbackBuffer.h"

using namespace MyDirectX;

void ReadbackBuffer::Create(ID3D12Device* pDevice, const std::wstring& name, uint32_t numElements, uint32_t elementSize)
{
	Destroy();

	m_ElementCount = numElements;
	m_ElementSize = elementSize;
	m_BufferSize = numElements * elementSize;
	m_UsageState = D3D12_RESOURCE_STATE_COPY_DEST;

	// create a readback buffer large enough to hold all texel data
	D3D12_HEAP_PROPERTIES heapProps;
	heapProps.Type = D3D12_HEAP_TYPE_READBACK;
	heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProps.CreationNodeMask = 1;
	heapProps.VisibleNodeMask = 1;
	
	// readback buffers must be 1-dimensional, i.e. "buffer" not "texture2d"
	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = m_BufferSize;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	resourceDesc.Alignment = 0;

	ASSERT_SUCCEEDED(pDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
		&resourceDesc, m_UsageState, nullptr, IID_PPV_ARGS(&m_pResource)));
	m_GpuVirtualAddress = m_pResource->GetGPUVirtualAddress();

#ifdef RELEASE
	(name);
#else
	m_pResource->SetName(name.c_str());
#endif
}

void* ReadbackBuffer::Map()
{
	void* pMem;
	m_pResource->Map(0, &CD3DX12_RANGE(0, m_BufferSize), &pMem);
	return pMem;
}

void ReadbackBuffer::Unmap()
{
	m_pResource->Unmap(0, &CD3DX12_RANGE(0, 0));
}
