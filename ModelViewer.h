#pragma once
#include "IGameApp.h"
#include "Camera.h"
#include "ShadowCamera.h"
#include "CameraController.h"
#include "GameInput.h"
#include "Skybox.h"

namespace MyDirectX
{
	class GraphicsContext;

	enum class RSId
	{
		kMeshConstants = 0,
		kMaterialConstants,
		kMaterialSRVs,
		kMaterialSamplers,
		kCommonCBV,
		kCommonSRVs,
		kSkinMatrices,

		kNum
	};

	class ModelViewer final : public IGameApp
	{
	public:
		ModelViewer(HINSTANCE hInstance, const char *modelName, const wchar_t* title = L"Hello, World!", UINT width = SCR_WIDTH, UINT height = SCR_HEIGHT);

		virtual void Update(float deltaTime) override;
		virtual void Render() override;

		virtual void Raytrace();

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

		static const UINT c_MaxRayRecursion = 2;

	private:
		virtual void InitPipelineStates() override;
		virtual void InitGeometryBuffers() override;
		virtual void InitCustom() override;
		virtual void CleanCustom() override;

		virtual void PostProcess() override;

		void RenderLightShadows(GraphicsContext& gfxContext);
		void RenderObjects(GraphicsContext& gfxContext, const Math::Matrix4 viewProjMat, ObjectFilter filter = ObjectFilter::kAll);
		void CreateParticleEffects();

		void InitRaytracingStateObjects();
		void RaytraceDiffuse(GraphicsContext& gfxContext);
		void RaytraceShadows(GraphicsContext& gfxContext);
		void RaytraceReflections(GraphicsContext& gfxContext);

		Math::Camera m_Camera;
		std::unique_ptr<CameraController> m_CameraController;
		Math::Matrix4 m_ViewProjMatrix;

		// root signature & PSOs
		RootSignature m_RootSig;
		GraphicsPSO m_DepthPSO{ L"Depth PSO" };
		GraphicsPSO m_CutoutDepthPSO{ L"Cutout Depth PSO" };
		GraphicsPSO m_ModelPSO{ L"Color PSO" };
		GraphicsPSO m_CutoutModelPSO{ L"Cutout Color PSO" };
		GraphicsPSO m_ShadowPSO{ L"Shadow PSO" };
		GraphicsPSO m_CutoutShadowPSO{ L"Cutout Shadow PSO" };

		// 临时设置，后面需要移到别处 -20-2-21
		RootSignature m_LinearDepthRS;
		ComputePSO m_LinearDepthCS;

		Math::Vector3 m_SunDirection;
		ShadowCamera m_SunShadow;

		D3D12_CPU_DESCRIPTOR_HANDLE m_ExtraTextures[6];
		
		// skybox
		Skybox m_Skybox;

		const char* m_ModelName = nullptr;
	};

}
