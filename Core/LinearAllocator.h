#pragma once
#include "GpuResource.h"
#include <queue>
#include <mutex>

namespace MyDirectX
{
	/**
		this is a dynamic graphics memory allocator for DX12. It's designed to work in concert with the CommandContext
	class and to do so in a thread-safe manner. There may be many command contexts, each with its own linear
	allocators. They act as windows into a global memory pool by reserving a context-local memory page. Requesting
	a new page is done is a thread-safe manner by guarding accesses with a mutex lock
		when a command context is finishied, it will receive a fence ID that indicates when it's safe to reclaim
	used resources. The CleanupUsedPages() method must be invoked at this time so that the used pages can be 
	scheduled for reuse after the fence has cleared.
	*/

	// constant blocks must be multiple of 16 * 16 bytes each
	#define DEFAULT_ALIGN 256

	// various types of allocations may contain NULL pointers. Check before dereferencing if you are unsure
	struct DynAlloc
	{
		DynAlloc(GpuResource& baseResource, size_t offset, size_t size)
			: buffer(baseResource), offset(offset), size(size)
		{  }

		GpuResource& buffer;	// the D3D buffer associated with this memory
		size_t offset;			// offset from start of buffer resource
		size_t size;			// reserved size of this allocation
		void* dataPtr;			// the CPU-writeable address
		D3D12_GPU_VIRTUAL_ADDRESS GpuAddress;	// the GPU-visible address
	};

	class LinearAllocationPage : public GpuResource
	{
	public:
		LinearAllocationPage(ID3D12Resource *pResource, D3D12_RESOURCE_STATES usage) 
			: GpuResource()
		{ 
			m_pResource.Attach(pResource);
			m_UsageState = usage;
			m_GpuVirtualAddress = m_pResource->GetGPUVirtualAddress();
			m_pResource->Map(0, nullptr, &m_CpuVirtualAddress);
		}
		~LinearAllocationPage()
		{
			Unmap();
		}

		// UPLOAD_HEAP (or DEFAULT_HEAP Map ???，DEFAULT_HEAP也能MAP ??? -20-1-15)
		void Map()
		{
			if (m_CpuVirtualAddress == nullptr)
				m_pResource->Map(0, nullptr, &m_CpuVirtualAddress);
		}

		void Unmap()
		{
			if (m_CpuVirtualAddress != nullptr)
			{
				m_pResource->Unmap(0, nullptr);
				m_CpuVirtualAddress = nullptr;
			}
		}

		void* m_CpuVirtualAddress;
		D3D12_GPU_VIRTUAL_ADDRESS m_GpuVirtualAddress;
	};

	// 主要2种 分配器类型：GPU可写分配器(DefaultHeap),CPU可写分配器(UploadHeap)
	enum class LinearAllocatorType
	{
		kInvalidAllocator = -1,

		kGpuExclusive = 0,	// DEFAULT GPU-writeable (via UAV)	exclisive - 独占的
		kCpuWritable = 1,	// UPLOAD CPU-writeable (but write combined)

		kNumAllocatorTypes
	};

	enum
	{
		kGpuAllocatorPageSize = 0x10000,	// 64K
		kCpuAllocatorPageSize = 0x200000    // 2MB
	};

	// 页管理器 管理 页 的生命周期，和对应操作
	class LinearAllocatorPageManager
	{
	public:
		LinearAllocatorPageManager();
		LinearAllocationPage* RequestPage();
		LinearAllocationPage* CreateNewPage(size_t pageSize = 0);

		// discard pages will get recycled. This is for fixed size pages
		void DiscardPages(uint64_t fenceID, const std::vector<LinearAllocationPage*>& pages);

		// freed pages will be destroyed once their fence has passed. This is for single-use, "large" pages
		void FreeLargePages(uint64_t fenceID, const std::vector<LinearAllocationPage*>& pages);

		void Destroy() { m_PagePool.clear(); }

	private:
		static LinearAllocatorType s_AutoType;

		LinearAllocatorType m_AllocationType;
		std::vector<std::unique_ptr<LinearAllocationPage>> m_PagePool;
		std::queue<std::pair<uint64_t, LinearAllocationPage*>> m_RetiredPages;
		std::queue<std::pair<uint64_t, LinearAllocationPage*>> m_DeletionQueue;
		std::queue<LinearAllocationPage*> m_AvailablePages;
		std::mutex m_Mutex;
	};

	// 分配器 分配 页，页管理器 进行记录，操作和管理
	// 多个线程 - 多个分配器
	class LinearAllocator
	{
	public:
		LinearAllocator(LinearAllocatorType type) : m_AllocationType(type), m_PageSize(0), m_CurOffset(~(size_t)0), m_CurPage(nullptr)
		{
			ASSERT(type > LinearAllocatorType::kInvalidAllocator&& type < LinearAllocatorType::kNumAllocatorTypes);
			m_PageSize = (type == LinearAllocatorType::kGpuExclusive ? kGpuAllocatorPageSize : kCpuAllocatorPageSize);
		}

		DynAlloc Allocate(size_t sizeInBytes, size_t alignment = DEFAULT_ALIGN);

		void CleanupUsedPages(uint64_t fenceID);

		static void DestroyAll()
		{
			s_PageManager[0].Destroy();
			s_PageManager[1].Destroy();
		}

	private:
		DynAlloc AllocateLargePage(size_t sizeInBytes);

		static LinearAllocatorPageManager s_PageManager[2];

		LinearAllocatorType m_AllocationType;
		size_t m_PageSize;
		size_t m_CurOffset;
		LinearAllocationPage* m_CurPage;
		std::vector<LinearAllocationPage*> m_RetiredPages;
		std::vector<LinearAllocationPage*> m_LargePageList;
	};

}