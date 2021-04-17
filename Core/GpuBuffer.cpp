#include "GpuBuffer.h"
#include "UploadBuffer.h"
#include "Graphics.h"
#include "CommandContext.h"

using namespace MyDirectX;

void GpuBuffer::Create(ID3D12Device* pDevice, const std::wstring& name, uint32_t numElements, uint32_t elementSize, const void* initialData)
{
	// -mf
	ASSERT(pDevice != nullptr);

	Destroy();

	m_ElementCount = numElements;
	m_ElementSize = elementSize;
	m_BufferSize = numElements * elementSize;

	D3D12_RESOURCE_DESC resourceDesc = DescribeBuffer();

	m_UsageState = D3D12_RESOURCE_STATE_COMMON;

	D3D12_HEAP_PROPERTIES heapProps;
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProps.CreationNodeMask = 1;
	heapProps.VisibleNodeMask = 1;

	ASSERT_SUCCEEDED(pDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
		&resourceDesc, m_UsageState, nullptr, IID_PPV_ARGS(&m_pResource)));

	m_GpuVirtualAddress = m_pResource->GetGPUVirtualAddress();

	if (initialData)
	{
		CommandContext::InitializeBuffer(*this, initialData, m_BufferSize);
	}

#ifdef RELEASE
	(name)
#else
	m_pResource->SetName(name.c_str());
#endif

	CreateDerivedViews(pDevice);
}

void GpuBuffer::Create(ID3D12Device* pDevice, const std::wstring& name, uint32_t numElements, uint32_t elementSize,
	const UploadBuffer& srcData, uint32_t srcOffset)
{
	Destroy();

	m_ElementCount = numElements;
	m_ElementSize = elementSize;
	m_BufferSize = numElements * elementSize;

	D3D12_RESOURCE_DESC resourceDesc = DescribeBuffer();

	m_UsageState = D3D12_RESOURCE_STATE_COMMON;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProps.CreationNodeMask = 1;
	heapProps.VisibleNodeMask = 1;

	ASSERT_SUCCEEDED(pDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
		&resourceDesc, m_UsageState, nullptr, IID_PPV_ARGS(&m_pResource)));

	CommandContext::InitializeBuffer(*this, srcData, srcOffset);

#ifdef RELEASE
	(name);
#else
	m_pResource->SetName(name.c_str());
#endif

	CreateDerivedViews(pDevice);
}

// sub-allocate a buffer out of a pre-allocated heap. If initial data is provided, it will be copied into
// the buffer using the default command context
// 
void GpuBuffer::CreatePlaced(ID3D12Device* pDevice, const std::wstring& name, ID3D12Heap* pBackingHeap, uint32_t heapOffset, uint32_t numElements, uint32_t elementSize, const void* initialData)
{
	// -mf
	ASSERT(pDevice != nullptr);

	m_ElementCount = numElements;
	m_ElementSize = elementSize;
	m_BufferSize = numElements * elementSize;

	D3D12_RESOURCE_DESC resourceDesc = DescribeBuffer();

	m_UsageState = D3D12_RESOURCE_STATE_COMMON;

	ASSERT_SUCCEEDED(pDevice->CreatePlacedResource(pBackingHeap, heapOffset, &resourceDesc, m_UsageState,
		nullptr, IID_PPV_ARGS(&m_pResource)));

	m_GpuVirtualAddress = m_pResource->GetGPUVirtualAddress();

	if (initialData)
	{
		CommandContext::InitializeBuffer(*this, initialData, m_BufferSize);
	}

#ifdef RELEASE
	(name)
#else
	m_pResource->SetName(name.c_str());
#endif

	CreateDerivedViews(pDevice);
}

D3D12_CPU_DESCRIPTOR_HANDLE GpuBuffer::CreateConstantBufferView(ID3D12Device* pDevice, uint32_t offset, uint32_t size) const
{
	ASSERT(pDevice != nullptr);

	ASSERT(offset + size <= m_BufferSize);

	size = Math::AlignUp(size, 16);

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
	cbvDesc.BufferLocation = m_GpuVirtualAddress + (size_t)offset;
	cbvDesc.SizeInBytes = size;

	D3D12_CPU_DESCRIPTOR_HANDLE hCBV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	pDevice->CreateConstantBufferView(&cbvDesc, hCBV);

	return hCBV;
}

D3D12_RESOURCE_DESC GpuBuffer::DescribeBuffer()
{
	ASSERT(m_BufferSize != 0);

	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Flags = m_ResourceFlags;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.Height = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Width = (UINT64)m_BufferSize;
	
	return desc;
}

// ByteAddressBuffer
void ByteAddressBuffer::CreateDerivedViews(ID3D12Device* pDevice)
{
	// srv
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.NumElements = (UINT)m_BufferSize / 4;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;

	if (m_SRV.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
	{
		m_SRV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	pDevice->CreateShaderResourceView(m_pResource.Get(), &srvDesc, m_SRV);

	// uav
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	uavDesc.Buffer.NumElements = (UINT)m_BufferSize / 4;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
	if (m_UAV.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
	{
		m_UAV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	pDevice->CreateUnorderedAccessView(m_pResource.Get(), nullptr, &uavDesc, m_UAV);
}

// StructuredBuffer
void StructuredBuffer::CreateDerivedViews(ID3D12Device* pDevice)
{
	// srv
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.NumElements = m_ElementCount;
	srvDesc.Buffer.StructureByteStride = m_ElementSize;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	if (m_SRV.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
	{
		m_SRV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	pDevice->CreateShaderResourceView(m_pResource.Get(), &srvDesc, m_SRV);

	// uav
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.Buffer.CounterOffsetInBytes = 0;
	uavDesc.Buffer.NumElements = m_ElementCount;
	uavDesc.Buffer.StructureByteStride = m_ElementSize;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	m_CounterBuffer.Create(pDevice, L"StructuredBuffer::Counter", 1, 4);

	if (m_UAV.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
	{
		m_UAV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	pDevice->CreateUnorderedAccessView(m_pResource.Get(), m_CounterBuffer.GetResource(), &uavDesc, m_UAV);
}

const D3D12_CPU_DESCRIPTOR_HANDLE& StructuredBuffer::GetCounterSRV(CommandContext& context)
{
	context.TransitionResource(m_CounterBuffer, D3D12_RESOURCE_STATE_GENERIC_READ);
	return m_CounterBuffer.GetSRV();
}

const D3D12_CPU_DESCRIPTOR_HANDLE& StructuredBuffer::GetCounterUAV(CommandContext& context)
{
	context.TransitionResource(m_CounterBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	return m_CounterBuffer.GetUAV();
}

// TypedBuffer
void TypedBuffer::CreateDerivedViews(ID3D12Device* pDevice)
{
	// srv
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Format = m_DataFormat;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.NumElements = m_ElementCount;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	if (m_SRV.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
	{
		m_SRV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	pDevice->CreateShaderResourceView(m_pResource.Get(), &srvDesc, m_SRV);

	// uav
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uavDesc.Format = m_DataFormat;
	uavDesc.Buffer.NumElements = m_ElementCount;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	if (m_UAV.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
	{
		m_UAV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	pDevice->CreateUnorderedAccessView(m_pResource.Get(), nullptr, &uavDesc, m_UAV);
}

/**
	https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createunorderedaccessview
	ID3d12Device::CreateUnorderedAccessView
	>> create a view for unordered accessing
	void CreateUnorderedAccessView(
	  ID3D12Resource                         *pResource,
	  ID3D12Resource                         *pCounterResource,
	  const D3D12_UNORDERED_ACCESS_VIEW_DESC *pDesc,
	  D3D12_CPU_DESCRIPTOR_HANDLE            DestDescriptor
	);

	> ID3D12Resource *pResource - a pointer to the ID3D12Resource object that represents the unordered access
	at least one of pResource or pDesc must be provided. A null pResource is used to initialize a null descriptor,
which guarantees D3D11-like null binding behavior (readings 0s, writes are discarded), but must have a valid 
pDesc in order to determine the descriptor type.
	> ID3D12Resource *pCounterResource - the ID3D12Resource for the counter (if any) associated with the UAV
	if pCounterResource is not specified, the CounterOffsetInBytes member of the D3D12_BUFFER_UAV structure must be 0.
	if pCounterResource is specified, then there is a counter associated with the UAV, and the runtime performs
validation of the following requirements:
		
    1.The StructureByteStride member of the D3D12_BUFFER_UAV structure must be greater than 0.
    2.The format must be DXGI_FORMAT_UNKNOWN.
    3.The D3D12_BUFFER_UAV_FLAG_RAW flag (a D3D12_BUFFER_UAV_FLAGS enumeration constant) must not be set.
    4.Both of the resources (pResource and pCounterResource) must be buffers.
    5.The CounterOffsetInBytes member of the D3D12_BUFFER_UAV structure must be a multiple of 4 bytes, and must be within the range of the counter resource.
    6.pResource cannot be NULL
    7.pDesc cannot be NULL.
*/
