#include "FrameDescriptorHeap.h"

using namespace MyDirectX;

void FrameDescriptorHeap::Create(ID3D12Device *pDevice, const std::wstring &heapName, uint32_t numPersistent)
{
	Destroy();

	auto maxCount = m_DescriptorHeaps[0]->GetCapacity();

	if (numPersistent == uint32_t(-1))
		numPersistent = maxCount;

	m_NumPersistent = numPersistent;
	m_NumTemporary = maxCount - numPersistent;

	for (uint32_t i = 0; i < m_NumHeaps; i++)
	{
		m_DescriptorHeaps[i]->Create(pDevice, heapName + std::to_wstring(i));
	}

	m_PersistentIndices.resize(m_NumPersistent);
	for (uint32_t i = 0; i < m_NumPersistent; i++)
		m_PersistentIndices[i] = i;
	m_PersistentAllocated = 0;
}

void FrameDescriptorHeap::Create(ID3D12Device *pDevice, const std::wstring &heapName, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t maxCount, uint32_t numPersistent, bool shaderVisible)
{
	if (type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV || type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV)
		shaderVisible = false;

	m_bShaderVisible = shaderVisible;
	m_NumHeaps = shaderVisible ? MaxFrameBufferCount : 1;
	
	for (uint32_t i = 0; i < m_NumHeaps; i++)
	{
		m_DescriptorHeaps[i].reset(new UserDescriptorHeap(type, maxCount));
	}

	Create(pDevice, heapName, numPersistent);
}

void FrameDescriptorHeap::Destroy()
{
	for (uint32_t i = 0; i < m_NumHeaps; i++)
	{
		if (m_DescriptorHeaps[i] != nullptr)
			m_DescriptorHeaps[i]->Destroy();
	}
	m_PersistentAllocated = 0;

}

PersistentDescriptorAlloc FrameDescriptorHeap::AllocPersistent()
{
	ASSERT(m_PersistentAllocated < m_NumPersistent);

	uint32_t index = m_PersistentIndices[m_PersistentAllocated];
	m_PersistentAllocated++;

	PersistentDescriptorAlloc alloc;
	alloc.index = index;
	
	for (uint32_t i = 0; i < m_NumHeaps; i++)
	{
		alloc.handles[i] = m_DescriptorHeaps[i]->GetHandleAtOffset(index);
	}

	return alloc;
}

void FrameDescriptorHeap::FreePersistent(uint32_t& index)
{
	ASSERT(index < m_NumPersistent && m_PersistentAllocated > 0);

	m_PersistentIndices[--m_PersistentAllocated] = index;
	index = uint32_t(-1);
}

void FrameDescriptorHeap::FreePersistent(DescriptorHandle& handle)
{
	if (!handle.IsNull())
		return;

	uint32_t index = IndexFromHandle(handle);
	ASSERT(index < m_NumPersistent && m_PersistentAllocated > 0);

	FreePersistent(index);
}

void FrameDescriptorHeap::AllocAndCopyPersistentDescriptor(ID3D12Device *pDevice, DescriptorHandle descriptor)
{
	const auto type = m_DescriptorHeaps[0]->GetType();
	auto alloc = AllocPersistent();
	for (uint32_t i = 0; i < m_NumHeaps; i++)
		pDevice->CopyDescriptorsSimple(1, alloc.handles[i], descriptor, type);
}

TemporaryDescriptorAlloc FrameDescriptorHeap::AllocTemporary(uint32_t count)
{
	ASSERT(m_TemporaryAllocated + count <= m_NumTemporary);

	uint32_t index = m_TemporaryAllocated;
	m_TemporaryAllocated += count;

	index = index + m_NumPersistent;

	TemporaryDescriptorAlloc alloc;
	alloc.startIndex = index;
	alloc.startHandle = m_DescriptorHeaps[m_HeapIndex]->GetHandleAtOffset(index);

	return alloc;
}

void FrameDescriptorHeap::AllocAndCopyTemporaryDescriptor(ID3D12Device* pDevice, DescriptorHandle descriptor)
{
	auto type = m_DescriptorHeaps[0]->GetType(); 
	auto alloc = AllocTemporary();
	pDevice->CopyDescriptorsSimple(1, alloc.startHandle, descriptor, type);
}

void FrameDescriptorHeap::AllocAndCopyTemporaryDescriptors(ID3D12Device* pDevice, const std::span<DescriptorHandle> &descriptors)
{
	auto type = m_DescriptorHeaps[0]->GetType();
	auto count = static_cast<uint32_t>(descriptors.size());
	auto alloc = AllocTemporary(count);
	pDevice->CopyDescriptorsSimple(count, alloc.startHandle, descriptors[0], type);
}

void FrameDescriptorHeap::EndFrame()
{
	m_HeapIndex = (m_HeapIndex + 1) % m_NumHeaps;
	m_TemporaryAllocated = 0;
}

DescriptorHandle FrameDescriptorHeap::HandleFromIndex(uint32_t descriptorIndex) const
{
	return HandleFromIndex(descriptorIndex, m_HeapIndex);
}

DescriptorHandle FrameDescriptorHeap::HandleFromIndex(uint32_t descriptorIndex, uint32_t heapIndex) const
{
	ASSERT(heapIndex < m_NumHeaps && descriptorIndex < TotalNumDescriptors());
	auto  &heap = m_DescriptorHeaps[heapIndex];
	return heap->GetHandleAtOffset(descriptorIndex);
}

uint32_t FrameDescriptorHeap::IndexFromHandle(const DescriptorHandle &handle) const
{
	auto &heap = m_DescriptorHeaps[m_HeapIndex];
	return heap->GetOffsetOfHandle(handle);
}

UserDescriptorHeap* FrameDescriptorHeap::CurrentHeap() const
{
	return m_DescriptorHeaps[m_HeapIndex].get();
}
