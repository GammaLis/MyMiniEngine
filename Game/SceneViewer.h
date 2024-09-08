#pragma once
#include "IGameApp.h"
#include "Scenes/Scene.h"

namespace MyDirectX
{
	class GraphicsContext;
	class PostEffects;
	class DebugPass;
	class MSAAFilter;

	class SceneViewer : public IGameApp
	{
	public:
		SceneViewer(HINSTANCE hInstance, const wchar_t* title = L"Hello, World!", UINT width = SCR_WIDTH, UINT height = SCR_HEIGHT);

		virtual void Update(float deltaTime) override;
		virtual void Render() override;

		Graphics* GetGraphics() const { return m_Gfx.get(); }
		GameInput* GetInput() const { return m_Input.get(); }

	private:
		virtual void InitPipelineStates() override;
		virtual void InitGeometryBuffers() override;
		virtual void InitCustom() override;
		virtual void CleanCustom() override;

		void RenderForward(GraphicsContext &commandContext);
		void RenderDeferred(GraphicsContext &commandContext);
		void RenderVisibility(GraphicsContext &commandContext);
		void RenderDynamic(GraphicsContext &commandContext);

		virtual void PostProcess() override;

		MFalcor::Scene::SharedPtr m_MainScene;

		// root signature & PSOs
		RootSignature m_RootSig;
		GraphicsPSO m_DepthPSO;
		GraphicsPSO m_CutoutDepthPSO;
		GraphicsPSO m_ModelPSO;
		GraphicsPSO m_ShadowPSO;

		bool m_IndirectRendering = true;
		bool m_DeferredRendering = true;
		bool m_VisibilityRendering = false;

		bool m_EnableMSAAFilter = true;
		std::shared_ptr<MSAAFilter> m_MSAAFilter;

		// Post effects
		std::shared_ptr<PostEffects> m_PostEffects;

		std::shared_ptr<DebugPass> m_DebugPass;
	};

}
