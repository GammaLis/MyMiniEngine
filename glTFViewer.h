#pragma once
#include "pch.h"
#include "IGameApp.h"
#include "glTFImporter.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "Camera.h"
#include "CameraController.h"
#include "GpuBuffer.h"

#define SHADING_MODEL_METALLIC_ROUGHNESS

namespace MyDirectX
{
	class glTFViewer : public IGameApp
	{
	public:
		glTFViewer(HINSTANCE hInstance, const std::string &glTFFileName, const wchar_t* title = L"Hello, World!", 
			UINT width = SCR_WIDTH, UINT height = SCR_HEIGHT);

		virtual void Update(float deltaTime) override;
		virtual void Render() override;

	protected:
		virtual void InitAssets() override;

	private:
		virtual void CleanCustom() override;

		void RenderObjects(GraphicsContext& gfx, const Math::Matrix4 viewProjMat, ObjectFilter filter = ObjectFilter::kAll);

		Math::Camera m_Camera;
		std::unique_ptr<CameraController> m_CameraController;
		Math::Matrix4 m_ViewProjMatrix;

		GraphicsPSO m_ModelViewerPSO;

		glTF::glTFImporter m_Importer;

		// lights
		StructuredBuffer m_LightBuffer;

		// SH
		RootSignature m_SHRS;
		ComputePSO m_SHPSO;
		// resources
		D3D12_CPU_DESCRIPTOR_HANDLE m_SHsrv;
		StructuredBuffer m_SHOutput;
	};
}
