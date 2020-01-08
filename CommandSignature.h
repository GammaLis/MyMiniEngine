#pragma once
#include "pch.h"

namespace MyDirectX
{
	class RootSignature;

	class IndirectParameter
	{
		friend class CommandSignature;

	public:
		IndirectParameter()
		{
			m_IndirectParam.Type = (D3D12_INDIRECT_ARGUMENT_TYPE)0xFFFFFFFF;
		}

		void Draw() { m_IndirectParam.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW; }
		void DrawIndexed() { m_IndirectParam.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED; }
		void Dispatch() { m_IndirectParam.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH; }
		
		void VertexBufferView(UINT slot)
		{
			m_IndirectParam.Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
			m_IndirectParam.VertexBuffer.Slot = slot;
		}

		void IndexBufferView()
		{
			m_IndirectParam.Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW;
		}

		void Constant(UINT rootParameterIndex, UINT destOffsetIn32BitValues, UINT num32BitValuesToSet)
		{
			m_IndirectParam.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
			m_IndirectParam.Constant.RootParameterIndex = rootParameterIndex;
			m_IndirectParam.Constant.DestOffsetIn32BitValues = destOffsetIn32BitValues;
			m_IndirectParam.Constant.Num32BitValuesToSet = num32BitValuesToSet;
		}

		void ConstantBufferView(UINT rootParameterIndex)
		{
			m_IndirectParam.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
			m_IndirectParam.ConstantBufferView.RootParameterIndex = rootParameterIndex;
		}

		void ShaderResourceView(UINT rootParameterIndex)
		{
			m_IndirectParam.Type = D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW;
			m_IndirectParam.ShaderResourceView.RootParameterIndex = rootParameterIndex;
		}

		void UnorderedAccessView(UINT rootParameterIndex)
		{
			m_IndirectParam.Type = D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW;
			m_IndirectParam.UnorderedAccessView.RootParameterIndex = rootParameterIndex;
		}

		const D3D12_INDIRECT_ARGUMENT_DESC& GetDesc() const { return m_IndirectParam; }

	protected:
		D3D12_INDIRECT_ARGUMENT_DESC m_IndirectParam;

	};

	class CommandSignature
	{
	public:
		CommandSignature(UINT numParams = 0) : m_Finalized(FALSE), m_NumParameters(numParams)
		{
			Reset(numParams);
		}

		void Destroy()
		{
			m_Signature = nullptr;
			m_ParamArray = nullptr;
		}

		void Reset(UINT numParams)
		{
			if (numParams > 0)
				m_ParamArray.reset(new IndirectParameter[numParams]);
			else
				m_ParamArray = nullptr;
			m_NumParameters = numParams;
		}

		IndirectParameter& operator[](size_t entryIndex)
		{
			ASSERT(entryIndex < m_NumParameters);
			return m_ParamArray[entryIndex];	// m_ParamArray.get()[entryIndex]
		}

		const IndirectParameter& operator[](size_t entryIndex) const
		{
			ASSERT(entryIndex < m_NumParameters);
			return m_ParamArray[entryIndex];
		}

		void Finalize(ID3D12Device *pDevice, const RootSignature* rootSignature = nullptr);

		ID3D12CommandSignature* GetSignature() const { return m_Signature.Get(); }

	private:
		BOOL m_Finalized;
		UINT m_NumParameters;
		std::unique_ptr<IndirectParameter[]> m_ParamArray;
		Microsoft::WRL::ComPtr<ID3D12CommandSignature> m_Signature;
	};

}

/**
	MSDN 间接绘制
	通过间接绘制，某些场景遍历和精选可以从CPU移动到GPU，从而提高性能。可以通过CPU或GPU生成命令缓冲区。

	命令签名(CommandSignature)
	ID3DCommandSignature允许应用指定间接绘制
	ID3DGraphicsCommandList - DrawInstanced, DrawIndexedInstanced, Dispatch
	启动时，应用会创建一小部分的命令签名。运行时，应用程序会使用命令填充缓冲区,包括VertexBufferView，
IndexBufferView，RootConstants 和 RootDescriptors(raw or structured SRV/UAV/CBVs)
*/
