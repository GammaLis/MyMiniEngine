#pragma once
#include "IGameApp.h"
#include "Camera.h"
#include "ShadowCamera.h"
#include "CameraController.h"
#include "GameInput.h"

namespace MyDirectX
{
	class GraphicsContext;

	class ModelViewer final : public IGameApp
	{
	public:
		ModelViewer(HINSTANCE hInstance, const wchar_t* title = L"Hello, World!", UINT width = SCR_WIDTH, UINT height = SCR_HEIGHT);

		virtual void Update(float deltaTime) override;
		virtual void Render() override;

		struct CommonStates
		{
			float SunLightIntensity = 4.0f;	// 0.0 - 16.0
			float AmbientIntensity = 0.1f;	// 
			float SunOrientation = -0.5f;	// -100.0 - 100.0
			float SunInclination = 0.75f;	// 0.0 - 1.0
			float ShadowDimX = 5000;		// 1000 - 10000
			float ShadowDimY = 3000;		// 1000 - 10000
			float ShadowDimZ = 3000;		// 1000 - 10000
		};
		CommonStates m_CommonStates;

	private:
		virtual void InitPipelineStates() override;
		virtual void InitGeometryBuffers() override;
		virtual void InitCustom() override;

		void RenderLightShadows(GraphicsContext& gfxContext);

		void RenderObjects(GraphicsContext& gfxContext, const Math::Matrix4 viewProjMat, ObjectFilter filter = ObjectFilter::kAll);

		Math::Camera m_Camera;
		std::unique_ptr<CameraController> m_CameraController;
		Math::Matrix4 m_ViewProjMatrix;

		// root signature & PSOs
		RootSignature m_RootSig;
		GraphicsPSO m_DepthPSO;
		GraphicsPSO m_CutoutDepthPSO;
		GraphicsPSO m_ModelPSO;
		GraphicsPSO m_ShadowPSO;

		Math::Vector3 m_SunDirection;
		ShadowCamera m_SunShadow;

		D3D12_CPU_DESCRIPTOR_HANDLE m_ExtraTextures[6];
		
	};

}
