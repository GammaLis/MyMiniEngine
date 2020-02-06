#pragma once
#include "pch.h"
#include "RootSignature.h"
#include "PipelineState.h"

#define MINI_ENGINE

namespace MyDirectX
{
	class MyWindow;
	class Graphics;
	class GameTimer;
	class Model;
	class GraphicsContext;

	enum class ObjectFilter
	{
		kNone = 0x0,
		kOpaque = 0x1,
		kCutout = 0x2,
		kTransparent = 0x4,
		kAll = 0xF,
	};

	class IGameApp
	{
	public:
		static IGameApp* GetApp();

		IGameApp(HINSTANCE hInstance, const wchar_t* title = L"Hello, World!", UINT width = SCR_WIDTH, UINT height = SCR_HEIGHT);
		virtual ~IGameApp();

		virtual bool Init();
		virtual void OnResize();
		virtual void Update(float deltaTime);
		virtual void Render();
		virtual void RenderUI();
		virtual void Cleanup();

		int Run();

		// convenience overrides for handling mouse event
		virtual void OnMouseDown(WPARAM btnState, int x, int y) {  }
		virtual void OnMouseUp(WPARAM btnState, int x, int y) {  }
		virtual void OnMouseMove(WPARAM btnState, int x, int y) {  }

		virtual LRESULT CALLBACK MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	protected:
		virtual void InitAssets();

		static IGameApp* m_App;

		std::unique_ptr<MyWindow> m_Window;
		std::unique_ptr<Graphics> m_Gfx;
		std::unique_ptr<GameTimer> m_Timer;
		std::unique_ptr<Model> m_Model;

	private:
		void CalculateFrameStats();

		HINSTANCE m_HInstance;

#pragma region Hello, Triangle
		virtual void InitPipelineStates();
		virtual void InitGeometryBuffers();
		virtual void InitCustom();
		
		virtual void CustomUI(GraphicsContext &context);

		void RenderTriangle();

		RootSignature m_EmptyRS;
		GraphicsPSO m_BasicTrianglePSO;
#pragma endregion

	protected:
		UINT m_Width;
		UINT m_Height;
		const wchar_t* m_Title;

		D3D12_VIEWPORT m_MainViewport;
		D3D12_RECT m_MainScissor;
	};
}

