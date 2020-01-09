#pragma once
#include "pch.h"
#include <queue>
#include <mutex>

namespace MyDirectX
{
	/**
		this is an unbounded resource descriptor allocator. It is intended to provide space for CPU-visible
	resource descriptors as resources are created. For those that need to be made shader-visible, they will
	need to be copied to a UserDescriptorHeap or a DynamicDescriptorHeap.
	*/
	class DescriptorAllocator
	{
	public:
		DescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE type) : m_Type(type), m_CurrentHeap(nullptr) {  }

		D3D12_CPU_DESCRIPTOR_HANDLE Allocate(ID3D12Device *pDevice, uint32_t count);

		static void DestroyAll();

	protected:
		static const uint32_t s_NumDescriptorsPerHeap = 256;
		static std::mutex s_AllocationMutex;
		static std::vector<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>> s_DescriptorHeapPool;
		static ID3D12DescriptorHeap* RequestNewHeap(ID3D12Device *pDevice, D3D12_DESCRIPTOR_HEAP_TYPE type);

		D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
		ID3D12DescriptorHeap* m_CurrentHeap;
		D3D12_CPU_DESCRIPTOR_HANDLE m_CurrentHandle;
		uint32_t m_DescriptorSize = 0;
		uint32_t m_RemainingFreeHandles = 0;
	};

	//
	class DescriptorHandle
	{
	public:
		DescriptorHandle()
		{
			m_CpuHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
			m_GpuHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
		}
		DescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle)
			: m_CpuHandle(cpuHandle)
		{
			m_GpuHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
		}
		DescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
			: m_CpuHandle(cpuHandle), m_GpuHandle(gpuHandle)
		{	}

		DescriptorHandle operator+(INT offsetScaledByDescriptorSize) const
		{
			DescriptorHandle ret = *this;
			ret += offsetScaledByDescriptorSize;
			return ret;
		}
		void operator+=(INT offsetScaledByDescriptorSize)
		{
			if (m_CpuHandle.ptr != D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
				m_CpuHandle.ptr += offsetScaledByDescriptorSize;
			if (m_GpuHandle.ptr != D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
				m_GpuHandle.ptr += offsetScaledByDescriptorSize;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle() const { return m_CpuHandle; }

		D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle() const { return m_GpuHandle; }

		bool IsNull() const { return m_CpuHandle.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN; }
		bool IsShaderVisible() const { return m_GpuHandle.ptr != D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN; }

	private:
		D3D12_CPU_DESCRIPTOR_HANDLE m_CpuHandle;
		D3D12_GPU_DESCRIPTOR_HANDLE m_GpuHandle;
	};

	// 
	class UserDescriptorHeap
	{
	public:
		UserDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t maxCount)
		{
			m_HeapDesc.Type = type;
			m_HeapDesc.NumDescriptors = maxCount;
			m_HeapDesc.NodeMask = 1;
			m_HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		}

		void Create(ID3D12Device* pDevice, const std::wstring& debugHeapName);

		bool HasAvailableSpace(uint32_t count) const { return count <= m_NumFreeDescriptors; }
		DescriptorHandle Alloc(uint32_t count = 1);

		DescriptorHandle GetHandleAtOffset(uint32_t offset) const { return m_FirstHandle + offset * m_DescriptorSize; }

		bool ValidateHandle(const DescriptorHandle& descHandle) const;

		ID3D12DescriptorHeap* GetHeapPointer() const { return m_DescriptorHeap.Get(); }

	private:
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DescriptorHeap;
		D3D12_DESCRIPTOR_HEAP_DESC m_HeapDesc;
		uint32_t m_DescriptorSize = 0;
		uint32_t m_NumFreeDescriptors = 0;
		DescriptorHandle m_FirstHandle;
		DescriptorHandle m_NextFreeHandle;
	};

}