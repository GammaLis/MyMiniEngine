#pragma once
#include "pch.h"
#include <queue>
#include <mutex>
#include <stdint.h>

namespace MyDirectX
{
	/**
		controls a pool of allocators of a certain type
		the CommandAllocatorPool holds a pool of command allocators. It dynamically creates more CommandAllocators
	when they run out, and releases them back into the pool of available allocators when they are done processing 
	on the GPU
	*/
	class CommandAllocatorPool
	{
	public:
		CommandAllocatorPool(D3D12_COMMAND_LIST_TYPE type);
		~CommandAllocatorPool();

		void Create(ID3D12Device* pDevice);

		void Shutdown();

		// requests an allocator that will be available past the the given fence point
		// completedFenceValue - fence point, at which the given allocator should be available
		ID3D12CommandAllocator* RequestAllocator(uint64_t completedFenceValue);
		// discards a given allocator. It will be available after a given fence point
		void DiscardAllocator(uint64_t fenceValue, ID3D12CommandAllocator* allocator);

		inline size_t Size() { return m_AllocatorPool.size(); }

	private:
		const D3D12_COMMAND_LIST_TYPE m_cCommandListType;

		ID3D12Device* m_Device;
		std::vector<ID3D12CommandAllocator*> m_AllocatorPool;
		std::queue<std::pair<uint64_t, ID3D12CommandAllocator*>> m_ReadyAllocators;
		std::mutex m_AllocatorMutex;
	};

}