#pragma once
#include "pch.h"
#include "CommandAllocatorPool.h"
#include <queue>
#include <mutex>
#include <stdint.h>

namespace MyDirectX
{
	class CommandListManager;

	constexpr uint32_t CommandTypeBitShift = 56;
	
	class CommandQueue
	{
		friend class CommandListManager;
		friend class CommandContext;

	public:
		CommandQueue(D3D12_COMMAND_LIST_TYPE type);
		~CommandQueue();

		void Create(ID3D12Device* pDevice);
		void ShutDown();

		inline bool IsReady() const
		{
			return m_CommandQueue != nullptr;
		}

		uint64_t IncrementFence();
		bool IsFenceComplete(uint64_t fenceValue);
		void StallForFence(uint64_t fenceValue);
		void StallForProducer(CommandQueue& producer);
		void WaitForFence(uint64_t fenceValue);
		void WaitForIdle() { WaitForFence(IncrementFence()); }

		ID3D12CommandQueue* GetCommandQueue() { return m_CommandQueue; }
		uint64_t GetNextFenceValue() { return m_NextFenceValue; }
		uint64_t GetLastCompletedFenceValue() { return m_LastCompletedFenceValue; }

	private:
		uint64_t ExecuteCommandList(ID3D12CommandList* list);
		ID3D12CommandAllocator* RequestAllocator();
		void DiscardAllocator(uint64_t fenceValueForReset, ID3D12CommandAllocator* allocator);

		const D3D12_COMMAND_LIST_TYPE m_Type;

		ID3D12CommandQueue* m_CommandQueue;

		std::mutex m_FenceMutex;
		std::mutex m_EventMutex;

		// lifetime of these objects is managed by the descriptor cache
		ID3D12Fence* m_pFence;
		// high 8 bits - CommandListTye, low 56 bits - actual fence value
		uint64_t m_NextFenceValue;
		uint64_t m_LastCompletedFenceValue;
		HANDLE m_FenceEventHandle{nullptr};

		CommandAllocatorPool m_AllocatorPool;
	};

	// The CommandListManager manages all the different CommandQueues: GraphicsQueue, ComputeQueue, and CopyQueue.
	class CommandListManager
	{
		friend class CommandContext;

	public:
		CommandListManager();
		~CommandListManager();

		void Create(ID3D12Device* pDevice);
		void Shutdown();

		CommandQueue& GetGraphicsQueue() { return m_GraphicsQueue; }
		CommandQueue& GetComputeQueue() { return m_ComputeQueue; }
		CommandQueue& GetCopyQueue() { return m_CopyQueue; }

		CommandQueue& GetQueue(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT)
		{
			switch (type)
			{
			case D3D12_COMMAND_LIST_TYPE_COMPUTE: return m_ComputeQueue;
			case D3D12_COMMAND_LIST_TYPE_COPY: return m_CopyQueue;
			default: return m_GraphicsQueue;
			}
		}

		ID3D12CommandQueue* GetCommandQueue()
		{
			return m_GraphicsQueue.GetCommandQueue();
		}

		void CreateNewCommandList(
			D3D12_COMMAND_LIST_TYPE type, 
			ID3D12GraphicsCommandList **list,
			ID3D12CommandAllocator **allocator);

		// Test to see if a fence has already been reached
		bool IsFenceComplete(uint64_t fenceValue)
		{
			return GetQueue(D3D12_COMMAND_LIST_TYPE(fenceValue >> CommandTypeBitShift)).IsFenceComplete(fenceValue);
		}

		// The cpu will wait for a fence to reach a specified value
		void WaitForFence(uint64_t fenceValue);

		// The cpu will wait for all command queues to empty (so that the cpu is idle)
		void IdleGPU()
		{
			m_GraphicsQueue.WaitForIdle();
			m_ComputeQueue.WaitForIdle();
			m_CopyQueue.WaitForIdle();
		}

	private:
		ID3D12Device* m_Device;

		CommandQueue m_GraphicsQueue;
		CommandQueue m_ComputeQueue;
		CommandQueue m_CopyQueue;

	};
}
