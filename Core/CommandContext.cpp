#include "CommandContext.h"
#include "Graphics.h"
#include "CommandListManager.h"

using namespace MyDirectX;

void CommandContext::DestroyAllContexts()
{
	LinearAllocator::DestroyAll();
	// DynamicDescriptorHeap::DestroyAll();
	Graphics::s_ContextManager.DestroyAllContexts();
}

CommandContext& CommandContext::Begin(const std::wstring& ID)
{
	CommandContext* pNewContext = Graphics::s_ContextManager.AllocateContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	pNewContext->SetID(ID);

	// TODO

	return *pNewContext;
}

CommandContext::CommandContext(D3D12_COMMAND_LIST_TYPE type)
	: m_Type(type),
	m_CpuLinearAllocator(LinearAllocatorType::kCpuWritable),
	m_GpuLinearAllocator(LinearAllocatorType::kGpuExclusive)
{
	// -mf
	m_Device = nullptr;

	m_CommandManager = nullptr;
	m_CommandList = nullptr;
	m_CurAllocator = nullptr;

	m_CurGraphicsRootSignature = nullptr;
	m_CurComputeRootSignature = nullptr;
	m_CurPipelineState = nullptr;

	m_NumBarriersToFlush = 0;

	ZeroMemory(m_CurDescriptorHeaps, sizeof(m_CurDescriptorHeaps));
		
}

void CommandContext::Reset()
{
	// we only call Reset() on previously freed contexts. The command list persists, but we must 
	// request a new allocator
	ASSERT(m_CommandList != nullptr && m_CurAllocator == nullptr);
	m_CurAllocator = Graphics::s_CommandManager.GetQueue(m_Type).RequestAllocator();
	m_CommandList->Reset(m_CurAllocator, nullptr);

	m_CurGraphicsRootSignature = nullptr;
	m_CurComputeRootSignature = nullptr;
	m_CurPipelineState = nullptr;

	m_NumBarriersToFlush = 0;

	BindDescriptorHeaps();
}

CommandContext::~CommandContext()
{
	if (m_CommandList != nullptr)
		m_CommandList->Release();
}

uint64_t CommandContext::Flush(bool bWaitForCompletion)
{
	FlushResourceBarriers();

	ASSERT(m_CurAllocator != nullptr);

	uint64_t fenceValue = Graphics::s_CommandManager.GetQueue(m_Type).ExecuteCommandList(m_CommandList);

	if (bWaitForCompletion)
	{
		Graphics::s_CommandManager.WaitForFence(fenceValue);
	}

	// reset the command list and restore previous state
	m_CommandList->Reset(m_CurAllocator, nullptr);

	if (m_CurGraphicsRootSignature)
	{
		m_CommandList->SetGraphicsRootSignature(m_CurGraphicsRootSignature);
	}
	if (m_CurComputeRootSignature)
	{
		m_CommandList->SetComputeRootSignature(m_CurComputeRootSignature);
	}
	if (m_CurPipelineState)
	{
		m_CommandList->SetPipelineState(m_CurPipelineState);
	}

	BindDescriptorHeaps();

	return fenceValue;
}

uint64_t CommandContext::Finish(bool bWaitForCompletion)
{
	ASSERT(m_Type == D3D12_COMMAND_LIST_TYPE_DIRECT || m_Type == D3D12_COMMAND_LIST_TYPE_COMPUTE);

	FlushResourceBarriers();

	//

	ASSERT(m_CurAllocator != nullptr);

	CommandQueue& cmdQueue = Graphics::s_CommandManager.GetQueue(m_Type);

	uint64_t fenceValue = cmdQueue.ExecuteCommandList(m_CommandList);
	cmdQueue.DiscardAllocator(fenceValue, m_CurAllocator);
	m_CurAllocator = nullptr;

	m_CpuLinearAllocator.CleanupUsedPages(fenceValue);
	m_GpuLinearAllocator.CleanupUsedPages(fenceValue);
	// ипн╢й╣ож DynamicDescriptorHeap -20-1-12
	// m_DynamicViewDescriptorHeap.CleanupUsedHeaps(fenceValue);
	// m_DynamicSamplerDescriptorHeap.CleanupUsedHeaps(fenceValue);

	if (bWaitForCompletion)
		Graphics::s_CommandManager.WaitForFence(fenceValue);

	Graphics::s_ContextManager.FreeContext(this);

	return fenceValue;
}

void CommandContext::Initialize(ID3D12Device* pDevice)
{
	m_Device = pDevice;
	Graphics::s_CommandManager.CreateNewCommandList(m_Type, &m_CommandList, &m_CurAllocator);
}

void CommandContext::CopySubresource(GpuResource& dest, UINT destSubIndex, GpuResource& src, UINT srcSubIndex)
{
	FlushResourceBarriers();

	D3D12_TEXTURE_COPY_LOCATION destLocation =
	{
		dest.GetResource(),
		D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
		destSubIndex
	};
	D3D12_TEXTURE_COPY_LOCATION srcLocation =
	{
		src.GetResource(),
		D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
		srcSubIndex
	};
	m_CommandList->CopyTextureRegion(&destLocation, 0, 0, 0, &srcLocation, nullptr);
}

void CommandContext::InitializeTexture(GpuResource& dest, UINT numSubresources, D3D12_SUBRESOURCE_DATA subData[])
{
	UINT64 uploadBufferSize = GetRequiredIntermediateSize(dest.GetResource(), 0, numSubresources);

	CommandContext& initContext = CommandContext::Begin();

	// copy data to the intermediate upload heap and then schedule a copy from the upload heap to the defualt texture 
	DynAlloc mem = initContext.ReserveUploadMemory(uploadBufferSize);
	UpdateSubresources(initContext.m_CommandList, dest.GetResource(), mem.buffer.GetResource(), 0, 0, numSubresources, subData);
	initContext.TransitionResource(dest, D3D12_RESOURCE_STATE_GENERIC_READ);

	// execute the command list and wait for it to finish so we can release the upload buffer
	initContext.Finish(true);
}

void CommandContext::InitializeBuffer(GpuResource& dest, const void* data, size_t numBytes, size_t offset)
{
	CommandContext& initContext = CommandContext::Begin();

	DynAlloc mem = initContext.ReserveUploadMemory(numBytes);
	SIMDMemCopy(mem.dataPtr, data, Math::DivideByMultiple(numBytes, 16));

	// copy data to the intermediate upload heap and then schedule a copy from the upload heap to the default buffer
	initContext.TransitionResource(dest, D3D12_RESOURCE_STATE_COPY_DEST, true);
	initContext.m_CommandList->CopyBufferRegion(dest.GetResource(), offset, mem.buffer.GetResource(), 0, numBytes);
	initContext.TransitionResource(dest, D3D12_RESOURCE_STATE_GENERIC_READ, true);

	// execute the command list and wait for it to finish so we can release the upload buffer
	initContext.Finish(true);
}

void CommandContext::InitializeTextureArraySlice(GpuResource& dest, UINT sliceIndex, GpuResource& src)
{
	CommandContext& context = CommandContext::Begin();

	context.TransitionResource(dest, D3D12_RESOURCE_STATE_COPY_DEST);
	context.FlushResourceBarriers();

	const D3D12_RESOURCE_DESC& destDesc = dest.GetResource()->GetDesc();
	const D3D12_RESOURCE_DESC& srcDest = src.GetResource()->GetDesc();

	ASSERT(sliceIndex < destDesc.DepthOrArraySize &&
		srcDest.DepthOrArraySize == 1 &&
		destDesc.Width == srcDest.Width &&
		destDesc.Height == srcDest.Height &&
		destDesc.MipLevels <= srcDest.MipLevels);

	UINT subResourceIndex = sliceIndex * destDesc.MipLevels;

	for (UINT i = 0; i < destDesc.MipLevels; ++i)
	{
		D3D12_TEXTURE_COPY_LOCATION destCopyLocation =
		{
			dest.GetResource(),
			D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
			subResourceIndex + i
		};

		D3D12_TEXTURE_COPY_LOCATION srcCopyLocation =
		{
			src.GetResource(),
			D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
			i
		};
		context.m_CommandList->CopyTextureRegion(&destCopyLocation, 0, 0, 0, &srcCopyLocation, nullptr);
	}

	context.TransitionResource(dest, D3D12_RESOURCE_STATE_GENERIC_READ);
	context.Finish(true);
}

void CommandContext::ReadbackTexture2D(GpuResource& readbackBuffer, PixelBuffer& srcBuffer)
{
	// the footprint may depend on the device of the resource, but we can assume there is only one device
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT placedFootprint;
	Graphics::s_Device->GetCopyableFootprints(&srcBuffer.GetResource()->GetDesc(), 0, 1, 0, &placedFootprint, nullptr, nullptr, nullptr);

	// this very short command list only issues one API call and will be synchronized so we can immediately read
	// the buffer contents
	CommandContext& context = CommandContext::Begin(L"Copy texture to memory");

	context.TransitionResource(srcBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, true);

	context.m_CommandList->CopyTextureRegion(
		&CD3DX12_TEXTURE_COPY_LOCATION(readbackBuffer.GetResource(), placedFootprint), 0, 0, 0,
		&CD3DX12_TEXTURE_COPY_LOCATION(srcBuffer.GetResource(), 0), nullptr);

	context.Finish(true);
}

void CommandContext::WriteBuffer(GpuResource& dest, size_t destOffset, const void* data, size_t numBytes)
{
	ASSERT(data != nullptr && Math::IsAligned(numBytes, 16));
	DynAlloc tempSpace = m_CpuLinearAllocator.Allocate(m_Device, numBytes, 512);
	SIMDMemCopy(tempSpace.dataPtr, data, Math::DivideByMultiple(numBytes, 16));
	CopyBufferRegion(dest, destOffset, tempSpace.buffer, tempSpace.offset, numBytes);
}

void CommandContext::FillBuffer(GpuResource& dest, size_t destOffset, DWParam value, size_t numBytes)
{
	DynAlloc tempSpace = m_CpuLinearAllocator.Allocate(m_Device, numBytes, 512);
	__m128 vectorValue = _mm_set1_ps(value.Float);
	SIMDMemFill(tempSpace.dataPtr, vectorValue, Math::DivideByMultiple(numBytes, 16));
	CopyBufferRegion(dest, destOffset, tempSpace.buffer, tempSpace.offset, numBytes);
}

void CommandContext::TransitionResource(GpuResource& resource, D3D12_RESOURCE_STATES newState, bool flushImmediate)
{
	D3D12_RESOURCE_STATES oldState = resource.m_UsageState;

	if (m_Type == D3D12_COMMAND_LIST_TYPE_COMPUTE)
	{
		ASSERT((oldState & VALID_COMPUTE_QUEUE_RESOURCE_STATE) == oldState);
		ASSERT((newState & VALID_COMPUTE_QUEUE_RESOURCE_STATE) == newState);
	}

	if (oldState != newState)
	{
		ASSERT(m_NumBarriersToFlush < 16, "Exceeded arbitrary limit on buffered barriers");
		D3D12_RESOURCE_BARRIER& barrierDesc = m_ResourceBarrierBuffer[m_NumBarriersToFlush++];

		barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrierDesc.Transition.pResource = resource.GetResource();
		barrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrierDesc.Transition.StateBefore = oldState;
		barrierDesc.Transition.StateAfter = newState;

		//check to see if we already started the transition
		if (newState == resource.m_TransitioningState)
		{
			barrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
			resource.m_TransitioningState = (D3D12_RESOURCE_STATES)-1;
		}
		else
		{
			barrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		}		
	}
	else if (newState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
	{
		InsertUAVBarrier(resource, flushImmediate);
	}

	if (flushImmediate || m_NumBarriersToFlush == 16)
		FlushResourceBarriers();
}

void CommandContext::BeginResourceTransition(GpuResource& resource, D3D12_RESOURCE_STATES newState, bool flushImmediate)
{
	// if it's already transitioning, finish that transition
	if (resource.m_TransitioningState != (D3D12_RESOURCE_STATES)-1)
		TransitionResource(resource, resource.m_TransitioningState);

	D3D12_RESOURCE_STATES oldState = resource.m_UsageState;

	if (oldState != newState)
	{
		ASSERT(m_NumBarriersToFlush < 16, "Exceeded arbitrary limit on buffered barriers");
		D3D12_RESOURCE_BARRIER& barrierDesc = m_ResourceBarrierBuffer[m_NumBarriersToFlush++];

		barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrierDesc.Transition.pResource = resource.GetResource();
		barrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrierDesc.Transition.StateBefore = oldState;
		barrierDesc.Transition.StateAfter = newState;

		barrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;

		resource.m_TransitioningState = newState;
	}

	if (flushImmediate || m_NumBarriersToFlush == 16)
		FlushResourceBarriers();
}

void CommandContext::InsertUAVBarrier(GpuResource& resource, bool flushImmediate)
{
	ASSERT(m_NumBarriersToFlush < 16, "Exceeded arbitrary limit on buffered barriers");
	D3D12_RESOURCE_BARRIER barrierDesc = m_ResourceBarrierBuffer[m_NumBarriersToFlush++];

	barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	barrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrierDesc.UAV.pResource = resource.GetResource();

	if (flushImmediate)
		FlushResourceBarriers();
}

void CommandContext::InsertAliasBarrier(GpuResource& before, GpuResource& after, bool flushImmediate)
{
	ASSERT(m_NumBarriersToFlush < 16, "Exceeded arbitrary limit on buffered barriers");
	D3D12_RESOURCE_BARRIER& barrierDesc = m_ResourceBarrierBuffer[m_NumBarriersToFlush++];

	barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
	barrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrierDesc.Aliasing.pResourceBefore = before.GetResource();
	barrierDesc.Aliasing.pResourceAfter = after.GetResource();

	if (flushImmediate)
		FlushResourceBarriers();
}

void CommandContext::PIXBeginEvent(const wchar_t* label)
{
#ifdef RELEASE 
	(label);
#else
	::PIXBeginEvent(m_CommandList, 0, label);
#endif
}

void CommandContext::PIXEndEvent()
{
#ifndef RELEASE
	::PIXEndEvent(m_CommandList);
#endif
}

void CommandContext::PIXSetMarker(const wchar_t* label)
{
#ifdef RELEASE
	(label);
#else
	::PIXSetMarker(m_CommandList, 0, label);
#endif
}

void CommandContext::BindDescriptorHeaps()
{
	UINT nonNullHeaps = 0;
	ID3D12DescriptorHeap* heapsToBind[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];
	for (UINT i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
	{
		ID3D12DescriptorHeap* pHeap = m_CurDescriptorHeaps[i];
		if (pHeap != nullptr)
			heapsToBind[nonNullHeaps++] = pHeap;
	}

	if (nonNullHeaps)
		m_CommandList->SetDescriptorHeaps(nonNullHeaps, heapsToBind);
}

