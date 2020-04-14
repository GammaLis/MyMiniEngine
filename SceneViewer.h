#pragma once
#include "IGameApp.h"
#include "Scenes/Scene.h"

namespace MyDirectX
{
	class SceneViewer : public IGameApp
	{
	public:
		SceneViewer(HINSTANCE hInstance, const wchar_t* title = L"Hello, World!", UINT width = SCR_WIDTH, UINT height = SCR_HEIGHT);

		virtual void Update(float deltaTime) override;
		virtual void Render() override;

	private:
		virtual void InitPipelineStates() override;
		virtual void InitGeometryBuffers() override;
		virtual void InitCustom() override;

		virtual void PostProcess() override;

		virtual void CleanCustom() override;

		MFalcor::Scene::SharedPtr m_MainScene;

		// root signature & PSOs
		RootSignature m_RootSig;
		GraphicsPSO m_DepthPSO;
		GraphicsPSO m_CutoutDepthPSO;
		GraphicsPSO m_ModelPSO;
		GraphicsPSO m_ShadowPSO;

		bool m_IndirectRendering = true;
	};

}
