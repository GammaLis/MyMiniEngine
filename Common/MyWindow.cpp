#include "MyWindow.h"
#include "resource.h"
#include "MyApp.h"
#include "Game/IGameApp.h"

#define MINI_ENGINE

using namespace MyDirectX;

LRESULT CALLBACK MyDirectX::MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// forward hwnd on because we can get messages (e.g. WM_CREATE)
	// before CreateWindow returns, and thus before mMainWnd is valid
#ifdef MINI_ENGINE
	return IGameApp::GetApp()->MsgProc(hwnd, msg, wParam, lParam);
#else
	return MyApp::GetApp()->MsgProc(hwnd, msg, wParam, lParam);
#endif
}

const wchar_t* MyWindow::ClassName = L"MyWindow";

MyWindow::MyWindow(HINSTANCE hInstance, const wchar_t* title, UINT width, UINT height)
	:m_HInstance{hInstance}, m_Title{title}, m_Width{width}, m_Height{height}
{

}

MyWindow::~MyWindow()
{
	if (m_HWnd)
	{
		DestroyWindow(m_HWnd);
	}
	UnregisterClass(ClassName, m_HInstance);
}

bool MyWindow::Init()
{
	if (RegisterWindowClass() != 0)
	{
		return false;
	}

	if (CreateWindowInstance() != 0)
	{
		return false;
	}

	SetWindowLongPtr(m_HWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

#if 0
	// Show after app inited
	ShowWindow(m_HWnd, SW_SHOWDEFAULT);
	UpdateWindow(m_HWnd);
#endif

	return true;
}

// Show window
void MyWindow::Show(int nCmdShow)
{
	ShowWindow(m_HWnd, nCmdShow);
}

// convert a styled window into a fullscreen borderless window and back again
void MyWindow::ToggleFullscreenWindow(IDXGISwapChain* pSwapchain)
{
	using Microsoft::WRL::ComPtr;

	if (m_bFullscreenMode)
	{
		// full screen -> windowed screen
		// restore the window's attribute and size
		SetWindowLong(m_HWnd, GWL_STYLE, m_WindowStyle);
		SetWindowPos(m_HWnd, 
			HWND_NOTOPMOST, 
			m_WindowRect.left, m_WindowRect.top, m_WindowRect.right - m_WindowRect.left, m_WindowRect.bottom - m_WindowRect.top,
			SWP_FRAMECHANGED | SWP_NOACTIVATE);
		ShowWindow(m_HWnd, SW_NORMAL);
	}
	else
	{
		// save the old window rect so we can restore it when existing fullscreen mode
		GetWindowRect(m_HWnd, &m_WindowRect);

		// make the window borderless so that the client area can fill the screen
		SetWindowLong(m_HWnd, GWL_STYLE, m_WindowStyle & ~(WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU | WS_THICKFRAME));

		RECT fullscreenWindowRect;
		if (pSwapchain)
		{
			// get the settings of the display on which the app's window is currently displaed
			ComPtr<IDXGIOutput> pOutput;
			ThrowIfFailed(pSwapchain->GetContainingOutput(&pOutput));
			DXGI_OUTPUT_DESC outputDesc;
			ThrowIfFailed(pOutput->GetDesc(&outputDesc));
			fullscreenWindowRect = outputDesc.DesktopCoordinates;
		}
		else
		{
			throw com_exception(S_FALSE);
		}

		SetWindowPos(m_HWnd, HWND_TOPMOST,
			fullscreenWindowRect.left, fullscreenWindowRect.top, 
			fullscreenWindowRect.right - fullscreenWindowRect.left, fullscreenWindowRect.bottom - fullscreenWindowRect.top,
			SWP_FRAMECHANGED | SWP_NOACTIVATE);
		ShowWindow(m_HWnd, SW_MAXIMIZE);
	}

	m_bFullscreenMode = !m_bFullscreenMode;
}

void MyWindow::SetWindowZorderToTopMost(bool setToTopMost)
{
	RECT windowRect;
	GetWindowRect(m_HWnd, &windowRect);

	SetWindowPos(m_HWnd, (setToTopMost) ? HWND_TOPMOST : HWND_NOTOPMOST,
		windowRect.left, windowRect.top,
		windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
		SWP_FRAMECHANGED | SWP_NOACTIVATE);
}

LRESULT CALLBACK MyWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	auto myWindow = reinterpret_cast<MyWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

	switch (msg)
	{
	case WM_KEYUP:
		if (wParam == VK_ESCAPE)
		{
			PostQuitMessage(0);
		}
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

int MyWindow::RegisterWindowClass()
{
	WNDCLASSEX wcex = {};
	wcex.cbSize = sizeof(WNDCLASSEX);			// the size, in bytes, of the structure
	wcex.style = CS_HREDRAW | CS_VREDRAW;		// the class style, CS_HREDRAW specifies that the entire window is redrawn 
												// if a movement or size adjustment changes the width of the client area
	wcex.hInstance = m_HInstance;				// a handle to the instance that contains the window procudure for the class
	wcex.lpfnWndProc = MainWndProc; 	// MainWndProc	WindowProc	// a pointer to the window procedure that will handle window messages for any window created using this window class
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);	// a handle to the clas background brush
	wcex.hIcon = (HICON)LoadImage(m_HInstance, MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, 32, 32, 0);
	wcex.hIconSm = (HICON)LoadImage(m_HInstance, MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, 16, 16, 0);
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.lpszClassName = ClassName;
	wcex.lpszMenuName = nullptr;

	if (!RegisterClassEx(&wcex))
	{
		OutputDebugString(L"Create Window Class Failed!");
		return 1;
	}

	return 0;
}

int MyWindow::CreateWindowInstance()
{
	LONG width = m_Width, height = m_Height;
	RECT rect = { 0, 0, width, height };
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

	HWND hwnd = CreateWindowEx(
		0,
		ClassName,
		m_Title,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		rect.right - rect.left,
		rect.bottom - rect.top,
		nullptr,
		nullptr,
		m_HInstance,
		nullptr
	);

	if (!hwnd)
	{
		OutputDebugString(L"Create Window Failed!");
		return 1;
	}

	m_HWnd = hwnd;

	return 0;
}
