#pragma once

#include <span>
#include "DescriptorHeap.h"
#include "Utilities/GameUtility.h"

namespace MyDirectX
{
	// Ref: MyNameIsMJP - BindlessDeferred

	struct DescriptorRange
	{
		uint32_t start = 0;
		uint32_t count = 0;
	};

	struct PersistentDescriptorAlloc
	{
		DescriptorHandle handles[MaxFrameBufferCount] = {};
		uint32_t index = INVALID_INDEX;
	};
	struct TemporaryDescriptorAlloc
	{
		DescriptorHandle startHandle{};
		uint32_t startIndex = INVALID_INDEX;
	};

	class FrameDescriptorHeap
	{
	public:
		FrameDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, uint32_t maxCount = 1024, bool shaderVisible = true)
		{
			if (type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV || type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV)
				shaderVisible = false;

			m_bShaderVisible = shaderVisible;
			m_NumHeaps = shaderVisible ? MaxFrameBufferCount : 1;
			for (uint32_t i = 0; i < m_NumHeaps; i++)
				m_DescriptorHeaps[i].reset(new UserDescriptorHeap(type, maxCount));
		}
		~FrameDescriptorHeap() { Destroy(); }

		void Create(ID3D12Device *pDevice, const std::wstring &heapName, uint32_t numPersistent = INVALID_INDEX);
		void Create(ID3D12Device *pDevice, const std::wstring &heapName, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t maxCount, uint32_t numPersistent, bool shaderVisible = true);

		void Destroy();

		PersistentDescriptorAlloc AllocPersistent();
		void FreePersistent(uint32_t &index);
		void FreePersistent(DescriptorHandle &handle);

		void AllocAndCopyPersistentDescriptor(ID3D12Device *pDevice, DescriptorHandle descriptor);

		TemporaryDescriptorAlloc AllocTemporary(uint32_t count = 1);
		void AllocAndCopyTemporaryDescriptor(ID3D12Device *pDevice, DescriptorHandle descriptor);
		
		void AllocAndCopyTemporaryDescriptors(ID3D12Device *pDevice,  const std::span<DescriptorHandle> &descriptors);

		void EndFrame();

		DescriptorHandle HandleFromIndex(uint32_t descriptorIndex) const;
		DescriptorHandle HandleFromIndex(uint32_t descriptorIndex, uint32_t heapIndex) const;

		uint32_t IndexFromHandle(const DescriptorHandle &handle) const;

		UserDescriptorHeap* CurrentHeap() const;
		uint32_t GetCurrentHeapIndex() const { return m_HeapIndex; }
		uint32_t TotalNumDescriptors() const { return m_NumPersistent + m_NumTemporary; }
		uint32_t FrameHeapCount() const { return m_NumHeaps; }
		uint32_t PersistentAllocated() const { return m_PersistentAllocated; }
		uint32_t TemporaryAllocated() const { return m_TemporaryAllocated; }

	private:
		std::unique_ptr<UserDescriptorHeap> m_DescriptorHeaps[MaxFrameBufferCount];

		bool m_bShaderVisible{true};
		uint32_t m_NumHeaps{0};

		uint32_t m_NumPersistent{0};
		uint32_t m_PersistentAllocated{0};
		std::vector<uint32_t> m_PersistentIndices;

		uint32_t m_NumTemporary{0};
		volatile uint32_t m_TemporaryAllocated{0};

		uint32_t m_HeapIndex{0};
	};	
}
