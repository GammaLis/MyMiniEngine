#pragma once

#include "pch.h"

namespace MyDirectX
{
	LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	class MyWindow
	{
	public:
		static const wchar_t *ClassName;

		MyWindow(HINSTANCE hInstance, const wchar_t* title = L"Hello, World!", UINT width = SCR_WIDTH, UINT height = SCR_HEIGHT);
		MyWindow(const MyWindow&) = delete;
		MyWindow& operator=(const MyWindow&) = delete;
		~MyWindow();

		bool Init();

		HWND GetWindow() const
		{
			return m_HWnd;
		}

		const wchar_t* GetWindowTitle() const
		{
			return m_Title;
		}

		void Show(int nCmdShow = SW_SHOWDEFAULT);
		void ToggleFullscreenWindow(IDXGISwapChain *pOutput = nullptr);
		void SetWindowZorderToTopMost(bool setToTopMost);
		bool IsFullscreen() { return m_bFullscreenMode;}

		static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	private:
		int RegisterWindowClass();
		int CreateWindowInstance();

		HINSTANCE m_HInstance;
		HWND m_HWnd = NULL;

		UINT m_Width;
		UINT m_Height;
		RECT m_WindowRect;
		const wchar_t* m_Title;

		// settings
		bool m_bFullscreenMode = false;
		UINT m_WindowStyle = WS_OVERLAPPEDWINDOW;
	};
}
