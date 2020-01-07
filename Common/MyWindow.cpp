#include "MyWindow.h"
#include "resource.h"
#include "MyApp.h"

using namespace MyDirectX;

LRESULT CALLBACK MyDirectX::MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// forward hwnd on because we can get messages (e.g. WM_CREATE)
	// before CreateWindow returns, and thus before mMainWnd is valid
	return MyApp::GetApp()->MsgProc(hwnd, msg, wParam, lParam);
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

	// ÏÔÊ¾´°¿Ú
	ShowWindow(m_HWnd, SW_SHOWDEFAULT);
	UpdateWindow(m_HWnd);

	return true;
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
	WNDCLASSEXW wcex = {};
	wcex.cbSize = sizeof(WNDCLASSEXW);			// the size, in bytes, of the structure
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
	UINT width = m_Width, height = m_Height;
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


