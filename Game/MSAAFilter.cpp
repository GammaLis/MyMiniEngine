#include "MSAAFilter.h"
#include "Scenes/Scene.h"
#include "Graphics.h"
#include "GfxCommon.h"
#include "CommandContext.h"
#include "ProfilingScope.h"

// Compiled shaders
#include "DepthOnlyDynDescVS.h"
#include "DepthOnlyDynDescPS.h"
#include "DepthOnlyClipPS.h"

#include "ResolveCS.h"

using namespace MyDirectX;

void MSAAFilter::Init(ID3D12Device *pDevice, MFalcor::Scene* pScene)
{
    const auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
    uint32_t width = colorBuffer.GetWidth(), height = colorBuffer.GetHeight();
    auto colorFormat = colorBuffer.GetFormat();
    auto depthFormat = DXGI_FORMAT_D32_FLOAT;
    
    {
        m_RSMS.Reset(RSId::CBConstants+1, 7);
        m_RSMS[RSId::CBConstants].InitAsConstants(0, 64, 1);
        // m_RSMS[RSId::CBVTable].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 0, 16);
        // m_RSMS[RSId::SRVTable].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 16);
        // m_RSMS[RSId::UAVTable].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 16);
        m_RSMS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearClampDesc);
        m_RSMS.InitStaticSampler(1, Graphics::s_CommonStates.SamplerLinearWrapDesc);
        m_RSMS.InitStaticSampler(2, Graphics::s_CommonStates.SamplerPointClampDesc);
        m_RSMS.InitStaticSampler(3, Graphics::s_CommonStates.SamplerPointBorderDesc);
        m_RSMS.InitStaticSampler(4, Graphics::s_CommonStates.SamplerAnisoWrapDesc);
        m_RSMS.InitStaticSampler(5, Graphics::s_CommonStates.SamplerShadowDesc);
        m_RSMS.InitStaticSampler(6, Graphics::s_CommonStates.SamplerVolumeWrapDesc);
        m_RSMS.Finalize(pDevice, L"CommonRS",
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
            D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED);
    }

    // PSO

    {
        m_DepthOnlyPSO = pScene->m_DepthPSO;
        // m_DepthOnlyPSO.SetInputLayout()
        m_DepthOnlyPSO.SetRootSignature(m_RSMS);
        m_DepthOnlyPSO.SetVertexShader(D3D12_SHADER_BYTECODE(DepthOnlyDynDescVS, sizeof(DepthOnlyDynDescVS)));
        m_DepthOnlyPSO.SetPixelShader(D3D12_SHADER_BYTECODE(DepthOnlyDynDescPS, sizeof(DepthOnlyDynDescPS)));
        m_DepthOnlyPSO.SetRasterizerState( Graphics::s_CommonStates.RasterizerDefaultMsaa );
        m_DepthOnlyPSO.SetBlendState(Graphics::s_CommonStates.BlendDisable);
        m_DepthOnlyPSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateReadWrite);
        m_DepthOnlyPSO.SetRenderTargetFormat(colorFormat, depthFormat, kSamples);
        m_DepthOnlyPSO.Finalize(pDevice);

        m_ComputeResolvePSO.SetRootSignature(m_RSMS);
        m_ComputeResolvePSO.SetComputeShader(D3D12_SHADER_BYTECODE(ResolveCS, sizeof(ResolveCS)));
        m_ComputeResolvePSO.Finalize(pDevice);
    }

    // Textures
    {
        static constexpr uint32_t Scale = 2;

        m_ColorBufferMS.SetMsaaMode(kSamples, kSamples);
        m_ColorBufferMS.Create(pDevice, L"Color Buffer MS", width/Scale, height/Scale, 1, colorFormat);
        m_DepthBufferMS.Create(pDevice, L"Depth Buffer MS", width/Scale, height/Scale, kSamples, depthFormat);

        m_ColorResolve.Create(pDevice, L"Color Resolve", width, height, 1, colorFormat);
        m_DepthResolve.Create(pDevice, L"Depth Resolve", width, height, 1, depthFormat);

    }

    // Frame descriptor heap
    m_FrameDescriptorHeap.Create(pDevice, L"FrameDescHeap", 512);
    pScene->UpdateDescriptorHeap(pDevice, m_FrameDescriptorHeap, m_DescRanges);
    {
        m_ColorMSIndex = m_FrameDescriptorHeap.PersistentAllocated();
        m_DescRanges.emplace_back(DescriptorRange{ m_ColorMSIndex, 2 });

        // Color buffer
        m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, m_ColorBufferMS.GetSRV());
        m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, m_ColorResolve.GetUAV());

        // m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, m_DepthBufferMS.GetDepthSRV());
        // m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, Graphics::s_BufferManager.m_SceneDepthBuffer.());
    }
}

void MSAAFilter::Destroy()
{
    m_ColorBufferMS.Destroy();
    m_DepthBufferMS.Destroy();

    m_ColorResolve.Destroy();
    m_DepthResolve.Destroy();

    m_FrameDescriptorHeap.Destroy();
    
}

void MSAAFilter::BeginRendering(GraphicsContext &gfx, MFalcor::Scene *pScene) 
{
    pScene->UpdateTemporaryDescriptorHeap(Graphics::s_Device, m_FrameDescriptorHeap, m_DescRanges);

    /**
     *  There is a new ordering constraint between 'SetDescriptorHeaps' and 'SetGraphicsRootSignature' or
     * 'SetComputeRootSignature'. SetDescriptorHeaps must be called, passing the corresponding heaps, before
     * a call to 'SetGraphicsRootSignature' that use either 'CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED' or
     * 'SAMPLER_HEAP_DIRECTLY_INDEXED' flags. This is in order to make sure the correct heap pointers are
     * available when the root signature is set.
     */
    gfx.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_FrameDescriptorHeap.CurrentHeap()->GetHeapPointer());
    gfx.SetRootSignature(m_RSMS, false);

    gfx.TransitionResource(m_ColorBufferMS, D3D12_RESOURCE_STATE_RENDER_TARGET);
    gfx.TransitionResource(m_DepthBufferMS, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    gfx.ClearColor(m_ColorBufferMS);
    gfx.ClearDepthAndStencil(m_DepthBufferMS);
    gfx.SetRenderTarget(m_ColorBufferMS.GetRTV(), m_DepthBufferMS.GetDSV());
    gfx.SetViewportAndScissor( 0, 0, m_ColorBufferMS.GetWidth(), m_ColorBufferMS.GetHeight() );
}

void MSAAFilter::EndRendering(GraphicsContext& gfx)
{
    m_FrameDescriptorHeap.EndFrame();
}

void MSAAFilter::Resolve(GraphicsContext& gfx)
{
    ProfilingScope profilingScope(L"MSAA Resolve", gfx);

    auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
    auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;

    uint32_t w = colorBuffer.GetWidth(), h = colorBuffer.GetHeight();

    // Standard resovle 
#if 0
    {
        /**
         * The source and destionation resources must be the same resource type and have the same dimensions.
         * In addition, they must have compatible formats. There are three scenarios for this:
         * * source and destination are prestructured and typed. Requirements: both the src and dst must have identical formats and 
         *  that format must be specified in the format parameter.
         * * one resource is prestructured and typed and the other is prestructured and typeless. Requirements: the typed resource 
         *  must have a format that is compatible with the typeless resource. The format of the typed resource must be specified
         *  in the Format parameter.
         * * source and destination are prestructured and typeless.
         */
        gfx.ResolveSubresource(m_ColorResolve, 0, m_ColorBufferMS, 0, m_ColorBufferMS.GetFormat());
        gfx.ResolveSubresource(m_DepthResolve, 0, m_DepthBufferMS, 0, m_DepthBufferMS.GetFormat());
    }
#else
    // Custom resolve
    {
        auto &computeContext = gfx.GetComputeContext();

        computeContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_FrameDescriptorHeap.CurrentHeap()->GetHeapPointer());

        computeContext.SetRootSignature(m_RSMS, false);

        computeContext.TransitionResource(m_ColorBufferMS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        computeContext.TransitionResource(m_ColorResolve, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        computeContext.SetPipelineState(m_ComputeResolvePSO);

        float indexStart = float(m_ColorMSIndex);
        auto rtSize = GetSizeAndInvSize(static_cast<float>(w), static_cast<float>(h));
        float consts[] = 
        {
            // MS color index, resolve color index, sample radius
            indexStart, indexStart+1.0f, 1.0f, 0.0f, 
            rtSize.GetX(), rtSize.GetY(), rtSize.GetZ(), rtSize.GetW(),
            0, 0, 0, 0
        };
        computeContext.SetConstants(0, ARRAYSIZE(consts), consts);

        computeContext.Dispatch2D(w, h);
    }
#endif

    // Copy
    {
        RECT rect{ .left = 0, .top = 0, .right = (LONG)w, .bottom = (LONG)h };
        gfx.CopyTextureRegion(colorBuffer, 0 ,0, 0, m_ColorResolve, rect);
    }

}
