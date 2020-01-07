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

		static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	private:
		int RegisterWindowClass();
		int CreateWindowInstance();

		HINSTANCE m_HInstance;
		HWND m_HWnd = NULL;

		UINT m_Width;
		UINT m_Height;
		const wchar_t* m_Title;
	};
}
