#include "CommandListManager.h"
#include "Graphics.h"

using namespace MyDirectX;

CommandQueue::CommandQueue(D3D12_COMMAND_LIST_TYPE type)
	: m_Type(type), 
	m_CommandQueue(nullptr), 
	m_pFence(nullptr), 
	m_NextFenceValue((uint64_t)type << CommandTypeBitShift | 1),			// type info in high 8 bits
	m_LastCompletedFenceValue((uint64_t)type << CommandTypeBitShift),
	m_AllocatorPool(type)
{
}

CommandQueue::~CommandQueue()
{
	ShutDown();
}

void CommandQueue::Create(ID3D12Device* pDevice)
{
	ASSERT(pDevice != nullptr);
	ASSERT(!IsReady());
	ASSERT(m_AllocatorPool.Size() == 0);

	D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {};
	cmdQueueDesc.Type = m_Type;
	cmdQueueDesc.NodeMask = 1;
	pDevice->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&m_CommandQueue));
	m_CommandQueue->SetName(L"CommandListManager::m_CommandQueue");
	
	ASSERT_SUCCEEDED(pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence)));
	m_pFence->SetName(L"CommandListManager::m_pFence");
	m_pFence->Signal((uint64_t)m_Type << CommandTypeBitShift);

	m_FenceEventHandle = CreateEvent(nullptr, false, false, nullptr);
	ASSERT(m_FenceEventHandle != nullptr);

	m_AllocatorPool.Create(pDevice);

	ASSERT(IsReady());

}

void CommandQueue::ShutDown()
{
	if (m_CommandQueue == nullptr)
		return;

	m_AllocatorPool.Shutdown();

	if (m_FenceEventHandle)
	{
		CloseHandle(m_FenceEventHandle);
		m_FenceEventHandle = nullptr;
	}

	m_pFence->Release();
	m_pFence = nullptr;

	m_CommandQueue->Release();
	m_CommandQueue = nullptr;

}

uint64_t CommandQueue::IncrementFence()
{
	std::lock_guard<std::mutex> lockGuard(m_FenceMutex);

	m_CommandQueue->Signal(m_pFence, m_NextFenceValue);
	return m_NextFenceValue++;
}

bool CommandQueue::IsFenceComplete(uint64_t fenceValue)
{
	// Avoid querying the fence value by testing against the last one seen.
	// the max() is to protect against an unlikely race condition that could cause the last
	// completed fence value to regress
	if (fenceValue > m_LastCompletedFenceValue)
		m_LastCompletedFenceValue = std::max(m_LastCompletedFenceValue, m_pFence->GetCompletedValue());

	return fenceValue <= m_LastCompletedFenceValue;
}

void CommandQueue::StallForFence(uint64_t fenceValue)
{
	CommandQueue& producer = Graphics::s_CommandManager.GetQueue((D3D12_COMMAND_LIST_TYPE)(fenceValue >> CommandTypeBitShift));
	m_CommandQueue->Wait(producer.m_pFence, fenceValue);
}

void CommandQueue::StallForProducer(CommandQueue& producer)
{
	ASSERT(producer.m_NextFenceValue > 0);
	m_CommandQueue->Wait(producer.m_pFence, producer.m_NextFenceValue - 1);
}

void CommandQueue::WaitForFence(uint64_t fenceValue)
{
	if (IsFenceComplete(fenceValue))
		return;

	// TODO:  Think about how this might affect a multi-threaded situation.  Suppose thread A
	// wants to wait for fence 100, then thread B comes along and wants to wait for 99.  If
	// the fence can only have one event set on completion, then thread B has to wait for 
	// 100 before it knows 99 is ready.  Maybe insert sequential events?
	{
		std::lock_guard<std::mutex> lockGuard(m_EventMutex);

		m_pFence->SetEventOnCompletion(fenceValue, m_FenceEventHandle);
		WaitForSingleObject(m_FenceEventHandle, INFINITE);
		m_LastCompletedFenceValue = fenceValue;
	}
}

uint64_t CommandQueue::ExecuteCommandList(ID3D12CommandList* list)
{
	std::lock_guard<std::mutex> lockGuard(m_FenceMutex);

	ASSERT_SUCCEEDED(((ID3D12GraphicsCommandList*)list)->Close());

	// kickoff the command list
	m_CommandQueue->ExecuteCommandLists(1, &list);

	// signal the next fence value (with the GPU)
	m_CommandQueue->Signal(m_pFence, m_NextFenceValue);

	// and increment the fence value
	return m_NextFenceValue++;
}

ID3D12CommandAllocator* CommandQueue::RequestAllocator()
{
	uint64_t completedFence = m_pFence->GetCompletedValue();

	return m_AllocatorPool.RequestAllocator(completedFence);
}

void CommandQueue::DiscardAllocator(uint64_t fenceValueForReset, ID3D12CommandAllocator* allocator)
{
	m_AllocatorPool.DiscardAllocator(fenceValueForReset, allocator);
}

// CommandListManager
CommandListManager::CommandListManager()
	: m_Device(nullptr),
	m_GraphicsQueue(D3D12_COMMAND_LIST_TYPE_DIRECT),
	m_ComputeQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE),
	m_CopyQueue(D3D12_COMMAND_LIST_TYPE_COPY)
{
}

CommandListManager::~CommandListManager()
{
	Shutdown();
}

void CommandListManager::Create(ID3D12Device* pDevice)
{
	ASSERT(pDevice != nullptr);
	
	m_Device = pDevice;

	m_GraphicsQueue.Create(pDevice);
	m_ComputeQueue.Create(pDevice);
	m_CopyQueue.Create(pDevice);

}

void CommandListManager::Shutdown()
{
	m_GraphicsQueue.ShutDown();
	m_ComputeQueue.ShutDown();
	m_CopyQueue.ShutDown();
}

void CommandListManager::CreateNewCommandList(D3D12_COMMAND_LIST_TYPE type, ID3D12GraphicsCommandList** list, ID3D12CommandAllocator** allocator)
{
	ASSERT(type != D3D12_COMMAND_LIST_TYPE_BUNDLE, "Bundles are not yet supported");

	switch (type)
	{
	default:
	case D3D12_COMMAND_LIST_TYPE_DIRECT:
		*allocator = m_GraphicsQueue.RequestAllocator();
		break;
	case D3D12_COMMAND_LIST_TYPE_BUNDLE:
		// TODO...
		break;
	case D3D12_COMMAND_LIST_TYPE_COMPUTE:
		*allocator = m_ComputeQueue.RequestAllocator();
		break;
	case D3D12_COMMAND_LIST_TYPE_COPY:
		*allocator = m_CopyQueue.RequestAllocator();
		break;
	}

	ASSERT_SUCCEEDED(m_Device->CreateCommandList(1, type, *allocator, nullptr, IID_PPV_ARGS(list)));
	(*list)->SetName(L"CommandList");
}

void CommandListManager::WaitForFence(uint64_t fenceValue)
{
	GetQueue((D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56)).WaitForFence(fenceValue);
}
