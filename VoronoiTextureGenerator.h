#pragma once
#include "IGameApp.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "GpuBuffer.h"
#include "DepthBuffer.h"
#include "ColorBuffer.h"
#include "CommandContext.h"
#include "Scenes/DebugPass.h"

namespace MyDirectX
{
    enum class VoronoiTextureRSId
    {
        CBConstants = 0,
        SRVTable,
        UAVTable,

        Count
    };

    class VoronoiTextureGenerator : public IGameApp
    {
    public:
        VoronoiTextureGenerator(HINSTANCE hInstance, const wchar_t* title = L"Hello, World!", UINT width = SCR_WIDTH, UINT height = SCR_HEIGHT);

        virtual void Update(float deltaTime) override;
        virtual void Render() override;

        static const DXGI_FORMAT s_DepthFormat;
        static const uint32_t s_Size = 512;

    private:
        virtual void InitPipelineStates() override;
        virtual void InitGeometryBuffers() override;
        virtual void InitCustom() override;

        virtual void CleanCustom() override;

        void UpdateVoronoi();
        void UpdateSeedTexture(ComputeContext &computeContext);
        void DoJFA(ComputeContext& computeContext);
        void GenerateVoronoiTexture(ComputeContext &computeContext);

        // root signature & PSOs
        RootSignature m_VoronoiRS;
        // I.
        GraphicsPSO m_VoronoiPSO;
        // II.
        ComputePSO m_InitSeedsPSO;
        ComputePSO m_JFAPSO;
        ComputePSO m_GenVoronoiTexPSO;

        // resources
        StructuredBuffer m_InstanceBuffer;
        DepthBuffer m_DepthBuffer;

        //
        ColorBuffer m_InitTexture;
        ColorBuffer m_PingpongBuffers[2];
        ColorBuffer m_VoronoiTexture;

        DebugPass m_DebugPass;

        // params
        uint32_t m_NumVertex = 32;
    };
}
