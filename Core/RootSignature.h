#pragma once
#include "pch.h"

namespace MyDirectX
{
	class RootParameter
	{
		friend class RootSignature;
	public:
		RootParameter()
		{
			m_RootParam.ParameterType = (D3D12_ROOT_PARAMETER_TYPE)0xFFFFFFFF;
		}
		~RootParameter()
		{
			Clear();
		}

		void Clear()
		{
			if (m_RootParam.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
				delete[] m_RootParam.DescriptorTable.pDescriptorRanges;
			m_RootParam.ParameterType = (D3D12_ROOT_PARAMETER_TYPE)0xFFFFFFFF;
		}

		// reg - register
		void InitAsConstants(UINT reg, UINT numDwords, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
		{
			m_RootParam.Constants.Num32BitValues = numDwords;
			m_RootParam.Constants.ShaderRegister = reg;
			m_RootParam.Constants.RegisterSpace = 0;
			m_RootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			m_RootParam.ShaderVisibility = visibility;
		}

		void InitAsConstantBuffer(UINT reg, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
		{
			m_RootParam.Descriptor.ShaderRegister = reg;
			m_RootParam.Descriptor.RegisterSpace = 0;
			m_RootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
			m_RootParam.ShaderVisibility = visibility;
		}

		void InitAsBufferSRV(UINT reg, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
		{
			m_RootParam.Descriptor.ShaderRegister = reg;
			m_RootParam.Descriptor.RegisterSpace = 0;
			m_RootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
			m_RootParam.ShaderVisibility = visibility;
		}

		void InitAsBufferUAV(UINT reg, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
		{
			m_RootParam.Descriptor.ShaderRegister = reg;
			m_RootParam.Descriptor.RegisterSpace = 0;
			m_RootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
			m_RootParam.ShaderVisibility = visibility;
		}

		void InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE type, UINT reg, UINT count, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
		{
			InitAsDescriptorTable(1, visibility);
			SetTableRange(0, type, reg, count);
		}

		void InitAsDescriptorTable(UINT rangeCount, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
		{
			m_RootParam.DescriptorTable.NumDescriptorRanges = rangeCount;
			m_RootParam.DescriptorTable.pDescriptorRanges = new D3D12_DESCRIPTOR_RANGE[rangeCount];
			m_RootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			m_RootParam.ShaderVisibility = visibility;			
		}

		void SetTableRange(UINT rangeIndex, D3D12_DESCRIPTOR_RANGE_TYPE type, UINT reg, UINT count, UINT space = 0)
		{
			D3D12_DESCRIPTOR_RANGE* range = const_cast<D3D12_DESCRIPTOR_RANGE*>(m_RootParam.DescriptorTable.pDescriptorRanges + rangeIndex);
			range->RangeType = type;
			range->NumDescriptors = count;
			range->BaseShaderRegister = reg;
			range->RegisterSpace = space;
			range->OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		}

		const D3D12_ROOT_PARAMETER& operator() () const { return m_RootParam; }

	protected:
		D3D12_ROOT_PARAMETER m_RootParam;
	};

	/**
		Maximum 64 DWORDS divied up amongst all root parameters
		root constants = 1 DWORD * numConstants
		root descriptor (CBV, SRV, or UAV) = 2 DWORDS each
		descriptor table pointer = 1 DWORD
		static samplers = 0 DWORDS (compiled into shader)
	*/
	class RootSignature
	{
		friend class DynamicDescriptorHeap;

	public:
		RootSignature(UINT numRootParams = 0, UINT numStaticSamplers = 0)
			: m_Finalized(FALSE), m_NumParameters(numRootParams)
		{
			Reset(numRootParams, numStaticSamplers);
		}
		~RootSignature() {  }

		static void DestroyAll();

		void Reset(UINT numRootParams, UINT numStaticSamplers = 0)
		{
			if (numRootParams > 0)
				m_ParamArray.reset(new RootParameter[numRootParams]);
			else
				m_ParamArray = nullptr;
			m_NumParameters = numRootParams;

			if (numStaticSamplers > 0)
				m_SamplerArray.reset(new D3D12_STATIC_SAMPLER_DESC[numStaticSamplers]);
			else
				m_SamplerArray = nullptr;
			m_NumSamplers = numStaticSamplers;
			m_NumInitializedStaticSamplers = 0;
		}

		RootParameter& operator[](size_t entryIndex)
		{
			ASSERT(entryIndex < m_NumParameters);
			return m_ParamArray[entryIndex];
		}

		const RootParameter& operator[](size_t entryIndex) const
		{
			ASSERT(entryIndex < m_NumParameters);
			return m_ParamArray[entryIndex];
		}

		void InitStaticSampler(UINT reg, const D3D12_SAMPLER_DESC& nonStaticSamplerDesc,
			D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

		void Finalize(ID3D12Device* pDevice, const std::wstring& name, D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE);

		ID3D12RootSignature* GetSignature() const { return m_Signature; }

	private:
		BOOL m_Finalized;
		UINT m_NumParameters;
		UINT m_NumSamplers;
		UINT m_NumInitializedStaticSamplers;
		uint32_t m_DescriptorTableBitMap;		// one bit is set for root parameters that are non-sampler descriptor tables
		uint32_t m_SamplerTableBitMap;			// one bit is set for root parameters that are sampler descriptor tables
		uint32_t m_DescriptorTableSize[16];		// non-sampler descriptor tables need to know their descriptor count
		std::unique_ptr<RootParameter[]> m_ParamArray;
		std::unique_ptr<D3D12_STATIC_SAMPLER_DESC[]> m_SamplerArray;
		ID3D12RootSignature* m_Signature;
	};

}
