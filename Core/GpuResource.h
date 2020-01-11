#pragma once

#include "pch.h"

namespace MyDirectX
{
	class GpuResource
	{
	public:
		GpuResource()
			: m_GpuVirtualAddress(D3D12_GPU_VIRTUAL_ADDRESS_NULL),
			m_UserAllocatedMemory(nullptr),
			m_UsageState(D3D12_RESOURCE_STATE_COMMON),
			m_TransitioningState((D3D12_RESOURCE_STATES)-1)
		{	}
		GpuResource(ID3D12Resource *pResource, D3D12_RESOURCE_STATES currentState)
			: m_GpuVirtualAddress(D3D12_GPU_VIRTUAL_ADDRESS_NULL),
			m_UserAllocatedMemory(nullptr),
			m_pResource(pResource),
			m_UsageState(currentState),
			m_TransitioningState((D3D12_RESOURCE_STATES)-1)
		{  }

		virtual void Destroy()
		{
			m_pResource = nullptr;
			m_GpuVirtualAddress = D3D12_GPU_VIRTUAL_ADDRESS_NULL;
			if (m_UserAllocatedMemory)
			{
				VirtualFree(m_UserAllocatedMemory, 0, MEM_RELEASE);
				m_UserAllocatedMemory = nullptr;
			}
		}

		ID3D12Resource* operator->() { return m_pResource.Get(); }
		const ID3D12Resource* operator->() const { return m_pResource.Get(); }

		ID3D12Resource* GetResource() { return m_pResource.Get(); }
		const ID3D12Resource* GetResource() const { return m_pResource.Get(); }

		D3D12_GPU_VIRTUAL_ADDRESS GetGpuVirtualAddress() const { return m_GpuVirtualAddress; }

	protected:
		Microsoft::WRL::ComPtr<ID3D12Resource> m_pResource;
		D3D12_RESOURCE_STATES m_UsageState;
		D3D12_RESOURCE_STATES m_TransitioningState;
		D3D12_GPU_VIRTUAL_ADDRESS m_GpuVirtualAddress;

		// when using VirtualAlloc() to allocate memory directly, record the allocation here so that it can 
		// be freed. The GpuVirtualAddress may be offset from the true allocation start.
		void* m_UserAllocatedMemory;
	};

}