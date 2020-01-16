#pragma once
#include "pch.h"
#include "CommandAllocatorPool.h"
#include <queue>
#include <mutex>
#include <stdint.h>

namespace MyDirectX
{
	class CommandListManager;

	class CommandQueue
	{
		/**
			cppreference.com -> namespace
			在非局部类X中由友元声明所引入的名字，会成为X的最内层外围命名空间的成员，但它们对于通常的名字
		查找（无限定或有限定）不可见，除非在命名空间作用域提供与之匹配的声明，不论在类之前还是类之后均可。
		这种名字可通过ADL找到，其中同时考虑命名空间和类。
			这种友元声明，在确定名字是否与先前声明的名字冲突时，只考虑最内层外围命名空间。
		*/
		friend class CommandListManager;
		friend class CommandContext;

	public:
		CommandQueue(D3D12_COMMAND_LIST_TYPE type);
		~CommandQueue();

		void Create(ID3D12Device* pDevice);
		void ShutDown();

		inline bool IsReady()
		{
			return m_CommandQueue != nullptr;
		}

		uint64_t IncrementFence();
		bool IsFenceComplete(uint64_t fenceValue);
		// 尽管声明友元friend class CommandListManager,这里还是需要声明class CommandListManager
		// void StallForFence(CommandListManager* pCmdListManager, uint64_t fenceValue);
		// 还是改成这样
		void StallForFence(uint64_t fenceValue);
		void StallForProducer(CommandQueue& producer);
		void WaitForFence(uint64_t fenceValue);
		void WaitForIdle() { WaitForFence(IncrementFence()); }

		ID3D12CommandQueue* GetCommandQueue() { return m_CommandQueue; }

		uint64_t GetNextFenceValue() { return m_NextFenceValue; }

	private:
		uint64_t ExecuteCommandList(ID3D12CommandList* list);
		ID3D12CommandAllocator* RequestAllocator();
		void DiscardAllocator(uint64_t fenceValueForReset, ID3D12CommandAllocator* allocator);

		ID3D12CommandQueue* m_CommandQueue;

		const D3D12_COMMAND_LIST_TYPE m_Type;

		CommandAllocatorPool m_AllocatorPool;

		std::mutex m_FenceMutex;
		std::mutex m_EventMutex;

		// lifetime of these objects is managed by the descriptor cache
		ID3D12Fence* m_pFence;
		// high 8 bits - CommandListTye, low 56 bits - actual fence value
		uint64_t m_NextFenceValue;
		uint64_t m_LastCompletedFenceValue;
		HANDLE m_FenceEventHandle;
	};

	/**
		the CommandListManager manages all the different CommandQueues: GraphicsQueue, ComputeQueue, and
	CopyQueue.
	*/
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

		// test to see if a fence has already been reached
		bool IsFenceComplete(uint64_t fenceValue)
		{
			return GetQueue(D3D12_COMMAND_LIST_TYPE(fenceValue >> 56)).IsFenceComplete(fenceValue);
		}

		// the cpu will wait for a fence to reach a specified value
		void WaitForFence(uint64_t fenceValue);

		// the cpu will wait for all command queues to empty (so that the cpu is idle)
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

// test
namespace Test
{
	void h(int);
	namespace A
	{
		class X
		{
			friend void f(X);	// A::f 友元
			friend class Z;
			class Y
			{
				friend void g();	// A::g 友元
				friend void h(int);	// A::h 友元，与Test::h不冲突
			};

			Z* pz;	// Error: 未定义标识符
		};

		// A::f, A::g与A::h，Z 在命名空间作用域不可见
		// 虽然它们是命名空间A的成员
		// 需要加上声明 
		class Z;	// 不加出错
		extern Z* pz;	// Error: 未定义标识符

		extern X x;
		inline void g() {
			f(x);	// A::x::f 通过ADL找到
		}
		inline void f(X) { }	// A::f 定义
		inline void h(int) { }	// A::h 定义
		// A::f, A::g, A::h现在在命名空间作用域可见
		// 而且它们也是A::X A::X::Y 的友元
	}
}
