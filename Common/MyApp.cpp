#include "MyApp.h"
#include "MyWindow.h"
#include "DeviceResources.h"
#include "GameTimer.h"
#include <sstream>
#include <windowsX.h>
#include <d3dcompiler.h>

#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
// msdn DXGI_DEBUG_ID
// DXGI_DEBUG_ID globally unique identifier(GUID) values that identify producers of degbug messages
// IDXGIInfoQueue
// to use any of these GUID valus, include DXGIDebug.h in your code and link to dxguid.lib
#pragma comment(lib, "d3dcompiler.lib")

using namespace MyDirectX;

MyApp* MyApp::m_App = nullptr;


MyApp* MyApp::GetApp()
{
	return m_App;
}

MyApp::MyApp(HINSTANCE hInstance, const wchar_t* title, UINT width, UINT height)
	: m_HInstance{hInstance}, m_Title{title}, m_Width{width}, m_Height{height}
{
	m_Window = std::make_unique<MyWindow>(hInstance, title, width, height);

	m_DeviceResources = std::make_unique<DeviceResources>();

	m_Timer = std::make_unique<GameTimer>();

	// only one MyApp can be constructed
	assert(m_App == nullptr);
	m_App = this;
}

MyApp::~MyApp()
{
	ReleaseD3DObjects();
}

bool MyApp::Init()
{
	if (!m_Window->Init())
	{
		return false;
	}

	HWND hwnd = m_Window->GetWindow();

	if (!m_DeviceResources->Init(hwnd, m_Width, m_Height))
	{
		return false;
	}

	CacheD3DObjects();

	InitAssets();

	return true;
}

void MyApp::OnResize()
{
	// TO DO
}

void MyApp::Update()
{
	// TO DO
}

void MyApp::Render()
{
	m_DeviceResources->Prepare();

	m_DeviceResources->Clear();

	m_DeviceResources->Present();
}

int MyApp::Run()
{
	m_Timer->Reset();

	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))	// 这里不要写成hwnd，老是忘记
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			m_Timer->Tick();
			CalculateFrameStats();

			Update();

			Render();
		}
	}

	return (int)msg.wParam;
}

LRESULT MyApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;

	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
		OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;

	case WM_MOUSEMOVE:
		OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;

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

void MyApp::InitAssets()
{
	// TO DO
}

void MyApp::CalculateFrameStats()
{
	// Code computes the average frames per second, and also the 
	// average time it takes to render one frame.  These stats 
	// are appended to the window caption bar.

	static int frameCnt = 0;
	static float timeElapsed = 0.0f;

	frameCnt++;

	// Compute averages over one second period.
	if ((m_Timer->TotalTime() - timeElapsed) >= 1.0f)
	{
		float fps = (float)frameCnt; // fps = frameCnt / 1
		float mspf = 1000.0f / fps;

		std::wostringstream outs;
		outs.precision(6);
		outs << m_Title << L"    "
			<< L"FPS: " << fps << L"    "
			<< L"Frame Time: " << mspf << L" (ms)";
		SetWindowText(m_Window->GetWindow(), outs.str().c_str());

		// Reset for next average.
		frameCnt = 0;
		timeElapsed += 1.0f;
	}
}

// 缓存部分常用DirectX对象，供子类使用
void MyApp::CacheD3DObjects()
{
	pDevice = m_DeviceResources->GetD3DDevice();

	pCmdList = m_DeviceResources->GetCommandList();
}

void MyApp::ReleaseD3DObjects()
{
	pDevice = nullptr;

	pCmdList = nullptr;
}
