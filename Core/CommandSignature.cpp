#include "CommandSignature.h"
#include "RootSignature.h"

using namespace MyDirectX;

void CommandSignature::Finalize(ID3D12Device* pDevice, const RootSignature* rootSignature)
{
	if (m_Finalized)
		return;

	// -mf
	ASSERT(pDevice != nullptr);

	UINT byteStride = 0;
	bool requireRootSignature = false;

	for (UINT i = 0; i < m_NumParameters; ++i)
	{
		switch (m_ParamArray[i].GetDesc().Type)
		{
		case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
			byteStride += sizeof(D3D12_DRAW_ARGUMENTS);
			break;
		case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
			byteStride += sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
			break;
		case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
			byteStride += sizeof(D3D12_DISPATCH_ARGUMENTS);
			break;
		case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT:
			byteStride += m_ParamArray[i].GetDesc().Constant.Num32BitValuesToSet * 4;
			requireRootSignature = true;
			break;
		case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW:
			byteStride += sizeof(D3D12_VERTEX_BUFFER_VIEW);
			break;
		case D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW:
			byteStride += sizeof(D3D12_INDEX_BUFFER_VIEW);
			break;
		case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
		case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
		case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
			byteStride += 8;
			requireRootSignature = true;
			break;
		}
	}

	D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc;
	commandSignatureDesc.ByteStride = byteStride;
	commandSignatureDesc.NumArgumentDescs = m_NumParameters;
	commandSignatureDesc.pArgumentDescs = (const D3D12_INDIRECT_ARGUMENT_DESC*)m_ParamArray.get();
	commandSignatureDesc.NodeMask = 1;

	Microsoft::WRL::ComPtr<ID3DBlob> pOutBlob, pErrorBlob;
	ID3D12RootSignature* pRootSig = rootSignature ? rootSignature->GetSignature() : nullptr;
	if (requireRootSignature)
	{
		ASSERT(pRootSig != nullptr);
	}
	else
	{
		pRootSig = nullptr;
	}

	ASSERT_SUCCEEDED(pDevice->CreateCommandSignature(&commandSignatureDesc, pRootSig, IID_PPV_ARGS(&m_Signature)));

	m_Signature->SetName(L"CommandSignature");

	m_Finalized = TRUE;
}
