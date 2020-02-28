#include "RootSignature.h"
#include "Hash.h"
#include <mutex>
#include <thread>

namespace MyDirectX
{
    using Microsoft::WRL::ComPtr;

    static std::map<size_t, ComPtr<ID3D12RootSignature>> s_RootSignatureHashMap;

    void RootSignature::DestroyAll()
    {
        s_RootSignatureHashMap.clear();
    }

    void RootSignature::InitStaticSampler(UINT reg, const D3D12_SAMPLER_DESC& nonStaticSamplerDesc, D3D12_SHADER_VISIBILITY visibility)
    {
        ASSERT(m_NumInitializedStaticSamplers < m_NumSamplers);
        D3D12_STATIC_SAMPLER_DESC& staticSamplerDesc = m_SamplerArray[m_NumInitializedStaticSamplers++];

        staticSamplerDesc.Filter = nonStaticSamplerDesc.Filter;
        staticSamplerDesc.AddressU = nonStaticSamplerDesc.AddressU;
        staticSamplerDesc.AddressV = nonStaticSamplerDesc.AddressV;
        staticSamplerDesc.AddressW = nonStaticSamplerDesc.AddressW;
        staticSamplerDesc.MipLODBias = nonStaticSamplerDesc.MipLODBias;
        staticSamplerDesc.MaxAnisotropy = nonStaticSamplerDesc.MaxAnisotropy;
        staticSamplerDesc.ComparisonFunc = nonStaticSamplerDesc.ComparisonFunc;
        staticSamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        staticSamplerDesc.MinLOD = nonStaticSamplerDesc.MinLOD;
        staticSamplerDesc.MaxLOD = nonStaticSamplerDesc.MaxLOD;
        staticSamplerDesc.ShaderRegister = reg;
        staticSamplerDesc.RegisterSpace = 0;
        staticSamplerDesc.ShaderVisibility = visibility;

        if (staticSamplerDesc.AddressU == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
            staticSamplerDesc.AddressV == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
            staticSamplerDesc.AddressW == D3D12_TEXTURE_ADDRESS_MODE_BORDER)
        {
            WARN_ONCE_IF_NOT(
                // transparent black
                nonStaticSamplerDesc.BorderColor[0] == 0.0f &&
                nonStaticSamplerDesc.BorderColor[1] == 0.0f &&
                nonStaticSamplerDesc.BorderColor[2] == 0.0f &&
                nonStaticSamplerDesc.BorderColor[3] == 0.0f ||
                // opaque black
                nonStaticSamplerDesc.BorderColor[0] == 0.0f &&
                nonStaticSamplerDesc.BorderColor[1] == 0.0f &&
                nonStaticSamplerDesc.BorderColor[2] == 0.0f &&
                nonStaticSamplerDesc.BorderColor[3] == 1.0f ||
                // opaque white
                nonStaticSamplerDesc.BorderColor[0] == 1.0f &&
                nonStaticSamplerDesc.BorderColor[1] == 1.0f &&
                nonStaticSamplerDesc.BorderColor[2] == 1.0f &&
                nonStaticSamplerDesc.BorderColor[3] == 1.0f,
                "Sampler border color does not match static sampler limitations"
            );

            if (nonStaticSamplerDesc.BorderColor[3] == 1.0f)    // opaque
            {
                if (nonStaticSamplerDesc.BorderColor[0] == 1.0f)
                    staticSamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
                else
                    staticSamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
            }
            else
                staticSamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        }
    }

    void RootSignature::Finalize(ID3D12Device* pDevice, const std::wstring& name, D3D12_ROOT_SIGNATURE_FLAGS flags)
    {
        if (m_Finalized)
            return;

        // -mf
        ASSERT(pDevice != nullptr);

        ASSERT(m_NumInitializedStaticSamplers == m_NumSamplers);

        D3D12_ROOT_SIGNATURE_DESC rootDesc;
        rootDesc.NumParameters = m_NumParameters;
        rootDesc.pParameters = (const D3D12_ROOT_PARAMETER*)m_ParamArray.get();
        rootDesc.NumStaticSamplers = m_NumSamplers;
        rootDesc.pStaticSamplers = (const D3D12_STATIC_SAMPLER_DESC*)m_SamplerArray.get();
        rootDesc.Flags = flags;

        m_DescriptorTableBitMap = 0;
        m_SamplerTableBitMap = 0;

        size_t hashCode = Utility::HashState(&rootDesc.Flags);
        hashCode = Utility::HashState(rootDesc.pStaticSamplers, m_NumSamplers, hashCode);

        for (UINT param = 0; param < m_NumParameters; ++param)
        {
            const D3D12_ROOT_PARAMETER& rootParam = rootDesc.pParameters[param];
            m_DescriptorTableSize[param] = 0;

            if (rootParam.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            {
                ASSERT(rootParam.DescriptorTable.pDescriptorRanges != nullptr);

                hashCode = Utility::HashState(rootParam.DescriptorTable.pDescriptorRanges,
                    rootParam.DescriptorTable.NumDescriptorRanges, hashCode);

                // we keep track of sampler descriptor tables separately from CBV_SRV_UAV descriptor tables
                if (rootParam.DescriptorTable.pDescriptorRanges->RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
                    m_SamplerTableBitMap |= (1 << param);
                else
                    m_DescriptorTableBitMap |= (1 << param);

                for (UINT tableRange = 0; tableRange < rootParam.DescriptorTable.NumDescriptorRanges; ++tableRange)
                {
                    m_DescriptorTableSize[param] += rootParam.DescriptorTable.pDescriptorRanges[tableRange].NumDescriptors;
                }
            }
            else
                hashCode = Utility::HashState(&rootParam, 1, hashCode);
        }

        ID3D12RootSignature** RSRef = nullptr;
        bool firstCompile = false;
        {
            static std::mutex s_HashMapMutex;
            std::lock_guard<std::mutex> lockGuard(s_HashMapMutex);

            auto iter = s_RootSignatureHashMap.find(hashCode);

            // reserve space so the next inquiry will find that someone got here first
            // map里面没有，首次编译
            if (iter == s_RootSignatureHashMap.end())
            {
                RSRef = s_RootSignatureHashMap[hashCode].GetAddressOf();
                firstCompile = true;
            }
            else
                RSRef = iter->second.GetAddressOf();

        }

        if (firstCompile)
        {
            ComPtr<ID3DBlob> pOutBlob, pErrorBlob;

            ASSERT_SUCCEEDED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                pOutBlob.GetAddressOf(), pErrorBlob.GetAddressOf()));

            // 打印错误信息
            //HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
            //    pOutBlob.GetAddressOf(), pErrorBlob.GetAddressOf());
            //if (FAILED(hr))
            //{
            //    Utility::Print((const char*)pErrorBlob->GetBufferPointer());
            //}

            ASSERT_SUCCEEDED(pDevice->CreateRootSignature(1, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(),
                IID_PPV_ARGS(&m_Signature)));

            m_Signature->SetName(name.c_str());

            s_RootSignatureHashMap[hashCode].Attach(m_Signature);
            ASSERT(*RSRef == m_Signature);
        }
        else
        {
            while (*RSRef == nullptr)
                std::this_thread::yield();  // 将当前线程CPU “时间片”让渡给其他线程（其他线程会争抢此“时间片”）
            m_Signature = *RSRef;
        }

        m_Finalized = TRUE;
    }
}

/**
    std::this_thread::yield()
    比如线程需要等待某个操作完成，如果直接使用一个循环不断判断这个操作是否完成就会使得这个线程占满CPU时间，
这会造成资源浪费。这时候可以判断一次操作是否完成，如果没有完成就调用yield交出时间片，过一会儿再来判断是否完成，
这样这个线程占用CPU时间会大大减少。举例：
    while(!isDone());   // Bad
    while(!isDone()) yield(); // Good

    https://www.zhihu.com/question/52892878/answer/132533818
*/