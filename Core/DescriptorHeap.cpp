#include "DescriptorHeap.h"
// #include "MyApp.h"		// GetDescriptorIncrementSize

namespace MyDirectX
{
	using Microsoft::WRL::ComPtr;

	std::mutex DescriptorAllocator::s_AllocationMutex;
	std::vector<ComPtr<ID3D12DescriptorHeap>> DescriptorAllocator::s_DescriptorHeapPool;

	D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocator::Allocate(ID3D12Device* pDevice, uint32_t count)
	{
		if (m_CurrentHeap == nullptr || m_RemainingFreeHandles < count)
		{
			ASSERT(pDevice != nullptr);

			m_CurrentHeap = RequestNewHeap(pDevice, m_Type);
			m_CurrentHandle = m_CurrentHeap->GetCPUDescriptorHandleForHeapStart();
			m_RemainingFreeHandles = s_NumDescriptorsPerHeap;

			if (m_DescriptorSize == 0)
				m_DescriptorSize = pDevice->GetDescriptorHandleIncrementSize(m_Type);
		}

		D3D12_CPU_DESCRIPTOR_HANDLE ret = m_CurrentHandle;
		m_CurrentHandle.ptr += count * m_DescriptorSize;
		m_RemainingFreeHandles -= count;

		return ret;
	}

	void DescriptorAllocator::DestroyAll()
	{
		s_DescriptorHeapPool.clear();
	}

	ID3D12DescriptorHeap* DescriptorAllocator::RequestNewHeap(ID3D12Device* pDevice, D3D12_DESCRIPTOR_HEAP_TYPE type)
	{
		ASSERT(pDevice != nullptr);

		std::lock_guard<std::mutex> lockGuard(s_AllocationMutex);

		D3D12_DESCRIPTOR_HEAP_DESC heapDesc;
		heapDesc.Type = type;
		heapDesc.NumDescriptors = s_NumDescriptorsPerHeap;
		heapDesc.NodeMask = 1;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		ComPtr<ID3D12DescriptorHeap> pHeap;
		ASSERT_SUCCEEDED(pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&pHeap)));
		s_DescriptorHeapPool.emplace_back(pHeap);

		return pHeap.Get();
	}

	/// UserDescriptorHeap
	void UserDescriptorHeap::Create(ID3D12Device* pDevice, const std::wstring& debugHeapName)
	{
		ASSERT(pDevice != nullptr);

		ASSERT_SUCCEEDED(pDevice->CreateDescriptorHeap(&m_HeapDesc, IID_PPV_ARGS(m_DescriptorHeap.ReleaseAndGetAddressOf())));
#ifdef RELEASE
		(void)debugHeapName;
#else
		m_DescriptorHeap->SetName(debugHeapName.c_str());
#endif

		m_DescriptorSize = pDevice->GetDescriptorHandleIncrementSize(m_HeapDesc.Type);
		m_NumFreeDescriptors = m_HeapDesc.NumDescriptors;
		m_FirstHandle = DescriptorHandle(m_DescriptorHeap->GetCPUDescriptorHandleForHeapStart(), m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		m_NextFreeHandle = m_FirstHandle;
	}

	void UserDescriptorHeap::Create(ID3D12Device* pDevice, const std::wstring& debugHeapName, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t maxCount)
	{
		m_HeapDesc.Type = type;
		m_HeapDesc.NumDescriptors = maxCount;
		m_HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		m_HeapDesc.NodeMask = 1;
		
		Create(pDevice, debugHeapName);
	}

	DescriptorHandle UserDescriptorHeap::Alloc(uint32_t count)
	{
		ASSERT(HasAvailableSpace(count), "Descriptor Heap out of space. Increase heap size");
		DescriptorHandle ret = m_NextFreeHandle;
		m_NextFreeHandle += count * m_DescriptorSize;
		m_NumFreeDescriptors -= count;
		return ret;
	}

	bool UserDescriptorHeap::ValidateHandle(const DescriptorHandle& descHandle) const
	{
		if (descHandle.GetCpuPtr() < m_FirstHandle.GetCpuPtr() ||
			descHandle.GetCpuPtr() >= m_FirstHandle.GetCpuPtr() + m_HeapDesc.NumDescriptors * m_DescriptorSize)
			return false;

		if (descHandle.GetGpuPtr() - m_FirstHandle.GetGpuPtr() !=
			descHandle.GetCpuPtr() - m_FirstHandle.GetCpuPtr())
			return false;

		return true;
	}
}
