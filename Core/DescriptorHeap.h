#pragma once
#include "pch.h"
#include <queue>
#include <mutex>

namespace MyDirectX
{
	/**
		This is an unbounded resource descriptor allocator. It is intended to provide space for CPU-visible
	resource descriptors as resources are created. For those that need to be made shader-visible, they will
	need to be copied to a UserDescriptorHeap or a DynamicDescriptorHeap.
	*/
	class DescriptorAllocator
	{
	public:
		DescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE type) : m_Type(type), m_CurrentHeap(nullptr)
		{ 
			m_CurrentHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
		}

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

	// This handle refers to a descriptor or a descriptor table (contiguous descriptors) that is shader visible
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

		const D3D12_CPU_DESCRIPTOR_HANDLE *operator&() const { return &m_CpuHandle; }
		operator D3D12_CPU_DESCRIPTOR_HANDLE() const { return m_CpuHandle; }
		operator D3D12_GPU_DESCRIPTOR_HANDLE() const { return m_GpuHandle; }

		D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle() const { return m_CpuHandle; }
		D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle() const { return m_GpuHandle; }

		size_t GetCpuPtr() const { return m_CpuHandle.ptr; }
		const size_t &GetCpuPtrRef() const { return m_CpuHandle.ptr; }
		uint64_t GetGpuPtr() const { return m_GpuHandle.ptr; }
		const uint64_t &GetGpuPtrRef() const { return m_GpuHandle.ptr; }

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
		UserDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, uint32_t maxCount = 1024)
		{
			m_HeapDesc.Type = type;
			m_HeapDesc.NumDescriptors = maxCount;
			m_HeapDesc.NodeMask = 1;
			m_HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		}
		~UserDescriptorHeap() { Destroy(); }

		void Create(ID3D12Device* pDevice, const std::wstring& debugHeapName);
		void Create(ID3D12Device *pDevice, const std::wstring &debugHeapName, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t maxCount);
		void Destroy() { m_DescriptorHeap = nullptr; }

		bool HasAvailableSpace(uint32_t count) const { return count <= m_NumFreeDescriptors; }
		DescriptorHandle Alloc(uint32_t count = 1);

		const DescriptorHandle &operator[] (uint32_t arrayIdx) const { return m_FirstHandle + arrayIdx * m_DescriptorSize; }
		DescriptorHandle &operator[] (uint32_t arrayIdx) { return m_FirstHandle + arrayIdx * m_DescriptorSize; }

		uint32_t GetCapacity() const { return m_HeapDesc.NumDescriptors; }
		D3D12_DESCRIPTOR_HEAP_TYPE GetType() const { return m_HeapDesc.Type; }
		ID3D12DescriptorHeap* GetHeapPointer() const { return m_DescriptorHeap.Get(); }
		uint32_t GetDescriptorSize() const { return m_DescriptorSize; }
		DescriptorHandle GetHandleAtOffset(uint32_t offset) const { return m_FirstHandle + offset * m_DescriptorSize; }
		uint32_t GetOffsetOfHandle(const DescriptorHandle& handle) { return (uint32_t)(handle.GetCpuPtr() - m_FirstHandle.GetCpuPtr()) / m_DescriptorSize; }
		uint32_t GetAllocedCount() const { return m_HeapDesc.NumDescriptors - m_NumFreeDescriptors; }
		bool ValidateHandle(const DescriptorHandle& descHandle) const;

	private:
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DescriptorHeap;
		D3D12_DESCRIPTOR_HEAP_DESC m_HeapDesc;
		uint32_t m_DescriptorSize = 0;
		uint32_t m_Capacity = 0;
		uint32_t m_NumFreeDescriptors = 0;
		DescriptorHandle m_FirstHandle;
		DescriptorHandle m_NextFreeHandle;
	};
}
