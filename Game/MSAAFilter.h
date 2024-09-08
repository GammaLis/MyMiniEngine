#pragma once

#include "pch.h"
#include "ColorBuffer.h"
#include "DepthBuffer.h"
#include "FrameDescriptorHeap.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "Utilities/GameUtility.h"

namespace MFalcor
{
    class Scene;
}

namespace MyDirectX
{
    class GraphicsContext;

    class MSAAFilter
    {
        friend class SceneViewer;
    public:
        enum RSId
        {
            CBConstants = 0,
            CBVTable,
            SRVTable,
            UAVTable,
            Count,
        };

        static constexpr uint32_t kSamples = 4;
        
        void Init(ID3D12Device *pDevice, MFalcor::Scene* pScene);
        void Destroy();

        void BeginRendering(GraphicsContext &gfx, MFalcor::Scene *pScene);
        void EndRendering(GraphicsContext &gfx);
        void Resolve(GraphicsContext& gfx);

        const RootSignature& GetRootSignature() const { return m_RSMS; }
        const FrameDescriptorHeap& GetDescriptorHeap() const { return m_FrameDescriptorHeap; }
        const ColorBuffer& GetColorBuffer() const { return m_ColorBufferMS; }
        const DepthBuffer& GEtDepthBuffer() const { return m_DepthBufferMS; }

    private:
        ColorBuffer m_ColorBufferMS;
        DepthBuffer m_DepthBufferMS;

        ColorBuffer m_ColorResolve;
        DepthBuffer m_DepthResolve;

        RootSignature m_RSMS;
        GraphicsPSO m_DepthOnlyPSO{ L"DepthOnly" };
        GraphicsPSO m_ColorPSO;
        ComputePSO m_ComputeResolvePSO{ L"ComputeResolve"};

        uint32_t m_ColorMSIndex{ INVALID_INDEX };

        FrameDescriptorHeap m_FrameDescriptorHeap;
        std::vector<DescriptorRange> m_DescRanges;
    };
}
