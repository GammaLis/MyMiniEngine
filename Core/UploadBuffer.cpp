#include "UploadBuffer.h"

using namespace MyDirectX;

void UploadBuffer::Create(ID3D12Device* pDevice, const std::wstring& name, size_t bufferSize)
{
    Destroy();

    // create an upload buffer. This is CPU-visible, but it's write combined memory, so avoiding read back from it.
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;
    
    // upload buffer must be 1-dimensional
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

    ASSERT_SUCCEEDED(pDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc, 
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_pResource)));
    
    m_GpuVirtualAddress = m_pResource->GetGPUVirtualAddress();

#ifdef RELEASE
    (fileName);
#else 
    m_pResource->SetName(name.c_str());
#endif
}

void* UploadBuffer::Map(void)
{
    void *memory;
    m_pResource->Map(0, &CD3DX12_RANGE(0, m_BufferSize), &memory);
    return memory;
}

void UploadBuffer::Unmap(size_t begin, size_t end)
{
    m_pResource->Unmap(0, &CD3DX12_RANGE(begin, std::min(end, m_BufferSize)));
}
