#include "CommandContext.h"
#include "Graphics.h"
#include "CommandListManager.h"

#include "ColorBuffer.h"
#include "DepthBuffer.h"
#include "GpuBuffer.h"
#include "UploadBuffer.h"
#include "ReadbackBuffer.h"

#include "CommandSignature.h"

#pragma warning(push)
#pragma warning(disable:4100)	// unreferenced formal parameters in PIXCopyEventArguments() (WinPixEventRuntime.1.0.200127001)
#include <pix3.h>
#pragma warning(pop)

using namespace MyDirectX;

CommandContext* ContextManager::AllocateContext(D3D12_COMMAND_LIST_TYPE type)
{
	std::lock_guard<std::mutex> lockGuard(m_ContextAllocationMutex);

	auto& availableContexts = m_AvailableContexts[type];

	CommandContext* ret = nullptr;
	if (availableContexts.empty())
	{
		ret = new CommandContext(type);
		m_ContextPool[type].emplace_back(ret);
		ret->Initialize(Graphics::s_Device);
	}
	else
	{
		ret = availableContexts.front();
		availableContexts.pop();
		ret->Reset();
	}
	ASSERT(ret != nullptr);

	ASSERT(ret->m_Type == type);

	return ret;
}

void ContextManager::FreeContext(CommandContext* usedContext)
{
	ASSERT(usedContext != nullptr);
	std::lock_guard<std::mutex> lockGuard(m_ContextAllocationMutex);

	m_AvailableContexts[usedContext->m_Type].push(usedContext);
}

void ContextManager::DestroyAllContexts()
{
	for (uint32_t i = 0; i < 4; ++i)
		m_ContextPool[i].clear();
}

void CommandContext::DestroyAllContexts()
{
	LinearAllocator::DestroyAll();
	DynamicDescriptorHeap::DestroyAll();
	Graphics::s_ContextManager.DestroyAllContexts();
}

CommandContext& CommandContext::Begin(const std::wstring& ID)
{
	CommandContext* pNewContext = Graphics::s_ContextManager.AllocateContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	pNewContext->SetID(ID);

	// TODO

	return *pNewContext;
}

ComputeContext& ComputeContext::Begin(const std::wstring& ID, bool async)
{
	ComputeContext& newContext = Graphics::s_ContextManager.AllocateContext(
		async ? D3D12_COMMAND_LIST_TYPE_COMPUTE : D3D12_COMMAND_LIST_TYPE_DIRECT)->GetComputeContext();
	newContext.SetID(ID);

	// TODO

	return newContext;
}

CommandContext::CommandContext(D3D12_COMMAND_LIST_TYPE type)
	: m_Type(type),
	m_DynamicViewDescriptorHeap(*this, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
	m_DynamicSamplerDescriptorHeap(*this, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER),
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
	
	m_DynamicViewDescriptorHeap.CleanupUsedHeaps(fenceValue);
	m_DynamicSamplerDescriptorHeap.CleanupUsedHeaps(fenceValue);

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

uint32_t CommandContext::ReadbackTexture(ID3D12Device* pDevice, ReadbackBuffer& dstBuffer, PixelBuffer& srcBuffer)
{
	uint64_t copySize = 0;

	// the footprint may depend on the device of the resource, but we assume there is only one device
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT placedFootprint;
	auto resourceDesc = srcBuffer.GetResource()->GetDesc();
	pDevice->GetCopyableFootprints(&resourceDesc, 0, 1, 0, 
		&placedFootprint, nullptr, nullptr, &copySize);

	dstBuffer.Create(pDevice, L"Readback",(uint32_t)copySize, 1);

	TransitionResource(srcBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, true);

	CD3DX12_TEXTURE_COPY_LOCATION dstLoc(dstBuffer.GetResource(), placedFootprint);
	CD3DX12_TEXTURE_COPY_LOCATION srcLoc(srcBuffer.GetResource(), 0);
	m_CommandList->CopyTextureRegion(&dstLoc, 0, 0, 0,
		&srcLoc, nullptr);

	return placedFootprint.Footprint.RowPitch;
}

void CommandContext::InitializeTexture(GpuResource& dest, UINT numSubresources, D3D12_SUBRESOURCE_DATA subData[])
{
	UINT64 uploadBufferSize = GetRequiredIntermediateSize(dest.GetResource(), 0, numSubresources);

	CommandContext& initContext = CommandContext::Begin();

	// copy data to the intermediate upload heap and then schedule a copy from the upload heap to the default texture 
	DynAlloc mem = initContext.ReserveUploadMemory(uploadBufferSize);
	initContext.TransitionResource(dest, D3D12_RESOURCE_STATE_COPY_DEST, true);
	UpdateSubresources(initContext.m_CommandList, dest.GetResource(), mem.buffer.GetResource(), 0, 0, numSubresources, subData);
	initContext.TransitionResource(dest, D3D12_RESOURCE_STATE_GENERIC_READ);

	// execute the command list and wait for it to finish so we can release the upload buffer
	initContext.Finish(true);
}

void CommandContext::InitializeBuffer(GpuBuffer& dest, const void* data, size_t numBytes, size_t offset)
{
	CommandContext& initContext = CommandContext::Begin();

	DynAlloc mem = initContext.ReserveUploadMemory(numBytes);
	SIMDMemCopy(mem.dataPtr, data, Math::DivideByMultiple(numBytes, 16));	// 需要16字节对齐
	// memcpy(mem.dataPtr, data, numBytes);

	// copy data to the intermediate upload heap and then schedule a copy from the upload heap to the default buffer
	initContext.TransitionResource(dest, D3D12_RESOURCE_STATE_COPY_DEST, true);
	initContext.m_CommandList->CopyBufferRegion(dest.GetResource(), offset, mem.buffer.GetResource(), 0, numBytes);
	initContext.TransitionResource(dest, D3D12_RESOURCE_STATE_GENERIC_READ, true);

	// execute the command list and wait for it to finish so we can release the upload buffer
	initContext.Finish(true);
}

void CommandContext::InitializeBuffer(GpuBuffer& dest, const UploadBuffer& src, size_t srcOffset, size_t numBytes, size_t destOffset)
{
	CommandContext &initContext = CommandContext::Begin();

	size_t maxBytes = std::min<size_t>(dest.GetBufferSize() - destOffset, src.GetBufferSize() - srcOffset);
	numBytes = std::min<size_t>(maxBytes, numBytes);

	// copy data to the intermediate upload heap and then schedule a copy from the upload heap to the default texture
	initContext.TransitionResource(dest, D3D12_RESOURCE_STATE_COPY_DEST, true);
	initContext.m_CommandList->CopyBufferRegion(dest.GetResource(), destOffset, (ID3D12Resource*)src.GetResource(), srcOffset, numBytes);
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

	// full subresource index
	// MipSlice + (ArraySlice * MipLevels) + (PlaneSlice * MipLevels * ArraySize);
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
	DynAlloc tempSpace = m_CpuLinearAllocator.Allocate(numBytes, 512);
	SIMDMemCopy(tempSpace.dataPtr, data, Math::DivideByMultiple(numBytes, 16));
	CopyBufferRegion(dest, destOffset, tempSpace.buffer, tempSpace.offset, numBytes);
}

void CommandContext::FillBuffer(GpuResource& dest, size_t destOffset, DWParam value, size_t numBytes)
{
	DynAlloc tempSpace = m_CpuLinearAllocator.Allocate(numBytes, 512);
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

		resource.m_UsageState = newState;
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

//  all UAV accesses (reads or writes ) must complete before any future UAV access (read or write) can begin
void CommandContext::InsertUAVBarrier(GpuResource& resource, bool flushImmediate)
{
	ASSERT(m_NumBarriersToFlush < 16, "Exceeded arbitrary limit on buffered barriers");
	D3D12_RESOURCE_BARRIER &barrierDesc = m_ResourceBarrierBuffer[m_NumBarriersToFlush++];

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

/**
	能够同时设置
	D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV，
	D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER，
	2种
	一般很少设置D3D12_DESCRIPTOR_HEAP_TYPE_RTV和D3D12_DESCRIPTOR_HEAP_TYPE_DSV（大概），-2020-2-7

	Ref: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setdescriptorheaps
	You can only bind descriptor heaps of type D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV and D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLES.
	Only one descriptor heap of each type can be set at one time, which means a maximum of 2 heaps (one sampler, one CBV/SRV/UAV) can 
	be set at one time.
*/
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

	if (nonNullHeaps > 0)
		m_CommandList->SetDescriptorHeaps(nonNullHeaps, heapsToBind);
}

// GraphicsContext
void GraphicsContext::ClearUAV(GpuBuffer& target)
{
	FlushResourceBarriers();

	// after binding a UAV, we can get a GPU handle that is required to clear it as a UAV (because it essentially
	// runs a shader to set all of the values).
	D3D12_GPU_DESCRIPTOR_HANDLE gpuVisibleHandle = m_DynamicViewDescriptorHeap.UploadDirect(target.GetUAV());
	const UINT clearColor[4] = {};
	m_CommandList->ClearUnorderedAccessViewUint(gpuVisibleHandle, target.GetUAV(), target.GetResource(),
		clearColor, 0, nullptr);
}

void GraphicsContext::ClearUAV(ColorBuffer& target)
{
	FlushResourceBarriers();

	// After binding a UAV, we can get a GPU handle that is required to clear it as a UAV (because it essentially runs
	// a shader to set all of the values).
	D3D12_GPU_DESCRIPTOR_HANDLE gpuVisibleHandle = m_DynamicViewDescriptorHeap.UploadDirect(target.GetUAV());
	CD3DX12_RECT clearRect(0, 0, (LONG)target.GetWidth(), (LONG)target.GetHeight());

	const float* clearColor = target.GetClearColor().GetPtr();
	m_CommandList->ClearUnorderedAccessViewFloat(gpuVisibleHandle, target.GetUAV(), target.GetResource(),
		clearColor, 1, &clearRect);
}

void GraphicsContext::ClearColor(ColorBuffer& target, D3D12_RECT* rect)
{
	FlushResourceBarriers();
	m_CommandList->ClearRenderTargetView(target.GetRTV(), target.GetClearColor().GetPtr(), (rect == nullptr) ? 0 : 1, rect);
}

void GraphicsContext::ClearColor(ColorBuffer& target, float color[4], D3D12_RECT* rect)
{
	FlushResourceBarriers();
	m_CommandList->ClearRenderTargetView(target.GetRTV(), color, (rect == nullptr) ? 0 : 1, rect);
}

void GraphicsContext::ClearDepth(DepthBuffer& target)
{
	FlushResourceBarriers();
	m_CommandList->ClearDepthStencilView(target.GetDSV(), D3D12_CLEAR_FLAG_DEPTH, 
		target.GetClearDepth(), target.GetClearStencil(), 0, nullptr);
}

void GraphicsContext::ClearStencil(DepthBuffer& target)
{
	FlushResourceBarriers();
	m_CommandList->ClearDepthStencilView(target.GetDSV(), D3D12_CLEAR_FLAG_STENCIL,
		target.GetClearDepth(), target.GetClearStencil(), 0, nullptr);
}

void GraphicsContext::ClearDepthAndStencil(DepthBuffer& target)
{
	FlushResourceBarriers();
	m_CommandList->ClearDepthStencilView(target.GetDSV(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		target.GetClearDepth(), target.GetClearStencil(), 0, nullptr);
}

void GraphicsContext::BeginQuery(ID3D12QueryHeap* pQueryHeap, D3D12_QUERY_TYPE type, UINT heapIndex)
{
	m_CommandList->BeginQuery(pQueryHeap, type, heapIndex);
}

void GraphicsContext::EndQuery(ID3D12QueryHeap* pQueryHeap, D3D12_QUERY_TYPE type, UINT heapIndex)
{
	m_CommandList->EndQuery(pQueryHeap, type, heapIndex);
}

void GraphicsContext::ResolveQueryData(ID3D12QueryHeap* pQueryHeap, D3D12_QUERY_TYPE type,
	UINT startIndex, UINT numQueries, ID3D12Resource* pDestBuffer, UINT64 destBufferOffset)
{
	m_CommandList->ResolveQueryData(pQueryHeap, type, startIndex, numQueries, pDestBuffer, destBufferOffset);
}

void GraphicsContext::SetRootSignature(const RootSignature& rootSig, bool bParseSignature)
{
	if (rootSig.GetSignature() == m_CurGraphicsRootSignature)
		return;

	m_CommandList->SetGraphicsRootSignature(m_CurGraphicsRootSignature = rootSig.GetSignature());

	// Sometimes i know i don't use DynamicDescriptorHeap, so no need to parse them!
	if (bParseSignature)
	{
		m_DynamicViewDescriptorHeap.ParseGraphicsRootSignature(rootSig);
		m_DynamicSamplerDescriptorHeap.ParseGraphicsRootSignature(rootSig);
	}
}

void GraphicsContext::SetRenderTargets(UINT numRTVs, const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[])
{
	m_CommandList->OMSetRenderTargets(numRTVs, rtvs, FALSE, nullptr);
}

void GraphicsContext::SetRenderTargets(UINT numRTVs, const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[], D3D12_CPU_DESCRIPTOR_HANDLE dsv)
{
	m_CommandList->OMSetRenderTargets(numRTVs, rtvs, FALSE, &dsv);
}

// 
void GraphicsContext::SetViewport(const D3D12_VIEWPORT& vp)
{
	m_CommandList->RSSetViewports(1, &vp);
}

void GraphicsContext::SetViewport(FLOAT x, FLOAT y, FLOAT w, FLOAT h, FLOAT minDepth, FLOAT maxDepth)
{
	D3D12_VIEWPORT vp;
	vp.Width = w;
	vp.Height = h;
	vp.TopLeftX = x;
	vp.TopLeftY = y;
	vp.MinDepth = minDepth;
	vp.MaxDepth = maxDepth;

	m_CommandList->RSSetViewports(1, &vp);
}

void GraphicsContext::SetScissor(const D3D12_RECT& rect)
{
	ASSERT(rect.left < rect.right && rect.top < rect.bottom);
	m_CommandList->RSSetScissorRects(1, &rect);
}

void GraphicsContext::SetScissor(UINT left, UINT top, UINT right, UINT bottom)
{
	SetScissor(CD3DX12_RECT(left, top, right, bottom));
}

void GraphicsContext::SetViewportAndScissor(const D3D12_VIEWPORT& vp, const D3D12_RECT& rect)
{
	ASSERT(rect.left < rect.right && rect.top < rect.bottom);
	m_CommandList->RSSetViewports(1, &vp);
	m_CommandList->RSSetScissorRects(1, &rect);
}

void GraphicsContext::SetViewportAndScissor(UINT x, UINT y, UINT w, UINT h)
{
	SetViewport((float)x, (float)y, (float)w, (float)h);
	SetScissor(x, y, x + w, y + h);
}

void GraphicsContext::SetStencilRef(UINT stencilRef)
{
	m_CommandList->OMSetStencilRef(stencilRef);
}

void GraphicsContext::SetBlendFactor(Color blendFactor)
{
	m_CommandList->OMSetBlendFactor(blendFactor.GetPtr());
}

void GraphicsContext::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology)
{
	m_CommandList->IASetPrimitiveTopology(topology);
}

void GraphicsContext::SetConstantArray(UINT rootIndex, UINT numConstants, const void* pConstants)
{
	m_CommandList->SetGraphicsRoot32BitConstants(rootIndex, numConstants, pConstants, 0);
}

void GraphicsContext::SetConstant(UINT rootIndex, DWParam val, UINT offset)
{
	m_CommandList->SetGraphicsRoot32BitConstant(rootIndex, val.Uint, offset);
}

void GraphicsContext::SetConstants(UINT rootIndex, DWParam X)
{
	m_CommandList->SetGraphicsRoot32BitConstant(rootIndex, X.Uint, 0);
}

void GraphicsContext::SetConstants(UINT rootIndex, DWParam X, DWParam Y)
{
	m_CommandList->SetGraphicsRoot32BitConstant(rootIndex, X.Uint, 0);
	m_CommandList->SetGraphicsRoot32BitConstant(rootIndex, Y.Uint, 1);
}

void GraphicsContext::SetConstants(UINT rootIndex, DWParam X, DWParam Y, DWParam Z)
{
	m_CommandList->SetGraphicsRoot32BitConstant(rootIndex, X.Uint, 0);
	m_CommandList->SetGraphicsRoot32BitConstant(rootIndex, Y.Uint, 1);
	m_CommandList->SetGraphicsRoot32BitConstant(rootIndex, Z.Uint, 2);
}

void GraphicsContext::SetConstants(UINT rootIndex, DWParam X, DWParam Y, DWParam Z, DWParam W)
{
	m_CommandList->SetGraphicsRoot32BitConstant(rootIndex, X.Uint, 0);
	m_CommandList->SetGraphicsRoot32BitConstant(rootIndex, Y.Uint, 1);
	m_CommandList->SetGraphicsRoot32BitConstant(rootIndex, Z.Uint, 2);
	m_CommandList->SetGraphicsRoot32BitConstant(rootIndex, W.Uint, 3);
}

void GraphicsContext::SetConstants(UINT rootIndex, UINT num32BitValuesToSet, const void* pSrcData, UINT destOffsetIn32BitValues)
{
	m_CommandList->SetGraphicsRoot32BitConstants(rootIndex, num32BitValuesToSet, pSrcData, destOffsetIn32BitValues);
}

void GraphicsContext::SetConstantBuffer(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS cbv)
{
	m_CommandList->SetGraphicsRootConstantBufferView(rootIndex, cbv);
}

void GraphicsContext::SetDynamicConstantBufferView(UINT rootIndex, size_t bufferSize, const void* bufferData)
{
	ASSERT(bufferData != nullptr && Math::IsAligned(bufferData, 16));
	DynAlloc cb = m_CpuLinearAllocator.Allocate(bufferSize);
	//SIMDMemCopy(cb.dataPtr, bufferData, Math::AlignUp(bufferSize, 16) >> 4);
	memcpy(cb.dataPtr, bufferData, bufferSize);
	m_CommandList->SetGraphicsRootConstantBufferView(rootIndex, cb.GpuAddress);
}

void GraphicsContext::SetBufferSRV(UINT rootIndex, const GpuBuffer& srv, UINT64 offset)
{
	ASSERT((srv.m_UsageState & (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) != 0);
	m_CommandList->SetGraphicsRootShaderResourceView(rootIndex, srv.GetGpuVirtualAddress() + offset);
}

void GraphicsContext::SetShaderResourceView(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS srv)
{
	m_CommandList->SetGraphicsRootShaderResourceView(rootIndex, srv);
}

void GraphicsContext::SetBufferUAV(UINT rootIndex, const GpuBuffer& uav, UINT64 offset)
{
	ASSERT((uav.m_UsageState & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0);
	m_CommandList->SetGraphicsRootUnorderedAccessView(rootIndex, uav.GetGpuVirtualAddress() + offset);
}

void GraphicsContext::SetUnorderedAccessView(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS uav)
{
	m_CommandList->SetGraphicsRootUnorderedAccessView(rootIndex, uav);
}

void GraphicsContext::SetDescriptorTable(UINT rootIndex, D3D12_GPU_DESCRIPTOR_HANDLE firstHandle)
{
	m_CommandList->SetGraphicsRootDescriptorTable(rootIndex, firstHandle);
}

void GraphicsContext::SetDynamicDescriptor(UINT rootIndex, UINT offset, D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
	SetDynamicDescriptors(rootIndex, offset, 1, &handle);
}

void GraphicsContext::SetDynamicDescriptors(UINT rootIndex, UINT offset, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE handles[])
{
	m_DynamicViewDescriptorHeap.SetGraphicsDescriptorHandles(rootIndex, offset, count, handles);
}

void GraphicsContext::SetDynamicSampler(UINT rootIndex, UINT offset, D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
	SetDynamicSamplers(rootIndex, offset, 1, &handle);
}

void GraphicsContext::SetDynamicSamplers(UINT rootIndex, UINT offset, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE handles[])
{
	m_DynamicSamplerDescriptorHeap.SetGraphicsDescriptorHandles(rootIndex, offset, count, handles);
}

void GraphicsContext::SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW& ibv)
{
	m_CommandList->IASetIndexBuffer(&ibv);
}

void GraphicsContext::SetVertexBuffer(UINT slot, const D3D12_VERTEX_BUFFER_VIEW& vbv)
{
	SetVertexBuffers(slot, 1, &vbv);
}

void GraphicsContext::SetVertexBuffers(UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW vbvs[])
{
	m_CommandList->IASetVertexBuffers(startSlot, count, vbvs);
}

void GraphicsContext::SetDynamicVB(UINT slot, size_t numVertices, size_t vertexStride, const void* VBdata)
{
	ASSERT(VBdata != nullptr && Math::IsAligned(VBdata, 16));

	size_t bufferSize = Math::AlignUp(numVertices * vertexStride, 16);
	DynAlloc vb = m_CpuLinearAllocator.Allocate(bufferSize);

	SIMDMemCopy(vb.dataPtr, VBdata, bufferSize >> 4);

	D3D12_VERTEX_BUFFER_VIEW vbv;
	vbv.BufferLocation = vb.GpuAddress;
	vbv.SizeInBytes = (UINT)bufferSize;
	vbv.StrideInBytes = (UINT)vertexStride;

	m_CommandList->IASetVertexBuffers(slot, 1, &vbv);
}

void GraphicsContext::SetDynamicIB(size_t indexCount, const uint16_t* IBdata)
{
	ASSERT(IBdata != nullptr && Math::IsAligned(IBdata, 16));

	size_t bufferSize = Math::AlignUp(indexCount * sizeof(uint16_t), 16);
	DynAlloc ib = m_CpuLinearAllocator.Allocate(bufferSize);

	SIMDMemCopy(ib.dataPtr, IBdata, bufferSize >> 4);

	D3D12_INDEX_BUFFER_VIEW ibv;
	ibv.BufferLocation = ib.GpuAddress;
	ibv.SizeInBytes = (UINT)(indexCount * sizeof(uint16_t));
	ibv.Format = DXGI_FORMAT_R16_UINT;

	m_CommandList->IASetIndexBuffer(&ibv);
}

void GraphicsContext::SetDynamicSRV(UINT rootIndex, size_t bufferSize, const void* bufferData)
{
	ASSERT(bufferData != nullptr && Math::IsAligned(bufferData, 16));
	DynAlloc cb = m_CpuLinearAllocator.Allocate(bufferSize);

	SIMDMemCopy(cb.dataPtr, bufferData, Math::AlignUp(bufferSize, 16) >> 4);
	m_CommandList->SetGraphicsRootShaderResourceView(rootIndex, cb.GpuAddress);
}

// Draw
void GraphicsContext::Draw(UINT vertexCount, UINT vertexStartOffset)
{
	DrawInstanced(vertexCount, 1, vertexStartOffset, 0);
}

void GraphicsContext::DrawIndexed(UINT indexCount, UINT startIndexLocation, INT baseVertexLocation)
{
	DrawIndexedInstanced(indexCount, 1, startIndexLocation, baseVertexLocation, 0);
}

void GraphicsContext::DrawInstanced(UINT vertexCountPerInstance, UINT instanceCount, UINT startVertexLocation, UINT startInstanceLocation)
{
	FlushResourceBarriers();
	m_DynamicViewDescriptorHeap.CommitGraphicsRootDescriptorTables(m_CommandList);
	m_DynamicSamplerDescriptorHeap.CommitGraphicsRootDescriptorTables(m_CommandList);
	m_CommandList->DrawInstanced(vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
}

void GraphicsContext::DrawIndexedInstanced(UINT indexCountPerInstance, UINT instanceCount, UINT startIndexLocation, INT baseVertexLocation, UINT startInstanceLocation)
{
	FlushResourceBarriers();
	m_DynamicViewDescriptorHeap.CommitGraphicsRootDescriptorTables(m_CommandList);
	m_DynamicSamplerDescriptorHeap.CommitGraphicsRootDescriptorTables(m_CommandList);
	m_CommandList->DrawIndexedInstanced(indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
}

void GraphicsContext::DrawIndirect(GpuBuffer& argumentBuffer, uint64_t argumentBufferOffset)
{
	ExecuteIndirect(Graphics::s_CommonStates.DrawIndirectCommandSignature, argumentBuffer, argumentBufferOffset);
}

void GraphicsContext::ExecuteIndirect(CommandSignature& commandSig, GpuBuffer& argumentBuffer,
	uint64_t argumentStartOffset, uint32_t maxCommands,
	GpuBuffer* commandCounterBuffer, uint64_t counterOffset)
{
	FlushResourceBarriers();
	m_DynamicViewDescriptorHeap.CommitGraphicsRootDescriptorTables(m_CommandList);
	m_DynamicSamplerDescriptorHeap.CommitGraphicsRootDescriptorTables(m_CommandList);
	m_CommandList->ExecuteIndirect(commandSig.GetSignature(), maxCommands, 
		argumentBuffer.GetResource(), argumentStartOffset,
		commandCounterBuffer == nullptr ? nullptr : commandCounterBuffer->GetResource(), counterOffset);
}

// ComputeContext
void ComputeContext::ClearUAV(GpuBuffer& target)
{
	FlushResourceBarriers();

	// after binding a UAV, we can get a GPU handle that is required to clear it as a UAV (because it essentially runs
	// a shader to set all of the values)
	D3D12_GPU_DESCRIPTOR_HANDLE gpuVisibleHandle = m_DynamicViewDescriptorHeap.UploadDirect(target.GetUAV());
	const UINT clearColor[4] = {};
	m_CommandList->ClearUnorderedAccessViewUint(gpuVisibleHandle, target.GetUAV(), target.GetResource(),
		clearColor, 0, nullptr);
}

void ComputeContext::ClearUAV(ColorBuffer& target)
{
	FlushResourceBarriers();

	// After binding a UAV, we can get a GPU handle that is required to clear it as a UAV (because it essentially runs
	// a shader to set all of the values).
	D3D12_GPU_DESCRIPTOR_HANDLE gpuVisibleHandle = m_DynamicViewDescriptorHeap.UploadDirect(target.GetUAV());
	CD3DX12_RECT clearRect(0, 0, (LONG)target.GetWidth(), (LONG)target.GetHeight());

	const float* clearColor = target.GetClearColor().GetPtr();
	m_CommandList->ClearUnorderedAccessViewFloat(gpuVisibleHandle, target.GetUAV(), target.GetResource(), clearColor, 1, &clearRect);
}

void ComputeContext::SetRootSignature(const RootSignature& rootSig, bool bParseSignature)
{
	if (m_CurComputeRootSignature == rootSig.GetSignature())
		return;

	m_CommandList->SetComputeRootSignature(m_CurComputeRootSignature = rootSig.GetSignature());

	if (bParseSignature)
	{
		m_DynamicViewDescriptorHeap.ParseComputeRootSignature(rootSig);
		m_DynamicSamplerDescriptorHeap.ParseComputeRootSignature(rootSig);
	}
}

void ComputeContext::SetConstantArray(UINT rootIndex, UINT numConstants, const void* pConstants)
{
	m_CommandList->SetComputeRoot32BitConstants(rootIndex, numConstants, pConstants, 0);
}

void ComputeContext::SetConstant(UINT rootIndex, DWParam val, UINT offset)
{
	m_CommandList->SetComputeRoot32BitConstant(rootIndex, val.Uint, offset);
}

void ComputeContext::SetConstants(UINT rootIndex, DWParam X)
{
	m_CommandList->SetComputeRoot32BitConstant(rootIndex, X.Uint, 0);
}

void ComputeContext::SetConstants(UINT rootIndex, DWParam X, DWParam Y)
{
	m_CommandList->SetComputeRoot32BitConstant(rootIndex, X.Uint, 0);
	m_CommandList->SetComputeRoot32BitConstant(rootIndex, Y.Uint, 1);
}

void ComputeContext::SetConstants(UINT rootIndex, DWParam X, DWParam Y, DWParam Z)
{
	m_CommandList->SetComputeRoot32BitConstant(rootIndex, X.Uint, 0);
	m_CommandList->SetComputeRoot32BitConstant(rootIndex, Y.Uint, 1);
	m_CommandList->SetComputeRoot32BitConstant(rootIndex, Z.Uint, 2);
}

void ComputeContext::SetConstants(UINT rootIndex, DWParam X, DWParam Y, DWParam Z, DWParam W)
{
	m_CommandList->SetComputeRoot32BitConstant(rootIndex, X.Uint, 0);
	m_CommandList->SetComputeRoot32BitConstant(rootIndex, Y.Uint, 1);
	m_CommandList->SetComputeRoot32BitConstant(rootIndex, Z.Uint, 2);
	m_CommandList->SetComputeRoot32BitConstant(rootIndex, W.Uint, 3);
}

void ComputeContext::SetConstants(UINT rootIndex, UINT num32BitValuesToSet, const void* pSrcData, UINT destOffsetIn32BitValues)
{
	m_CommandList->SetComputeRoot32BitConstants(rootIndex, num32BitValuesToSet, pSrcData, destOffsetIn32BitValues);
}

void ComputeContext::SetConstantBuffer(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS cbv)
{
	m_CommandList->SetComputeRootConstantBufferView(rootIndex, cbv);
}

void ComputeContext::SetDynamicConstantBufferView(UINT rootIndex, size_t bufferSize, const void* bufferData)
{
	ASSERT(bufferData != nullptr && Math::IsAligned(bufferData, 16));
	DynAlloc cb = m_CpuLinearAllocator.Allocate(bufferSize);
	//SIMDMemCopy(cb.dataPtr, bufferData, Math::AlignUp(bufferSize, 16) >> 4);
	memcpy(cb.dataPtr, bufferData, bufferSize);
	m_CommandList->SetComputeRootConstantBufferView(rootIndex, cb.GpuAddress);
}

void ComputeContext::SetDynamicSRV(UINT rootIndex, size_t bufferSize, const void* bufferData)
{
	ASSERT(bufferData != nullptr && Math::IsAligned(bufferData, 16));
	DynAlloc cb = m_CpuLinearAllocator.Allocate(bufferSize);
	SIMDMemCopy(cb.dataPtr, bufferData, Math::AlignUp(bufferSize, 16) >> 4);
	m_CommandList->SetComputeRootShaderResourceView(rootIndex, cb.GpuAddress);
}

void ComputeContext::SetBufferSRV(UINT rootIndex, const GpuBuffer& srv, UINT64 offset)
{
	ASSERT((srv.m_UsageState & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) != 0);
	m_CommandList->SetComputeRootShaderResourceView(rootIndex, srv.GetGpuVirtualAddress() + offset);
}

void ComputeContext::SetShaderResourceView(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS srv)
{
	m_CommandList->SetComputeRootShaderResourceView(rootIndex, srv);
}

void ComputeContext::SetBufferUAV(UINT rootIndex, const GpuBuffer& uav, UINT64 offset)
{
	ASSERT((uav.m_UsageState & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0);
	m_CommandList->SetComputeRootUnorderedAccessView(rootIndex, uav.GetGpuVirtualAddress() + offset);
}

void ComputeContext::SetUnorderedAccessView(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS uav)
{
	m_CommandList->SetComputeRootUnorderedAccessView(rootIndex, uav);
}

void ComputeContext::SetDescriptorTable(UINT rootIndex, D3D12_GPU_DESCRIPTOR_HANDLE firstHandle)
{
	m_CommandList->SetComputeRootDescriptorTable(rootIndex, firstHandle);
}

void ComputeContext::SetDynamicDescriptor(UINT rootIndex, UINT offset, D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
	SetDynamicDescriptors(rootIndex, offset, 1, &handle);
}

void ComputeContext::SetDynamicDescriptors(UINT rootIndex, UINT offset, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE handles[])
{
	m_DynamicViewDescriptorHeap.SetComputeDescriptorHandles(rootIndex, offset, count, handles);
}

void ComputeContext::SetDynamicSampler(UINT rootIndex, UINT offset, D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
	SetDynamicSamplers(rootIndex, offset, 1, &handle);
}

void ComputeContext::SetDynamicSamplers(UINT rootIndex, UINT offset, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE handles[])
{
	m_DynamicSamplerDescriptorHeap.SetComputeDescriptorHandles(rootIndex, offset, count, handles);
}

// Dispatch
void ComputeContext::Dispatch(size_t groupCountX, size_t groupCountY, size_t groupCountZ)
{
	FlushResourceBarriers();
	m_DynamicViewDescriptorHeap.CommitComputeRootDescriptorTables(m_CommandList);
	m_DynamicSamplerDescriptorHeap.CommitComputeRootDescriptorTables(m_CommandList);
	m_CommandList->Dispatch((UINT)groupCountX, (UINT)groupCountY, (UINT)groupCountZ);
}

void ComputeContext::Dispatch1D(size_t threadCountX, size_t groupSizeX)
{
	Dispatch(Math::DivideByMultiple(threadCountX, groupSizeX), 1, 1);
}

void ComputeContext::Dispatch2D(size_t threadCountX, size_t threadCountY, size_t groupSizeX, size_t groupSizeY)
{
	Dispatch(
		Math::DivideByMultiple(threadCountX, groupSizeX), 
		Math::DivideByMultiple(threadCountY, groupSizeY), 1);
}

void ComputeContext::Dispatch3D(size_t threadCountX, size_t threadCountY, size_t threadCountZ, size_t groupSizeX, size_t groupSizeY, size_t groupSizeZ)
{
	Dispatch(
		Math::DivideByMultiple(threadCountX, groupSizeX),
		Math::DivideByMultiple(threadCountY, groupSizeY), 
		Math::DivideByMultiple(threadCountZ, groupSizeZ));
}

void ComputeContext::DispatchIndirect(GpuBuffer& argumentBuffer, uint64_t argumentBufferOffset)
{
	ExecuteIndirect(Graphics::s_CommonStates.DispatchIndirectCommandSignature, argumentBuffer, argumentBufferOffset);
}

void ComputeContext::ExecuteIndirect(CommandSignature& commandSig, GpuBuffer& argumentBuffer, uint64_t argumentStartOffset, uint32_t maxCommands, GpuBuffer* commandCounterBuffer, uint64_t counterOffset)
{
	FlushResourceBarriers();
	m_DynamicViewDescriptorHeap.CommitComputeRootDescriptorTables(m_CommandList);
	m_DynamicSamplerDescriptorHeap.CommitComputeRootDescriptorTables(m_CommandList);
	m_CommandList->ExecuteIndirect(commandSig.GetSignature(), maxCommands,
		argumentBuffer.GetResource(), argumentStartOffset,
		commandCounterBuffer == nullptr ? nullptr : commandCounterBuffer->GetResource(), counterOffset);
}

/**
	https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-executeindirect
	ID3D12GraphicsCommandList::ExecuteIndirect
	apps perform indirect draws/dispatches using the ExecuteIndirect method
	void ExecuteIndirect(
		ID3D12CommandSignature *pCommandSignature,
		UINT					maxCommandCount,
		ID3D12Resource			*pArgumentBuffer,
		UINT64					ArgumentBufferOffset,
		ID3D12Resource			*pCounterBuffer,
		UINT64					CountBufferOffset
	);
	1. pCommandSignature - specifies a ID3D12CommandSignature. The data referenced by pArgumentBuffer will be
interpreted depending on the contents of the command signature.
	2. MaxCommandCount - there are 2 ways that command counts can be specified:
		@if pCounterBuffer is not NULL, the MaxCommandCount specifies the maximum number of operations which will
be performed. The actual number of operations to be performed are defined by the minimum of this value, and 
a 32-bit unsigned integer contained in pCountBuffer (at the byte offset specified by CountBufferOffset)
		@if pCountBuffer is NULL, the MaxCommandCount specifies the exact number of operations which will 
be performed.
	3. pArgumentBuffer - specifies one or more ID3D12Resource objects, containing the command arguments
	4. ArgumentBufferOffset - specifies an offset inot pArgumentBuffer to identify the first command argument
	5. pCountBuffer - specifies a pointer to a ID3D12Resource
	6. CountBufferOffset - specifies a UINT64 that is the offset into pCountBuffer, identifying the argument count

	Example：
		// read draw count out of count buffer
		UINT commandCount = pCountBuffer->ReadUINT32(CountBufferOffset);
		CommandCount = min(CommandCount, MaxCommandCount);

		// get pointer to first Commanding argument
		BYTE *Arguments = pArgumentBuffer->GetBase() + ArgumentBufferOffset;

		for (UINT CommandIndex = 0; CommandIndex < CommandCount; ++CommandIndex){
			// interpret the data contained in *Arguments according to the command signature
			pCommandSignature->Interpret(Arguments);
			Arguments += pCommandSignature->GetByteStride();
		}
*/

/**
	https://docs.microsoft.com/en-us/windows/win32/direct3d12/indirect-drawing
	Indirect Drawing
	indirect drawing enables some scene-traversal and culling to be moved from the CPU to the GPU, which can
improve performance. The command buffer can be generated by the CPU or GPU
	** Command Signatures
	** Indirect Argument Buffer Structures
	** Command Signature Creation (No Argument Changes, Root Constants and Vertex Buffers)

	>> Command Signatures
	the command signature object enables apps to specify indirect drawing, in particular setting to the following:
		1. the indirect argument buffer format
		2. the command type that will be used (from the ID3D12GraphicsCommandList methods, "DrawInstanced", 
"DrawIndexedInstanced" or "Dispatch")
		3. the set of resource bindings which will change per-command call versus the set which will be inherited
	
	at startup, an app creates a small set of command signatures. At runtime, the application fills a buffer with
commands. The commands optionally containing state to set for vertex buffer views, index buffer views, 
root constants and root descriptors (raw or structured SRV/UAV/CBVs). These argument layout are not hardware
specific so apps can generate the buffers directly.		Then the app calls ExecuteIndirect to instruct the GPU
to interpret the contents of the indirect argument buffer according to the format defined by a particular 
command signature.
	Note that no command signatures state leaks back to the command list when execution is complete.
	For example, suppose an app developer wants a unique root constant to be specified per-draw call inthe 
indirect argument buffer. The app would create a command signature that enables the indirect argument buffer
to specify the following parameters per draw call:
	1. the value of root constant
	2. the draw arguments (vertex count, instance count, etc)

	>> Indirec Argument Buffer Structures
	the following structures define how particular arguments appear in an indirect buffer. These structures
do not appear in any D3D12 API. Applications use these definitions when writing to an indirect argument buffer
	{
		D3D12_DRAW_ARGUMENTS, D3D12_DRAW_INDEXED_ARGUMENTS, D3D12_DISPATCH_ARGUMENTS,
		D3D12_VERTEX_BUFFER_VIEW, D3D12_INDEX_BUFFER_VIEW,
		D3D12_GPU_VIRTUAL_ADDRESS
		D3D12_CONSTANT_BUFFER_VIEW
	}

*/

/**
	https://docs.microsoft.com/en-us/windows/win32/direct3d12/subresources
	Subresources

	inline UINT D3D12CalcSubresource( UINT MipSlice, UINT ArraySlice, UINT PlaneSlice, UINT MipLevels, UINT ArraySize )
	{
		return MipSlice + (ArraySlice * MipLevels) + (PlaneSlice * MipLevels * ArraySize);
	}

	Textures must be in the D3D12_RESOURCE_STATE_COMMON state for CPU access through WriteToSubresource and 
ReadFromSubresource to be legal; but buffers do not. CPU access to a resource is typically done through Map/Unmap.
*/
