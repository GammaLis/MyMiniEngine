#pragma once
#include "pch.h"
#include "GpuResource.h"

namespace MyDirectX
{
	class CommandContext;

	class GpuBuffer : public GpuResource
	{
	public:
		virtual ~GpuBuffer() { Destroy(); }

		// create a buffer. If initial data is provided, it will be copied into the buffer using the default 
		// command context
		void Create(const std::wstring& name, uint32_t numElements, uint32_t elementSize,
			const void* initialData = nullptr);

		// sub-allocate a buffer out of a pre-allocated heap. If initial data is provided, it will be copied into
		// the buffer
		void CreatePlaced(const std::wstring& name, ID3D12Heap* pBackingHeap, uint32_t heapOffset, uint32_t numElements,
			uint32_t elementSize, const void* initialData = nullptr);

		const D3D12_CPU_DESCRIPTOR_HANDLE GetUAV() const { return m_UAV; }
		const D3D12_CPU_DESCRIPTOR_HANDLE GetSRV() const { return m_SRV; }

		D3D12_GPU_VIRTUAL_ADDRESS RootConstantBufferView() const { return m_GpuVirtualAddress; }

		D3D12_CPU_DESCRIPTOR_HANDLE CreateConstantBufferView(uint32_t offset, uint32_t size) const;

		D3D12_VERTEX_BUFFER_VIEW VertexBufferView(size_t offset, uint32_t size, uint32_t stride) const;
		D3D12_VERTEX_BUFFER_VIEW VertexBufferView(size_t baseVertexIndex = 0) const
		{
			size_t offset = baseVertexIndex * m_ElementSize;
			return VertexBufferView(offset, (uint32_t)(m_BufferSize - offset), m_ElementSize);
		}

		D3D12_INDEX_BUFFER_VIEW IndexBufferView(size_t offset, uint32_t size, bool b32Bit = false) const;
		D3D12_INDEX_BUFFER_VIEW IndexBufferView(size_t startIndex = 0) const
		{
			size_t offset = startIndex * m_ElementSize;
			return IndexBufferView(offset, (uint32_t)(m_BufferSize - offset), m_ElementSize);
		}

		size_t GetBufferSize() const {}
		uint32_t GetElementCount() const {}
		uint32_t GetElementSize() const {}

	protected:
		GpuBuffer() : m_BufferSize(0), m_ElementCount(0), m_ElementSize(0)
		{
			m_ResourceFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			m_UAV.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
			m_SRV.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
		}

		D3D12_RESOURCE_DESC DescribeBuffer();
		virtual void CreateDerivedViews() = 0;

		D3D12_CPU_DESCRIPTOR_HANDLE m_UAV;
		D3D12_CPU_DESCRIPTOR_HANDLE m_SRV;

		size_t m_BufferSize;
		uint32_t m_ElementCount;
		uint32_t m_ElementSize;
		D3D12_RESOURCE_FLAGS m_ResourceFlags;
		
	};

	inline D3D12_VERTEX_BUFFER_VIEW GpuBuffer::VertexBufferView(size_t offset, uint32_t size, uint32_t stride) const
	{
		D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
		vertexBufferView.BufferLocation = m_GpuVirtualAddress + offset;
		vertexBufferView.SizeInBytes = size;
		vertexBufferView.StrideInBytes = stride;
		return vertexBufferView;
	}

	inline D3D12_INDEX_BUFFER_VIEW GpuBuffer::IndexBufferView(size_t offset, uint32_t size, bool b32Bit) const
	{
		D3D12_INDEX_BUFFER_VIEW indexBufferView = {};
		indexBufferView.BufferLocation = m_GpuVirtualAddress + offset;
		indexBufferView.SizeInBytes = size;
		indexBufferView.Format = b32Bit ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
		return indexBufferView;
	}

	/**
		MSDN
		ByteAddressBuffer - a read-only buffer that is indexed in bytes
		you can use the ByteAddressBuffer object type when you work with raw buffers.
	*/
	class ByteAddressBuffer : public GpuBuffer 
	{
	public:
		virtual void CreateDerivedViews() override;
	};

	class IndirectArgsBuffer : public ByteAddressBuffer
	{
	public:
		IndirectArgsBuffer() {  }
	};

	// 
	class StructuredBuffer : public GpuBuffer 
	{
	public:
		virtual void Destroy() override
		{
			m_CounterBuffer.Destroy();
			GpuBuffer::Destroy();
		}

		virtual void CreateDerivedViews() override;

		ByteAddressBuffer& GetCounterBuffer() { return m_CounterBuffer; }

		const D3D12_CPU_DESCRIPTOR_HANDLE& GetCounterSRV(CommandContext& context);
		const D3D12_CPU_DESCRIPTOR_HANDLE& GetCounterUAV(CommandContext& context);

	private:
		ByteAddressBuffer m_CounterBuffer;
	};

	// 
	class TypedBuffer : public GpuBuffer
	{
	public:
		TypedBuffer(DXGI_FORMAT format): m_DataFormat(format) {  }

		virtual void CreateDerivedViews() override;

	protected:
		DXGI_FORMAT m_DataFormat;
	};
}
