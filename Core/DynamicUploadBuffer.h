#pragma once
#include "pch.h"

class DynamicUploadBuffer
{
public:
	DynamicUploadBuffer() : m_GpuVirtualAddress(D3D12_GPU_VIRTUAL_ADDRESS_NULL), m_CpuVirtualAddress(nullptr)
	{  }
	~DynamicUploadBuffer() { Destroy(); }

	void Create(ID3D12Device *pDevice, const std::wstring& name, uint32_t numElements, uint32_t elementSize, bool bConstantBuffer = false);
	void Destroy();

	// Map a cpu-visible pointer to the buffer memory. You probably don't want to leave a lot of 
	// memory (100s of MB) mapped this way, so you have the option of unmapping it.
	void* Map();
	void Unmap();

	D3D12_VERTEX_BUFFER_VIEW VertexBufferView(uint32_t numVertices, uint32_t stride, uint32_t offset = 0) const;
	D3D12_INDEX_BUFFER_VIEW IndexBufferView(uint32_t numIndices, bool _32bit, uint32_t offset = 0)  const;
	D3D12_GPU_VIRTUAL_ADDRESS GetGpuPointer(uint32_t offset = 0) const
	{
		return m_GpuVirtualAddress + offset;
	}

	void CopyToGpu(void* pSrc, uint32_t memSize, uint32_t instanceIndex = 0);
	D3D12_GPU_VIRTUAL_ADDRESS GetInstanceGpuPointer(uint32_t instanceIndx = 0)
	{
		return m_GpuVirtualAddress + instanceIndx * m_ElementSize;
	}

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> m_pResource;
	D3D12_GPU_VIRTUAL_ADDRESS m_GpuVirtualAddress;
	void* m_CpuVirtualAddress;
	
	uint32_t m_NumElement = 0;
	uint32_t m_ElementSize = 0;
};
