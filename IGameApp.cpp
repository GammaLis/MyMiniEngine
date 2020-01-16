#include "IGameApp.h"
#include "MyWindow.h"
#include "Graphics.h"
#include "GameTimer.h"
#include <sstream>
#include <windowsX.h>

#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")

using namespace MyDirectX;

IGameApp* IGameApp::m_App = nullptr;

IGameApp* IGameApp::GetApp()
{
	return m_App;
}

IGameApp::IGameApp(HINSTANCE hInstance, const wchar_t* title, UINT width, UINT height)
	: m_HInstance{ hInstance }, m_Title{ title }, m_Width{ width }, m_Height{ height }
{
	m_Window = std::make_unique<MyWindow>(hInstance, title, width, height);
	m_Gfx = std::make_unique<Graphics>();
	m_Timer = std::make_unique<GameTimer>();

	// only one IGameApp can be constructed
	assert(m_App == nullptr);
	m_App = this;
}

IGameApp::~IGameApp()
{
}

bool IGameApp::Init()
{
	if (!m_Window->Init())
	{
		return false;
	}

	HWND hwnd = m_Window->GetWindow();

	m_Gfx->Init(hwnd, m_Width, m_Height);

	return true;
}

void IGameApp::OnResize()
{

}

void IGameApp::Update()
{

}

void IGameApp::Render()
{
	m_Gfx->Clear();
}

void IGameApp::Cleanup()
{
	// TO DO
	m_Gfx->Terminate();
	m_Gfx->Shutdown();
}

int IGameApp::Run()
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

			m_Gfx->Present();
		}
	}

	return (int)msg.wParam;
}

LRESULT IGameApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
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

void IGameApp::InitAssets()
{
}

void IGameApp::CalculateFrameStats()
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
