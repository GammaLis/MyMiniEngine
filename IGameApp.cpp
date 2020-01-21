#include "IGameApp.h"
#include "MyWindow.h"
#include "Graphics.h"
#include "CommandContext.h"
#include "GpuBuffer.h"
#include "GameTimer.h"
#include "Model.h"
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
	m_Model = std::make_unique<Model>();

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

	InitAssets();

	return true;
}

void IGameApp::OnResize()
{

}

void IGameApp::Update(float deltaTime)
{

}

void IGameApp::Render()
{
	// m_Gfx->Clear();

	RenderTriangle();
}

void IGameApp::Cleanup()
{
	// TO DO
	m_Gfx->Terminate();

	m_Model->Cleanup();

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
			
			float deltaTime = m_Timer->DeltaTime();
			Update(deltaTime);

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
	// a basic triangle
	InitPipelineStates();

	InitGeometryBuffers();
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

// 
void IGameApp::InitGeometryBuffers()
{
	m_Model->Create(Graphics::s_Device);
}

void IGameApp::InitPipelineStates()
{
	m_EmptyRS.Finalize(Graphics::s_Device, L"EmptyRootSignature", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	const auto& colorBuffer = Graphics::s_ResourceManager.m_SceneColorBuffer;
	// 或者 直接画到 backbuffer
	// const auto& colorBuffer = m_Gfx->GetRenderTarget();
	const auto& depthBuffer = Graphics::s_ResourceManager.m_SceneDepthBuffer;
	DXGI_FORMAT colorFormat = colorBuffer.GetFormat();
	DXGI_FORMAT depthFormat = depthBuffer.GetFormat();

	D3D12_INPUT_ELEMENT_DESC basicInputElements[] =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA},
		{"COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA},

		// DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM
		// 测试不同格式，
		// DXGI_FORMAT_B8G8R8A8_UNORM 对应 XMCOLOR
		// DXGI_FORMAT_R10G10B10A2_UNORM 对应 XMXDECN4
		// DXGI_FORMAT_R32G32B32A32_FLOAT 对应 XMFLOAT4
	};

	m_BasicTrianglePSO.SetRootSignature(m_EmptyRS);
	m_BasicTrianglePSO.SetInputLayout(_countof(basicInputElements), basicInputElements);
	m_BasicTrianglePSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	m_BasicTrianglePSO.SetVertexShader(Graphics::s_ShaderManager.m_BasicTriangleVS);
	m_BasicTrianglePSO.SetPixelShader(Graphics::s_ShaderManager.m_BasicTrianglePS);
	m_BasicTrianglePSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerDefault);
	m_BasicTrianglePSO.SetBlendState(Graphics::s_CommonStates.BlendDisable);
	m_BasicTrianglePSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateDisabled);
	m_BasicTrianglePSO.SetSampleMask(0xFFFFFFFF);
	m_BasicTrianglePSO.SetRenderTargetFormats(1, &colorFormat, depthFormat);
	m_BasicTrianglePSO.Finalize(Graphics::s_Device);

}

void IGameApp::RenderTriangle()
{
	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");
	
	auto &colorBuffer = Graphics::s_ResourceManager.m_SceneColorBuffer;
	// 或者 直接画到 backbuffer
	// auto& colorBuffer = m_Gfx->GetRenderTarget();

	auto bufferWidth = colorBuffer.GetWidth(), bufferHeight = colorBuffer.GetHeight();

	// 这里需要 flushImmediate，ClearRenderTargetView 需要rt处于D3D12_RESOURCE_STATE_RENDER_TARGET状态
	gfxContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);

	gfxContext.SetRootSignature(m_EmptyRS);
	gfxContext.SetPipelineState(m_BasicTrianglePSO);
	gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	/**
		MSDN ClearRenderTarget
		the debug layer will issue an error if the subresources referenced by the view are not in the appropriate state.
	For ClearRenderTargetView the state must be D3D12_RESOURCE_STATE_RENDER_TARGET.
	*/
	gfxContext.ClearColor(colorBuffer);		// 这里 就开始需要 colorBuffer 处于D3D12_RESOURCE_STATE_RENDER_TARGET状态了
	D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] =
	{
		colorBuffer.GetRTV()
	};
	gfxContext.SetRenderTargets(_countof(rtvs), rtvs);

	gfxContext.SetViewportAndScissor(0, 0, bufferWidth, bufferHeight);

	auto vertexBufferView = m_Model->m_VertexBuffer.VertexBufferView();
	gfxContext.SetVertexBuffer(0, vertexBufferView);
	auto indexBufferView = m_Model->m_IndexBuffer.IndexBufferView();
	gfxContext.SetIndexBuffer(indexBufferView);
	gfxContext.DrawIndexed(m_Model->m_IndexCount);

	// 如果直接 画到 backbuffer，注释下面
	// gfxContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_PRESENT);

	gfxContext.Finish();
}
