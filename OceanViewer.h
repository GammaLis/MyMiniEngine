#pragma once
#include "IGameApp.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "GPUBuffer.h"
#include "ColorBuffer.h"
#include "Camera.h"
#include "CameraController.h"
#include "Scenes/DebugPass.h"

namespace MyDirectX
{
    enum class WaterWaveRSId
    {
        CBConstants = 0,
        CBPerObject,
        CBPerCamera,
        SRVTable,
        UAVTable,

        Count
    };   

    class OceanViewer : public IGameApp
    {
    public:
        OceanViewer(HINSTANCE hInstance, const wchar_t* title = L"Hello, World!", UINT width = SCR_WIDTH, UINT height = SCR_HEIGHT);

        virtual void Update(float deltaTime) override;
        virtual void Render() override;

        static const uint32_t N = 256;

    private:
        virtual void InitPipelineStates() override;
        virtual void InitGeometryBuffers() override;
        virtual void InitCustom() override;

        virtual void CleanCustom() override;

        void InitUnchangedResources();
        void UpdateSpectrumsAndDoIFFT();
        void DoIFFT(ComputeContext &computeContext, ColorBuffer &spectrumInput, ColorBuffer &horizontalOutput, ColorBuffer &verticalOutput);
        void CombineOutputs(ComputeContext &computeContext);

        // RootSignature & PSOs
        RootSignature m_WaterWaveRS;
        ComputePSO m_InitH0SpectrumPSO;
        ComputePSO m_GenerateFFTWeightsPSO;
        ComputePSO m_UpdateSpectrumPSO;
        ComputePSO m_FFTHorizontalPSO;
        ComputePSO m_FFTVerticalPSO;
        ComputePSO m_CombineOutputsPSO;
        GraphicsPSO m_BasicShadingPSO;

        uint32_t m_Rows = N;
        uint32_t m_Columns = N;
        uint32_t m_VertexCount = 0;
        uint32_t m_VertexOffset = 0;
        uint32_t m_IndexCount = 0;
        uint32_t m_IndexOffset = 0;
        StructuredBuffer m_MeshVB;
        ByteAddressBuffer m_MeshIB;
        
        ColorBuffer m_H0SpectrumTexture;
        ColorBuffer m_FFTWeightsTexture;
        // spectrum textures
        ColorBuffer m_HeightSpectrumTexture;
        ColorBuffer m_DXSpectrumTexture;
        ColorBuffer m_DYSpectrumTexture;
        // ifft result textures
        ColorBuffer m_HeightMap;
        ColorBuffer m_TempHeightMap;
        ColorBuffer m_DXMap;
        ColorBuffer m_TempDXMap;
        ColorBuffer m_DYMap;
        ColorBuffer m_TempDYMap;
        // displacement & normal/fold
        ColorBuffer m_DisplacementMap;
        ColorBuffer m_NormalAndFoldMap;

        DebugPass m_DebugPass;

        // camera & camera controller
        Math::Camera m_Camera;
        std::unique_ptr<CameraController> m_CameraController;
        Math::Matrix4 m_ViewProjMatrix;

        // params
        float m_L = N;
        float m_V = 10.0f;
        float m_A = 40.0f;
        float m_Depth = 100.0f;
        XMFLOAT2 m_W = XMFLOAT2(1.0f, 1.0f);

    };
}
