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
			�ڷǾֲ���X������Ԫ��������������֣����ΪX�����ڲ���Χ�����ռ�ĳ�Ա�������Ƕ���ͨ��������
		���ң����޶������޶������ɼ��������������ռ��������ṩ��֮ƥ�����������������֮ǰ������֮����ɡ�
		�������ֿ�ͨ��ADL�ҵ�������ͬʱ���������ռ���ࡣ
			������Ԫ��������ȷ�������Ƿ�����ǰ���������ֳ�ͻʱ��ֻ�������ڲ���Χ�����ռ䡣
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
		// ����������Ԫfriend class CommandListManager,���ﻹ����Ҫ����class CommandListManager
		// void StallForFence(CommandListManager* pCmdListManager, uint64_t fenceValue);
		// ���Ǹĳ�����
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
			friend void f(X);	// A::f ��Ԫ
			friend class Z;
			class Y
			{
				friend void g();	// A::g ��Ԫ
				friend void h(int);	// A::h ��Ԫ����Test::h����ͻ
			};

			Z* pz;	// Error: δ�����ʶ��
		};

		// A::f, A::g��A::h��Z �������ռ������򲻿ɼ�
		// ��Ȼ�����������ռ�A�ĳ�Ա
		// ��Ҫ�������� 
		class Z;	// ���ӳ���
		extern Z* pz;	// Error: δ�����ʶ��

		extern X x;
		inline void g() {
			f(x);	// A::x::f ͨ��ADL�ҵ�
		}
		inline void f(X) { }	// A::f ����
		inline void h(int) { }	// A::h ����
		// A::f, A::g, A::h�����������ռ�������ɼ�
		// ��������Ҳ��A::X A::X::Y ����Ԫ
	}
}
