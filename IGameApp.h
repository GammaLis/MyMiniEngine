#pragma once
#include "pch.h"

#define MINI_ENGINE

namespace MyDirectX
{
	class MyWindow;
	class Graphics;
	class GameTimer;

	class IGameApp
	{
	public:
		static IGameApp* GetApp();

		IGameApp(HINSTANCE hInstance, const wchar_t* title = L"Hello, World!", UINT width = SCR_WIDTH, UINT height = SCR_HEIGHT);
		virtual ~IGameApp();

		virtual bool Init();
		virtual void OnResize();
		virtual void Update();
		virtual void Render();
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

	private:
		void CalculateFrameStats();

		HINSTANCE m_HInstance;

	protected:
		UINT m_Width;
		UINT m_Height;
		const wchar_t* m_Title;
	};
}

