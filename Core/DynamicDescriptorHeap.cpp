#include "DynamicDescriptorHeap.h"
#include "Graphics.h"
#include "CommandListManager.h"
#include "CommandContext.h"
#include "RootSignature.h"

namespace MyDirectX
{
	std::mutex DynamicDescriptorHeap::s_Mutex;
	std::vector<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>> DynamicDescriptorHeap::s_DescriptorHeapPool[2];
	std::queue<std::pair<uint64_t, ID3D12DescriptorHeap*>> DynamicDescriptorHeap::s_RetiredDescriptorHeaps[2];
	std::queue<ID3D12DescriptorHeap*> DynamicDescriptorHeap::s_AvailableDescriptorHeaps[2];

	ID3D12DescriptorHeap* DynamicDescriptorHeap::RequestDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type)
	{
		std::lock_guard<std::mutex> lockGuard(s_Mutex);

		uint32_t idx = type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ? 1 : 0;

		while (!s_RetiredDescriptorHeaps[idx].empty() && Graphics::s_CommandManager.IsFenceComplete(s_RetiredDescriptorHeaps[idx].front().first))
		{
			s_AvailableDescriptorHeaps[idx].push(s_RetiredDescriptorHeaps[idx].front().second);
			s_RetiredDescriptorHeaps[idx].pop();
		}

		if (!s_AvailableDescriptorHeaps[idx].empty())
		{
			ID3D12DescriptorHeap* pHeap = s_AvailableDescriptorHeaps[idx].front();
			s_AvailableDescriptorHeaps[idx].pop();
			return pHeap;
		}
		else
		{
			D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
			heapDesc.Type = type;
			heapDesc.NumDescriptors = s_kNumDescriptorsPerHeap;
			heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			heapDesc.NodeMask = 1;

			Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> pHeap;
			ASSERT_SUCCEEDED(Graphics::s_Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&pHeap)));
			s_DescriptorHeapPool[idx].emplace_back(pHeap);
			return pHeap.Get();

		}
	}

	void DynamicDescriptorHeap::DiscardDescriptorHeaps(D3D12_DESCRIPTOR_HEAP_TYPE type, uint64_t fenceValueForReset,
		const std::vector<ID3D12DescriptorHeap*>& usedHeaps)
	{
		uint32_t idx = type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ? 1 : 0;
		std::lock_guard<std::mutex> lockGuard(s_Mutex);

		for (auto iter = usedHeaps.begin(); iter != usedHeaps.end(); ++iter)
		{
			s_RetiredDescriptorHeaps[idx].push(std::make_pair(fenceValueForReset, *iter));
		}
	}

	DynamicDescriptorHeap::DynamicDescriptorHeap(CommandContext& context, D3D12_DESCRIPTOR_HEAP_TYPE type)
		: m_Context(context), m_DescriptorType(type)
	{
		m_CurHeap = nullptr;
		m_CurOffset = 0;
		m_DescriptorSize = Graphics::GetDescriptorHandleIncrementSize(type);
	}

	void DynamicDescriptorHeap::CleanupUsedHeaps(uint64_t fenceValue)
	{
		RetireCurrentHeap();
		RetireUsedHeaps(fenceValue);
		m_GraphicsHandleCache.ClearCahce();
		m_ComputeHandleCache.ClearCahce();
	}

	D3D12_GPU_DESCRIPTOR_HANDLE DynamicDescriptorHeap::UploadDirect(D3D12_CPU_DESCRIPTOR_HANDLE handle)
	{
		if (!HasSpace(1))
		{
			RetireCurrentHeap();
			UnbindAllValid();
		}

		m_Context.SetDescriptorHeap(m_DescriptorType, GetHeapPointer());

		DescriptorHandle destHandle = m_FirstDescriptor + m_CurOffset * m_DescriptorSize;
		m_CurOffset += 1;

		Graphics::s_Device->CopyDescriptorsSimple(1, destHandle.GetCpuHandle(), handle, m_DescriptorType);

		return destHandle.GetGpuHandle();
	}

	void DynamicDescriptorHeap::RetireCurrentHeap()
	{
		// don't retire unused heaps
		if (m_CurOffset == 0)
		{
			ASSERT(m_CurHeap == nullptr);
			return;
		}

		ASSERT(m_CurHeap != nullptr);
		m_RetiredHeaps.push_back(m_CurHeap);
		m_CurHeap = nullptr;
		m_CurOffset = 0;
	}

	void DynamicDescriptorHeap::RetireUsedHeaps(uint64_t fenceValue)
	{
		DiscardDescriptorHeaps(m_DescriptorType, fenceValue, m_RetiredHeaps);
		m_RetiredHeaps.clear();
	}

	ID3D12DescriptorHeap* DynamicDescriptorHeap::GetHeapPointer()
	{
		if (m_CurHeap == nullptr)
		{
			ASSERT(m_CurOffset == 0);
			m_CurHeap = RequestDescriptorHeap(m_DescriptorType);
			m_FirstDescriptor = DescriptorHandle(
				m_CurHeap->GetCPUDescriptorHandleForHeapStart(),
				m_CurHeap->GetGPUDescriptorHandleForHeapStart());
		}

		return m_CurHeap;
	}

	void DynamicDescriptorHeap::CopyAndBindStagedTables(DescriptorHandleCache& handleCache, 
		ID3D12GraphicsCommandList* pCmdList, 
		void(STDMETHODCALLTYPE ID3D12GraphicsCommandList::* SetFunc)(UINT, D3D12_GPU_DESCRIPTOR_HANDLE))
	{
		uint32_t neededSize = handleCache.ComputeStagedSize();
		if (!HasSpace(neededSize))
		{
			RetireCurrentHeap();
			UnbindAllValid();
			neededSize = handleCache.ComputeStagedSize();
		}

		// this can trigger the creation of a new heap
		m_Context.SetDescriptorHeap(m_DescriptorType, GetHeapPointer());
		handleCache.CopyAndBindStaleTables(m_DescriptorType, m_DescriptorSize, Allocate(neededSize),
			pCmdList, SetFunc);
	}

	void DynamicDescriptorHeap::UnbindAllValid()
	{
		m_GraphicsHandleCache.UnbindAllValid();
		m_ComputeHandleCache.UnbindAllValid();
	}

	// DescriptorHandleCache
	uint32_t DynamicDescriptorHeap::DescriptorHandleCache::ComputeStagedSize()
	{
		// sum the maximum assigned offsets of stale descriptor tables to determine total needed space
		uint32_t neededSpace = 0;
		uint32_t rootIndex;
		uint32_t staleParams = m_StaleRootParamsBitMap;

		while (_BitScanForward((unsigned long*)&rootIndex, staleParams))
		{
			staleParams ^= (1 << rootIndex);

			uint32_t maxSetHandle;
			ASSERT(TRUE == _BitScanReverse((unsigned long*)&maxSetHandle, m_RootDescriptorTable[rootIndex].assignedHandlesBitMap),
				"Root entry marked as stale but has not stale descriptors");

			neededSpace += maxSetHandle + 1;
		}

		return neededSpace;
	}

	void DynamicDescriptorHeap::DescriptorHandleCache::CopyAndBindStaleTables(D3D12_DESCRIPTOR_HEAP_TYPE type,
		uint32_t descriptorSize, DescriptorHandle destHandle, ID3D12GraphicsCommandList* pCmdList,
		void(STDMETHODCALLTYPE ID3D12GraphicsCommandList::* SetFunc)(UINT, D3D12_GPU_DESCRIPTOR_HANDLE))
	{
		uint32_t staleParamCount = 0;
		uint32_t tableSize[DescriptorHandleCache::kMaxNumDescriptorTables];
		uint32_t rootIndices[DescriptorHandleCache::kMaxNumDescriptorTables];
		uint32_t neededSpace = 0;
		uint32_t rootIndex;

		// sum the maximum assigned offsets of stale descriptor tables to determine total needed space
		uint32_t staleParams = m_StaleRootParamsBitMap;
		while (_BitScanForward((unsigned long*)&rootIndex, staleParams))
		{
			rootIndices[staleParamCount] = rootIndex;
			staleParams ^= (1 << rootIndex);

			uint32_t maxSetHandle;
			ASSERT(TRUE == _BitScanReverse((unsigned long*)&maxSetHandle, m_RootDescriptorTable[rootIndex].assignedHandlesBitMap),
				"Root entry marked as stale but has no stale descriptors");

			neededSpace += maxSetHandle + 1;
			tableSize[staleParamCount] = maxSetHandle + 1;

			++staleParamCount;
		}

		ASSERT(staleParamCount <= DescriptorHandleCache::kMaxNumDescriptorTables,
			"We're only equipped to handle so many descriptor tables");

		m_StaleRootParamsBitMap = 0;

		static const uint32_t kMaxDescriptorsPerCopy = 16;
		UINT numDestDescriptorRanges = 0;
		D3D12_CPU_DESCRIPTOR_HANDLE pDestDescriptorRangeStarts[kMaxDescriptorsPerCopy];
		UINT pDestDescriptorRangeSizes[kMaxDescriptorsPerCopy];

		UINT numSrcDescriptorRanges = 0;
		D3D12_CPU_DESCRIPTOR_HANDLE pSrcDescriptorRangeStarts[kMaxDescriptorsPerCopy];
		UINT pSrcDescriptorRangeSizes[kMaxDescriptorsPerCopy];

		for (uint32_t i = 0; i < staleParamCount; ++i)
		{
			rootIndex = rootIndices[i];
			(pCmdList->*SetFunc)(rootIndex, destHandle.GetGpuHandle());

			DescriptorTableCache& rootDescTable = m_RootDescriptorTable[rootIndex];

			D3D12_CPU_DESCRIPTOR_HANDLE* srcHandles = rootDescTable.tableStart;
			uint64_t setHandles = (uint64_t)rootDescTable.assignedHandlesBitMap;
			D3D12_CPU_DESCRIPTOR_HANDLE curDest = destHandle.GetCpuHandle();
			destHandle += tableSize[i] * descriptorSize;

			unsigned long skipCount;
			// 比如 0b...10 0011 1000
			while (_BitScanForward64(&skipCount, setHandles))	// skipCount = 3
			{
				// skip over unset descriptor handles
				setHandles >>= skipCount;	// -> 0x...10 0011 1
				srcHandles += skipCount;
				curDest.ptr += skipCount * descriptorSize;

				unsigned long descriptorCount;
				_BitScanForward64(&descriptorCount, ~setHandles);
				setHandles >>= descriptorCount;

				// if we run out of temp room, copy what we've got so far
				if (numSrcDescriptorRanges + descriptorCount > kMaxDescriptorsPerCopy)
				{
					Graphics::s_Device->CopyDescriptors(
						numDestDescriptorRanges, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes,
						numSrcDescriptorRanges, pSrcDescriptorRangeStarts, pSrcDescriptorRangeSizes,
						type);
					numSrcDescriptorRanges = 0;
					numDestDescriptorRanges = 0;
				}

				// setup destination range
				pDestDescriptorRangeStarts[numDestDescriptorRanges] = curDest;
				pDestDescriptorRangeSizes[numDestDescriptorRanges] = descriptorCount;
				++numDestDescriptorRanges;

				// setup source ranges (one descriptor each because we don't assume they are contiguous)
				for (uint32_t j = 0; j < descriptorCount; ++j)
				{
					pSrcDescriptorRangeStarts[numSrcDescriptorRanges] = srcHandles[j];
					pSrcDescriptorRangeSizes[numSrcDescriptorRanges] = 1;
					++numSrcDescriptorRanges;
				}

				// move the destination pointer forward by the number of descriptors we will copy
				srcHandles += descriptorCount;
				curDest.ptr += descriptorCount * descriptorSize;
			}
		}

		Graphics::s_Device->CopyDescriptors(
			numDestDescriptorRanges, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes,
			numSrcDescriptorRanges, pSrcDescriptorRangeStarts, pSrcDescriptorRangeSizes,
			type);
	}

	void DynamicDescriptorHeap::DescriptorHandleCache::UnbindAllValid()
	{
		m_StaleRootParamsBitMap = 0;

		unsigned long tableParams = m_RootDescriptorTablesBitMap;
		unsigned long rootIndex;
		while (_BitScanForward(&rootIndex, tableParams))
		{
			tableParams ^= (1 << rootIndex);
			if (m_RootDescriptorTable[rootIndex].assignedHandlesBitMap != 0)
				m_StaleRootParamsBitMap |= (1 << rootIndex);
		}
	}

	void DynamicDescriptorHeap::DescriptorHandleCache::StageDescriptorHandles(UINT rootIndex, UINT offset,
		UINT numHandles, const D3D12_CPU_DESCRIPTOR_HANDLE handles[])
	{
		ASSERT(((1 << rootIndex) & m_RootDescriptorTablesBitMap) != 0, "Root parameter is not a CBV_SRV_UAV descriptor table");
		ASSERT(offset + numHandles <= m_RootDescriptorTable[rootIndex].tableSize);

		DescriptorTableCache& tableCache = m_RootDescriptorTable[rootIndex];
		D3D12_CPU_DESCRIPTOR_HANDLE* copyDest = tableCache.tableStart + offset;
		for (UINT i = 0; i < numHandles; ++i)
			copyDest[i] = handles[i];
		tableCache.assignedHandlesBitMap |= ((1 << numHandles) - 1) << offset;
		m_StaleRootParamsBitMap |= (1 << rootIndex);
	}

	void DynamicDescriptorHeap::DescriptorHandleCache::ParseRootSignature(D3D12_DESCRIPTOR_HEAP_TYPE type,
		const RootSignature& rootSig)
	{
		UINT curOffset = 0;

		ASSERT(rootSig.m_NumParameters <= 16, "Maybe we need to support something greater");

		m_StaleRootParamsBitMap = 0;
		m_RootDescriptorTablesBitMap = (type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ? rootSig.m_SamplerTableBitMap :
			rootSig.m_DescriptorTableBitMap);

		unsigned long tableParams = m_RootDescriptorTablesBitMap;
		unsigned long rootIndex;
		while (_BitScanForward(&rootIndex, tableParams))
		{
			tableParams ^= (1 << rootIndex);

			UINT tableSize = rootSig.m_DescriptorTableSize[rootIndex];
			ASSERT(tableSize > 0);

			DescriptorTableCache& rootDescriptorTable = m_RootDescriptorTable[rootIndex];
			rootDescriptorTable.assignedHandlesBitMap = 0;
			rootDescriptorTable.tableStart = m_HandleCache + curOffset;
			rootDescriptorTable.tableSize = tableSize;

			curOffset += tableSize;
		}

		m_MaxCachedDescriptors = curOffset;

		ASSERT(m_MaxCachedDescriptors <= kMaxNumDescriptors, "Exceeded user-supplied maximum cache size");
	}
}

// _BitScanReverse - 从高位到低位，第一个1出现位置（左边开始计数...3210）
// _BitScanForward - 从低位到高位，第一个1出现位置