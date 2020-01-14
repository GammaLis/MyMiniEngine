#pragma once
#include "pch.h"
#include "DescriptorHeap.h"
#include <queue>
#include <mutex>

namespace MyDirectX
{
	class RootSignature;
	class CommandContext;

	// this class is a linear allocation system for dynamically generated tables. It internally caches 
	// CPU descriptor handles so that when not enough space is available in the current heap, necessary descriptors
	// can be re-copied to the new heap
	class DynamicDescriptorHeap
	{
	public:
		DynamicDescriptorHeap(CommandContext& context, D3D12_DESCRIPTOR_HEAP_TYPE type);
		~DynamicDescriptorHeap() {  }

		static void DestroyAll()
		{
			s_DescriptorHeapPool[0].clear();
			s_DescriptorHeapPool[1].clear();
		}

		void CleanupUsedHeaps(uint64_t fenceValue);

		// copy multiple handles into the cache area reserved for the specified root parameter
		void SetGraphicsDescriptorHandles(UINT rootIndex, UINT offset, UINT numHandles, const D3D12_CPU_DESCRIPTOR_HANDLE handles[])
		{
			m_GraphicsHandleCache.StageDescriptorHandles(rootIndex, offset, numHandles, handles);
		}

		void SetComputeDescriptorHandles(UINT rootIndex, UINT offset, UINT numHandles, const D3D12_CPU_DESCRIPTOR_HANDLE handles[])
		{
			m_ComputeHandleCache.StageDescriptorHandles(rootIndex, offset, numHandles, handles);
		}

		// bypass the cache and upload directly to the shader-visible heap
		D3D12_GPU_DESCRIPTOR_HANDLE UploadDirect(D3D12_CPU_DESCRIPTOR_HANDLE handle);

		// deduce cache layout needed to support the descriptor tables needed by the root signature
		void ParseGraphicsRootSignature(const RootSignature& rootSig)
		{
			m_GraphicsHandleCache.ParseRootSignature(m_DescriptorType, rootSig);
		}

		void ParseComputeRootSignature(const RootSignature& rootSig)
		{
			m_ComputeHandleCache.ParseRootSignature(m_DescriptorType, rootSig);
		}

		// upload any new descriptors in the cache to the shader-visible heap
		void CommitGraphicsRootDescriptorTables(ID3D12GraphicsCommandList* pCmdList)
		{
			if (m_GraphicsHandleCache.m_StaleRootParamsBitMap != 0)
			{
				CopyAndBindStagedTables(m_GraphicsHandleCache, pCmdList, &ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable);
			}
		}

		void CommitComputeRootDescriptorTables(ID3D12GraphicsCommandList* pCmdList)
		{
			if (m_ComputeHandleCache.m_StaleRootParamsBitMap != 0)
			{
				CopyAndBindStagedTables(m_ComputeHandleCache, pCmdList, &ID3D12GraphicsCommandList::SetComputeRootDescriptorTable);
			}
		}

	private:
		static const uint32_t s_kNumDescriptorsPerHeap = 1024;
		static std::mutex s_Mutex;
		// 0 - D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1 - D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
		static std::vector<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>> s_DescriptorHeapPool[2];
		static std::queue<std::pair<uint64_t, ID3D12DescriptorHeap*>> s_RetiredDescriptorHeaps[2];
		static std::queue<ID3D12DescriptorHeap*> s_AvailableDescriptorHeaps[2];

		static ID3D12DescriptorHeap* RequestDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type);
		static void DiscardDescriptorHeaps(D3D12_DESCRIPTOR_HEAP_TYPE type, uint64_t fenceValueForReset,
			const std::vector<ID3D12DescriptorHeap*>& usedHeaps);

		CommandContext& m_Context;
		ID3D12DescriptorHeap* m_CurHeap;
		const D3D12_DESCRIPTOR_HEAP_TYPE m_DescriptorType;
		uint32_t m_DescriptorSize;
		uint32_t m_CurOffset;
		DescriptorHandle m_FirstDescriptor;
		std::vector<ID3D12DescriptorHeap*> m_RetiredHeaps;

		// describes a descriptor table entry: a region of the handle cache and which handles have been set
		struct DescriptorTableCache
		{
			DescriptorTableCache() : assignedHandlesBitMap(0) {}

			uint32_t assignedHandlesBitMap;
			D3D12_CPU_DESCRIPTOR_HANDLE* tableStart;
			uint32_t tableSize;
		};

		struct DescriptorHandleCache
		{
			DescriptorHandleCache()
			{
				ClearCahce();
			}

			void ClearCahce()
			{
				m_RootDescriptorTablesBitMap = 0;
				m_MaxCachedDescriptors = 0;
			}

			uint32_t m_RootDescriptorTablesBitMap;
			uint32_t m_StaleRootParamsBitMap;
			uint32_t m_MaxCachedDescriptors;
			
			static const uint32_t kMaxNumDescriptors = 256;
			static const uint32_t kMaxNumDescriptorTables = 16;

			uint32_t ComputeStagedSize();
			void CopyAndBindStaleTables(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t descriptorSize, DescriptorHandle destHandle,
				ID3D12GraphicsCommandList* pCmdList, void (STDMETHODCALLTYPE ID3D12GraphicsCommandList::*SetFunc)(UINT, D3D12_GPU_DESCRIPTOR_HANDLE));

			DescriptorTableCache m_RootDescriptorTable[kMaxNumDescriptorTables];
			D3D12_CPU_DESCRIPTOR_HANDLE m_HandleCache[kMaxNumDescriptors];

			void UnbindAllValid();
			void StageDescriptorHandles(UINT rootIndex, UINT offset, UINT numHandles, const D3D12_CPU_DESCRIPTOR_HANDLE handles[]);
			void ParseRootSignature(D3D12_DESCRIPTOR_HEAP_TYPE type, const RootSignature& rootSig);

		};

		DescriptorHandleCache m_GraphicsHandleCache;
		DescriptorHandleCache m_ComputeHandleCache;

		bool HasSpace(uint32_t count)
		{
			return (m_CurHeap != nullptr && m_CurOffset + count <= s_kNumDescriptorsPerHeap);
		}

		void RetireCurrentHeap();
		void RetireUsedHeaps(uint64_t fenceValue);
		ID3D12DescriptorHeap* GetHeapPointer();

		DescriptorHandle Allocate(UINT count)
		{
			DescriptorHandle ret = m_FirstDescriptor + m_CurOffset * m_DescriptorSize;
			m_CurOffset += count;
			return ret;
		}

		void CopyAndBindStagedTables(DescriptorHandleCache& handleCache, ID3D12GraphicsCommandList* pCmdList,
			void (STDMETHODCALLTYPE ID3D12GraphicsCommandList::* SetFunc)(UINT, D3D12_GPU_DESCRIPTOR_HANDLE));

		// mark all descriptors in the cache as stale and in need of re-uploading
		void UnbindAllValid();
	};

}
