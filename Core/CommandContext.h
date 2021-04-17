#pragma once
#include "pch.h"
#include "Color.h"
#include "GpuBuffer.h"
#include "PixelBuffer.h"
#include "LinearAllocator.h"
#include "DynamicDescriptorHeap.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include <queue>
#include <mutex>

namespace MyDirectX
{
	class CommandSignature;

	class CommandListManager;

	class GraphicsContext;
	class ComputeContext;

	class ColorBuffer;
	class DepthBuffer;
	class UploadBuffer;
	class ReadbackBuffer;

	struct DWParam
	{
		DWParam(FLOAT f) : Float(f) {}
		DWParam(UINT u) : Uint(u) {}
		DWParam(INT i) : Int(i) {}

		void operator= (FLOAT f) { Float = f; }
		void operator= (UINT u) { Uint = u; }
		void operator= (INT i) { Int = i; }

		union
		{
			FLOAT Float;
			UINT Uint;
			INT Int;
		};
		
	};

#define VALID_COMPUTE_QUEUE_RESOURCE_STATE \
	( D3D12_RESOURCE_STATE_UNORDERED_ACCESS \
	| D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE \
	| D3D12_RESOURCE_STATE_COPY_DEST \
	| D3D12_RESOURCE_STATE_COPY_SOURCE)

	/// CommandContext 管理器，分配/销毁 CommandContext，全局唯一
	class ContextManager
	{
	public:
		ContextManager() {}

		CommandContext* AllocateContext(D3D12_COMMAND_LIST_TYPE type);
		void FreeContext(CommandContext*);
		void DestroyAllContexts();

	private:
		std::vector<std::unique_ptr<CommandContext>> m_ContextPool[4];
		std::queue<CommandContext*> m_AvailableContexts[4];
		std::mutex m_ContextAllocationMutex;
	};

	// 不可复制
	struct NonCopyable
	{
		NonCopyable() = default;
		NonCopyable(const NonCopyable&) = delete;
		NonCopyable& operator=(const NonCopyable&) = delete;
	};

	class CommandContext : public NonCopyable
	{
		friend ContextManager;

	private:
		CommandContext(D3D12_COMMAND_LIST_TYPE type);

		void Reset();

	public:
		~CommandContext();

		static void DestroyAllContexts();

		static CommandContext& Begin(const std::wstring& ID = L"");

		// flush existing commands to the GPU but keep the context alive
		uint64_t Flush(bool bWaitForCompletion = false);

		// flush existing commands and release the current context
		uint64_t Finish(bool bWaitForCompletion = false);

		// prepare to render by reserving a command list and command allocator
		void Initialize(ID3D12Device *pDevice);

		GraphicsContext& GetGraphicsContext()
		{
			ASSERT(m_Type != D3D12_COMMAND_LIST_TYPE_COMPUTE, "Cannot convert async compute context to graphics");
			return reinterpret_cast<GraphicsContext&>(*this);
		}

		ComputeContext& GetComputeContext()
		{
			return reinterpret_cast<ComputeContext&>(*this);
		}

		ID3D12GraphicsCommandList* GetCommandList()
		{
			return m_CommandList;
		}

		void CopyBuffer(GpuResource& dest, GpuResource& src);
		void CopyBufferRegion(GpuResource& dest, size_t destOffset, GpuResource& src, size_t srcOffset, size_t numBytes);
		void CopySubresource(GpuResource& dest, UINT destSubIndex, GpuResource& src, UINT srcSubIndex);
		void CopyCounter(GpuResource& dest, size_t destOffset, StructuredBuffer& src);
		void CopyTextureRegion(GpuResource &dest, UINT x, UINT y, UINT z, GpuResource &source, RECT &rect);
		void ResetCounter(StructuredBuffer& buf, uint32_t value = 0);

		// creates a readback buffer of sufficient size, copies the texture into it,
		// and returns row pitch in bytes
		uint32_t ReadbackTexture(ID3D12Device* pDevice, ReadbackBuffer &dstBuffer, PixelBuffer &srcBuffer);

		DynAlloc ReserveUploadMemory(size_t sizeInBytes)
		{
			return m_CpuLinearAllocator.Allocate(sizeInBytes);
		}

		static void InitializeTexture(GpuResource& dest, UINT numSubresources, D3D12_SUBRESOURCE_DATA subData[]);
		static void InitializeBuffer(GpuBuffer& dest, const void* data, size_t numBytes, size_t offset = 0);
		static void InitializeBuffer(GpuBuffer& dest, const UploadBuffer &src, size_t srcOffset, size_t numBytes = -1, size_t destOffset = 0);
		static void InitializeTextureArraySlice(GpuResource& dest, UINT sliceIndex, GpuResource& src);
		static void ReadbackTexture2D(GpuResource& readbackBuffer, PixelBuffer& srcBuffer);

		void WriteBuffer(GpuResource& dest, size_t destOffset, const void* data, size_t numBytes);
		void FillBuffer(GpuResource& dest, size_t destOffset, DWParam value, size_t numBytes);

		void TransitionResource(GpuResource& resource, D3D12_RESOURCE_STATES newState, bool flushImmediate = false);
		void BeginResourceTransition(GpuResource& resource, D3D12_RESOURCE_STATES newState, bool flushImmediate = false);
		void InsertUAVBarrier(GpuResource& resource, bool flushImmediate = false);
		void InsertAliasBarrier(GpuResource& before, GpuResource& after, bool flushImmediate = false);
		inline void FlushResourceBarriers();

		void InsertTimeStamp(ID3D12QueryHeap* pQueryHeap, uint32_t queryIdx);
		void ResolveTimeStamp(ID3D12Resource* pReadbackHeap, ID3D12QueryHeap* pQueryHeap, uint32_t numQueries);
		void PIXBeginEvent(const wchar_t* label);
		void PIXEndEvent();
		void PIXSetMarker(const wchar_t* label);

		void SetPipelineState(const PSO& pso);
		void SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, ID3D12DescriptorHeap* pHeap);
		void SetDescriptorHeaps(UINT heapCount, D3D12_DESCRIPTOR_HEAP_TYPE type[], ID3D12DescriptorHeap* pHeaps[]);

		void SetPredication(ID3D12Resource* buffer, UINT64 bufferOffset, D3D12_PREDICATION_OP op);

	protected:
		void BindDescriptorHeaps();

		ID3D12Device* m_Device;

		D3D12_COMMAND_LIST_TYPE m_Type;

		CommandListManager* m_CommandManager;
		ID3D12GraphicsCommandList* m_CommandList;
		ID3D12CommandAllocator* m_CurAllocator;

		ID3D12RootSignature* m_CurGraphicsRootSignature;
		ID3D12RootSignature* m_CurComputeRootSignature;
		ID3D12PipelineState* m_CurPipelineState;

		DynamicDescriptorHeap m_DynamicViewDescriptorHeap;		// HEAP_TYPE_CBV_SRV_UAV
		DynamicDescriptorHeap m_DynamicSamplerDescriptorHeap;	// HEAP_TYPE_SAMPLER

		D3D12_RESOURCE_BARRIER m_ResourceBarrierBuffer[16];
		UINT m_NumBarriersToFlush;

		ID3D12DescriptorHeap* m_CurDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];
		
		LinearAllocator m_CpuLinearAllocator;
		LinearAllocator m_GpuLinearAllocator;

		std::wstring m_ID;
		void SetID(const std::wstring& ID) { m_ID = ID; }
	};

	/// Graphics Context
	class GraphicsContext : public CommandContext
	{
	public:
		static GraphicsContext& Begin(const std::wstring& ID = L"")
		{
			return CommandContext::Begin(ID).GetGraphicsContext();
		}

		void ClearUAV(GpuBuffer& target);
		void ClearUAV(ColorBuffer& target);
		void ClearColor(ColorBuffer& target, D3D12_RECT *rect = nullptr);
		void ClearColor(ColorBuffer &target, float color[4], D3D12_RECT *rect = nullptr);
		void ClearDepth(DepthBuffer& target);
		void ClearStencil(DepthBuffer& target);
		void ClearDepthAndStencil(DepthBuffer& target);

		void BeginQuery(ID3D12QueryHeap* pQueryHeap, D3D12_QUERY_TYPE type, UINT heapIndex);
		void EndQuery(ID3D12QueryHeap* pQueryHeap, D3D12_QUERY_TYPE type, UINT heapIndex);
		void ResolveQueryData(ID3D12QueryHeap* pQueryHeap, D3D12_QUERY_TYPE type, UINT startIndex, UINT numQueries,
			ID3D12Resource* pDestBuffer, UINT64 destBufferOffset);

		void SetRootSignature(const RootSignature& rootSig);

		void SetRenderTargets(UINT numRTVs, const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[]);
		void SetRenderTargets(UINT numRTVs, const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[], D3D12_CPU_DESCRIPTOR_HANDLE dsv);
		void SetRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv) { SetRenderTargets(1, &rtv); }
		void SetRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv) { SetRenderTargets(1, &rtv, dsv); }
		void SetDepthStencilTarget(D3D12_CPU_DESCRIPTOR_HANDLE dsv) { SetRenderTargets(0, nullptr, dsv); }

		void SetViewport(const D3D12_VIEWPORT& vp);
		void SetViewport(FLOAT x, FLOAT y, FLOAT w, FLOAT h, FLOAT minDepth = 0.0f, FLOAT maxDepth = 1.0f);
		void SetScissor(const D3D12_RECT& rect);
		void SetScissor(UINT left, UINT top, UINT right, UINT bottom);
		void SetViewportAndScissor(const D3D12_VIEWPORT& vp, const D3D12_RECT& rect);
		void SetViewportAndScissor(UINT x, UINT y, UINT w, UINT h);
		void SetStencilRef(UINT stencilRef);
		void SetBlendFactor(Color blendFactor);
		void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology);

		void SetConstantArray(UINT rootIndex, UINT numConstants, const void* pConstants);
		void SetConstant(UINT rootIndex, DWParam val, UINT offset = 0);
		void SetConstants(UINT rootIndex, DWParam X);
		void SetConstants(UINT rootIndex, DWParam X, DWParam Y);
		void SetConstants(UINT rootIndex, DWParam X, DWParam Y, DWParam Z);
		void SetConstants(UINT rootIndex, DWParam X, DWParam Y, DWParam Z, DWParam W);
		void SetConstants(UINT rootIndex, UINT num32BitValuesToSet, const void* pSrcData, UINT destOffsetIn32BitValues = 0);
		void SetConstantBuffer(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS cbv);
		void SetDynamicConstantBufferView(UINT rootIndex, size_t bufferSize, const void* bufferData);
		void SetBufferSRV(UINT rootIndex, const GpuBuffer& srv, UINT64 offset = 0);
		void SetShaderResourceView(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS srv);
		void SetBufferUAV(UINT rootIndex, const GpuBuffer& uav, UINT64 offset = 0);
		void SetUnorderedAccessView(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS uav);
		void SetDescriptorTable(UINT rootIndex, D3D12_GPU_DESCRIPTOR_HANDLE firstHandle);

		void SetDynamicDescriptor(UINT rootIndex, UINT offset, D3D12_CPU_DESCRIPTOR_HANDLE handle);
		void SetDynamicDescriptors(UINT rootIndex, UINT offset, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE handles[]);
		void SetDynamicSampler(UINT rootIndex, UINT offset, D3D12_CPU_DESCRIPTOR_HANDLE handle);
		void SetDynamicSamplers(UINT rootIndex, UINT offset, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE handles[]);

		void SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW& ibv);
		void SetVertexBuffer(UINT slot, const D3D12_VERTEX_BUFFER_VIEW& vbv);
		void SetVertexBuffers(UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW vbvs[]);
		void SetDynamicVB(UINT slot, size_t numVertices, size_t vertexStride, const void* VBdata);
		void SetDynamicIB(size_t indexCount, const uint16_t* IBdata);
		void SetDynamicSRV(UINT rootIndex, size_t bufferSize, const void* bufferData);

		void Draw(UINT vertexCount, UINT vertexStartOffset = 0);
		void DrawIndexed(UINT indexCount, UINT startIndexLocation = 0, INT baseVertexLocation = 0);
		void DrawInstanced(UINT vertexCountPerInstance, UINT instanceCount, UINT startVertexLocation = 0, UINT startInstanceLocation = 0);
		void DrawIndexedInstanced(UINT indexCountPerInstance, UINT instanceCount, UINT startIndexLocation,
			INT baseVertexLocation, UINT startInstanceLocation);
		void DrawIndirect(GpuBuffer& argumentBuffer, uint64_t argumentBufferOffset = 0);
		void ExecuteIndirect(CommandSignature& commandSig, GpuBuffer& argumentBuffer, uint64_t argumentStartOffset = 0,
			uint32_t maxCommands = 1, GpuBuffer* commandCounterBuffer = nullptr, uint64_t counterOffset = 0);

	};

	/// Compute Context
	class ComputeContext : public CommandContext
	{
	public:
		static ComputeContext& Begin(const std::wstring& ID = L"", bool async = false);

		void ClearUAV(GpuBuffer& target);
		void ClearUAV(ColorBuffer& target);

		void SetRootSignature(const RootSignature& rootSig);

		void SetConstantArray(UINT rootIndex, UINT numConstants, const void* pConstants);
		void SetConstant(UINT rootIndex, DWParam val, UINT offset = 0);
		void SetConstants(UINT rootIndex, DWParam X);
		void SetConstants(UINT rootIndex, DWParam X, DWParam Y);
		void SetConstants(UINT rootIndex, DWParam X, DWParam Y, DWParam Z);
		void SetConstants(UINT rootIndex, DWParam X, DWParam Y, DWParam Z, DWParam W);
		void SetConstants(UINT rootIndex, UINT num32BitValuesToSet, const void *pSrcData, UINT destOffsetIn32BitValues = 0);
		void SetConstantBuffer(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS cbv);
		void SetDynamicConstantBufferView(UINT rootIndex, size_t bufferSize, const void* bufferData);
		void SetDynamicSRV(UINT rootIndex, size_t bufferSize, const void* bufferData);
		void SetBufferSRV(UINT rootIndex, const GpuBuffer& srv, UINT64 offset = 0);
		void SetShaderResourceView(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS srv);
		void SetBufferUAV(UINT rootIndex, const GpuBuffer& uav, UINT64 offset = 0);
		void SetUnorderedAccessView(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS uav);
		void SetDescriptorTable(UINT rootIndex, D3D12_GPU_DESCRIPTOR_HANDLE firstHandle);

		void SetDynamicDescriptor(UINT rootIndex, UINT offset, D3D12_CPU_DESCRIPTOR_HANDLE handle);
		void SetDynamicDescriptors(UINT rootIndex, UINT offset, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE handles[]);
		void SetDynamicSampler(UINT rootIndex, UINT offset, D3D12_CPU_DESCRIPTOR_HANDLE handle);
		void SetDynamicSamplers(UINT rootIndex, UINT offset, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE handles[]);

		void Dispatch(size_t groupCountX = 1, size_t groupCountY = 1, size_t groupCountZ = 1);
		void Dispatch1D(size_t threadCountX, size_t groupSizeX = 64);
		void Dispatch2D(size_t threadCountX, size_t threadCountY, size_t groupSizeX = 8, size_t groupSizeY = 8);
		void Dispatch3D(size_t threadCountX, size_t threadCountY, size_t threadCountZ, size_t groupSizeX, size_t groupSizeY, size_t groupSizeZ);
		void DispatchIndirect(GpuBuffer& argumentBuffer, uint64_t argumentBufferOffset = 0);
		void ExecuteIndirect(CommandSignature& commandSig, GpuBuffer& argumentBuffer, uint64_t argumentStartOffset = 0,
			uint32_t maxCommands = 1, GpuBuffer* commandCounterBuffer = nullptr, uint64_t counterOffset = 0);
	};

	/// inline functions
	inline void CommandContext::FlushResourceBarriers()
	{
		if (m_NumBarriersToFlush > 0)
		{
			m_CommandList->ResourceBarrier(m_NumBarriersToFlush, m_ResourceBarrierBuffer);
			m_NumBarriersToFlush = 0;
		}
	}

	inline void CommandContext::SetPipelineState(const PSO& pso)
	{
		ID3D12PipelineState* pipelineState = pso.GetPipelineStateObject();
		if (pipelineState == m_CurPipelineState)
			return;

		m_CommandList->SetPipelineState(pipelineState);
		m_CurPipelineState = pipelineState;
	}

	inline void CommandContext::SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, ID3D12DescriptorHeap* pDescHeap)
	{
		if (m_CurDescriptorHeaps[type] != pDescHeap)
		{
			m_CurDescriptorHeaps[type] = pDescHeap;
			BindDescriptorHeaps();
		}
	}

	inline void CommandContext::SetDescriptorHeaps(UINT heapCount, D3D12_DESCRIPTOR_HEAP_TYPE type[], ID3D12DescriptorHeap* pHeaps[])
	{
		bool anyChanged = false;
		
		for (UINT i = 0; i < heapCount; ++i)
		{
			if (m_CurDescriptorHeaps[type[i]] != pHeaps[i])
			{
				m_CurDescriptorHeaps[type[i]] = pHeaps[i];
				anyChanged = true;
			}
		}

		if (anyChanged)
			BindDescriptorHeaps();
	}

	inline void CommandContext::SetPredication(ID3D12Resource* buffer, UINT64 bufferOffset, D3D12_PREDICATION_OP op)
	{
		m_CommandList->SetPredication(buffer, bufferOffset, op);
	}

	inline void CommandContext::CopyBuffer(GpuResource& dest, GpuResource& src)
	{
		TransitionResource(dest, D3D12_RESOURCE_STATE_COPY_DEST);
		TransitionResource(src, D3D12_RESOURCE_STATE_COPY_SOURCE);
		FlushResourceBarriers();
		m_CommandList->CopyResource(dest.GetResource(), src.GetResource());
	}

	inline void CommandContext::CopyBufferRegion(GpuResource& dest, size_t destOffset, GpuResource& src, size_t srcOffset, size_t numBytes)
	{
		TransitionResource(dest, D3D12_RESOURCE_STATE_COPY_DEST);
		//TransitionResource(src, D3D12_RESOURCE_STATE_COPY_SOURCE);
		FlushResourceBarriers();
		m_CommandList->CopyBufferRegion(dest.GetResource(), destOffset, src.GetResource(), srcOffset, numBytes);
	}

	inline void CommandContext::CopyCounter(GpuResource& dest, size_t destOffset, StructuredBuffer& src)
	{
		TransitionResource(dest, D3D12_RESOURCE_STATE_COPY_DEST);
		TransitionResource(src.GetCounterBuffer(), D3D12_RESOURCE_STATE_COPY_SOURCE);
		FlushResourceBarriers();
		m_CommandList->CopyBufferRegion(dest.GetResource(), destOffset, src.GetCounterBuffer().GetResource(), 0, 4);
	}

	inline void CommandContext::CopyTextureRegion(GpuResource& dest, UINT x, UINT y, UINT z, GpuResource& source, RECT& rect)
	{
		TransitionResource(dest, D3D12_RESOURCE_STATE_COPY_DEST);
		TransitionResource(source, D3D12_RESOURCE_STATE_COPY_SOURCE);
		FlushResourceBarriers();

		D3D12_TEXTURE_COPY_LOCATION destLoc = CD3DX12_TEXTURE_COPY_LOCATION(dest.GetResource(), 0);
		D3D12_TEXTURE_COPY_LOCATION srcLoc = CD3DX12_TEXTURE_COPY_LOCATION(source.GetResource(), 0);

		D3D12_BOX box = {};
		box.back = 1;
		box.left = rect.left;
		box.right = rect.right;
		box.top = rect.top;
		box.bottom = rect.bottom;

		m_CommandList->CopyTextureRegion(&destLoc, x, y, z, &srcLoc, &box);
	}

	inline void CommandContext::ResetCounter(StructuredBuffer& buf, uint32_t value)
	{
		FillBuffer(buf.GetCounterBuffer(), 0, value, sizeof(uint32_t));
		TransitionResource(buf.GetCounterBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	}

	inline void CommandContext::InsertTimeStamp(ID3D12QueryHeap* pQueryHeap, uint32_t queryIdx)
	{
		m_CommandList->EndQuery(pQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, queryIdx);
	}

	// ID3D12QueryHeap *pQueryHeap - specifies the ID3D12QueryHeap containing the queries to resolve
	// D3D12_QUERY_TYPE - specifies the type of query, one member of D3D12_QUERY_TYPE
	// UINT startIndex
	// UINT numQueries - specifies the number of queries to resolve
	// ID3D12Resource* pDestinatinBuffer - specifies an ID3D12Resource destination buffer, which must be in the state
	//		D3D12_RESOURCE_STATE_COPY_DEST
	// UINT64 alignedDestinationBufferOffset - specifies an alignment offset into the destination buffer. Must be 
	//		a multiple of 8 bytes
	// ResolveQueryData performs a batched operation which writes query data into a destination buffer.
	inline void CommandContext::ResolveTimeStamp(ID3D12Resource* pReadbackHeap, ID3D12QueryHeap* pQueryHeap, uint32_t numQueries)
	{
		// extracts data from a query. ResolveQueryData works with all heap types (default, upload, and readback)
		m_CommandList->ResolveQueryData(pQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0, numQueries, pReadbackHeap, 0);
	}
}
