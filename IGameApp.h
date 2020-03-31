#pragma once
#include "pch.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "GpuBuffer.h"
#include "MyWindow.h"

namespace MyDirectX
{
	class MyWindow;
	class Graphics;
	class GameTimer;
	class GameInput;
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
		std::unique_ptr<GameInput> m_Input;
		std::unique_ptr<Model> m_Model;

		RootSignature m_EmptyRS;
		RootSignature m_CommonRS;

		UINT m_Width;
		UINT m_Height;
		const wchar_t* m_Title;

		D3D12_VIEWPORT m_MainViewport;
		D3D12_RECT m_MainScissor;

	private:
		void CalculateFrameStats();

		HINSTANCE m_HInstance;

#pragma region Hello, Triangle
		// 子类复写下列初始化函数
		void InitViewportAndScissor();
		virtual void InitPipelineStates();
		virtual void InitGeometryBuffers();
		virtual void InitCustom();

		// 后处理，从Render当中分离，子类进行自定义
		virtual void PostProcess();
		
		virtual void CustomUI(GraphicsContext &context);

		virtual void CleanCustom();

		void RenderTriangle();

		RootSignature m_BasicTriangleRS;
		GraphicsPSO m_BasicTrianglePSO;
		StructuredBuffer m_ConstantBuffer;
#pragma endregion
		
	};
}
