#include "CommandAllocatorPool.h"

using namespace MyDirectX;

CommandAllocatorPool::CommandAllocatorPool(D3D12_COMMAND_LIST_TYPE type)
	: m_cCommandListType(type), m_Device(nullptr)
{
}

CommandAllocatorPool::~CommandAllocatorPool()
{
	Shutdown();
}

void CommandAllocatorPool::Create(ID3D12Device* pDevice)
{
	m_Device = pDevice;
}

void CommandAllocatorPool::Shutdown()
{
	for (size_t i = 0; i < m_AllocatorPool.size(); ++i)
		m_AllocatorPool[i]->Release();

	m_AllocatorPool.clear();
}

ID3D12CommandAllocator* CommandAllocatorPool::RequestAllocator(uint64_t completedFenceValue)
{
	std::lock_guard<std::mutex> lockGuard(m_AllocatorMutex);

	ID3D12CommandAllocator* pAllocator = nullptr;
	if (!m_ReadyAllocators.empty())
	{
		std::pair<uint64_t, ID3D12CommandAllocator*> allocatorPair = m_ReadyAllocators.front();
		if (allocatorPair.first <= completedFenceValue)
		{
			pAllocator = allocatorPair.second;
			ASSERT_SUCCEEDED(pAllocator->Reset());
			m_ReadyAllocators.pop();
		}
	}

	// if no allocator's were ready to be reused, create a new one
	if (pAllocator == nullptr)
	{
		ASSERT_SUCCEEDED(m_Device->CreateCommandAllocator(m_cCommandListType, IID_PPV_ARGS(&pAllocator)));
		wchar_t allocatorName[32];
		swprintf(allocatorName, 32, L"CommandAllocator %zu", m_AllocatorPool.size());
		pAllocator->SetName(allocatorName);
		m_AllocatorPool.push_back(pAllocator);
	}

	return pAllocator;
}

void CommandAllocatorPool::DiscardAllocator(uint64_t fenceValue, ID3D12CommandAllocator* allocator)
{
	std::lock_guard<std::mutex> lockGuard(m_AllocatorMutex);

	// that fence value indicates we are free to reset the allocator
	m_ReadyAllocators.push(std::make_pair(fenceValue, allocator));
}
