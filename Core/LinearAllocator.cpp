#include "LinearAllocator.h"
#include "CommandListManager.h"
#include "Graphics.h"
#include <thread>

namespace MyDirectX
{
	LinearAllocatorType LinearAllocatorPageManager::s_AutoType = LinearAllocatorType::kGpuExclusive;

	LinearAllocatorPageManager LinearAllocator::s_PageManager[2];

	LinearAllocatorPageManager::LinearAllocatorPageManager()
	{
		m_AllocationType = s_AutoType;
		s_AutoType = (LinearAllocatorType)((int)s_AutoType + 1);
		ASSERT(s_AutoType <= LinearAllocatorType::kNumAllocatorTypes);
	}

	LinearAllocationPage* LinearAllocatorPageManager::RequestPage()
	{
		std::lock_guard<std::mutex> lockGuard(m_Mutex);

		while (!m_RetiredPages.empty() && Graphics::s_CommandManager.IsFenceComplete(m_RetiredPages.front().first))
		{
			m_AvailablePages.push(m_RetiredPages.front().second);
			m_RetiredPages.pop();
		}

		LinearAllocationPage* pagePtr = nullptr;

		if (!m_AvailablePages.empty())
		{
			pagePtr = m_AvailablePages.front();
			m_AvailablePages.pop();
		}
		else
		{
			pagePtr = CreateNewPage();
			m_PagePool.emplace_back(pagePtr);
		}

		return pagePtr;
	}

	LinearAllocationPage* LinearAllocatorPageManager::CreateNewPage(size_t pageSize)
	{
		D3D12_HEAP_PROPERTIES heapProps;
		heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProps.CreationNodeMask = 1;
		heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProps.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC resourceDesc;
		resourceDesc.Alignment = 0;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		resourceDesc.Height = 1;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resourceDesc.MipLevels = 1;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.SampleDesc.Quality = 0;

		D3D12_RESOURCE_STATES defaultUsage;

		if (m_AllocationType == LinearAllocatorType::kGpuExclusive)
		{
			heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
			resourceDesc.Width = pageSize == 0 ? kGpuAllocatorPageSize : pageSize;
			resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			defaultUsage = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		}
		else
		{
			heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
			resourceDesc.Width = pageSize == 0 ? kCpuAllocatorPageSize : pageSize;
			resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
			defaultUsage = D3D12_RESOURCE_STATE_GENERIC_READ;
		}

		ID3D12Resource* pBuffer;
		ASSERT_SUCCEEDED(Graphics::s_Device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
			&resourceDesc, defaultUsage, nullptr, IID_PPV_ARGS(&pBuffer)));

		pBuffer->SetName(L"LinearAllocator Page");

		return new LinearAllocationPage(pBuffer, defaultUsage);
	}

	void LinearAllocatorPageManager::DiscardPages(uint64_t fenceID, const std::vector<LinearAllocationPage*>& pages)
	{
		std::lock_guard<std::mutex> lockGuard(m_Mutex);

		for (auto iter = pages.begin(); iter != pages.end(); ++iter)
		{
			m_RetiredPages.push(std::make_pair(fenceID, *iter));
		}
	}

	void LinearAllocatorPageManager::FreeLargePages(uint64_t fenceID, const std::vector<LinearAllocationPage*>& pages)
	{
		std::lock_guard<std::mutex> lockGuard(m_Mutex);

		while (!m_DeletionQueue.empty() && Graphics::s_CommandManager.IsFenceComplete(m_DeletionQueue.front().first))
		{
			delete m_DeletionQueue.front().second;
			m_DeletionQueue.pop();
		}

		for (auto iter = pages.begin(); iter != pages.end(); ++iter)
		{
			(*iter)->Unmap();
			m_DeletionQueue.push(std::make_pair(fenceID, *iter));
		}
	}

	// LinearAllocator
	DynAlloc LinearAllocator::Allocate(size_t sizeInBytes, size_t alignment)
	{
		const size_t alignmentMask = alignment - 1;

		// assert that it's a power of 2 
		ASSERT((alignment & alignmentMask) == 0);

		// align the allocation
		const size_t alignedSize = Math::AlignUpWithMask(sizeInBytes, alignmentMask);

		if (alignedSize > m_PageSize)
			return AllocateLargePage(alignedSize);

		m_CurOffset = Math::AlignUp(m_CurOffset, alignment);

		if (m_CurOffset + alignedSize > m_PageSize)
		{
			ASSERT(m_CurPage != nullptr);
			m_RetiredPages.push_back(m_CurPage);
			m_CurPage = nullptr;
		}

		if (m_CurPage == nullptr)
		{
			m_CurPage = s_PageManager[(int)m_AllocationType].RequestPage();
			m_CurOffset = 0;
		}

		DynAlloc ret(*m_CurPage, m_CurOffset, alignedSize);
		ret.dataPtr = (uint8_t*)m_CurPage->m_CpuVirtualAddress + m_CurOffset;
		ret.GpuAddress = m_CurPage->m_GpuVirtualAddress + m_CurOffset;

		m_CurOffset += alignedSize;

		return ret;
	}

	// -2020-3-28 Note:
	//	if (m_CurPage == nullptr) return;
	//	When m_CurPage == nullptr, m_LargePageList != nullptr, can't delete
	void LinearAllocator::CleanupUsedPages(uint64_t fenceID)
	{
		if (m_CurPage != nullptr)
		{
			m_RetiredPages.push_back(m_CurPage);
			m_CurPage = nullptr;
			m_CurOffset = 0;

			s_PageManager[(int)m_AllocationType].DiscardPages(fenceID, m_RetiredPages);
			m_RetiredPages.clear();
		}

		if (!m_LargePageList.empty())
		{
			s_PageManager[(int)m_AllocationType].FreeLargePages(fenceID, m_LargePageList);
			m_LargePageList.clear();
		}
	}

	DynAlloc LinearAllocator::AllocateLargePage(size_t sizeInBytes)
	{
		LinearAllocationPage* oneOff = s_PageManager[(int)m_AllocationType].CreateNewPage(sizeInBytes);
		m_LargePageList.push_back(oneOff);

		DynAlloc ret(*oneOff, 0, sizeInBytes);
		ret.dataPtr = oneOff->m_CpuVirtualAddress;
		ret.GpuAddress = oneOff->m_GpuVirtualAddress;

		return ret;
	}
}
