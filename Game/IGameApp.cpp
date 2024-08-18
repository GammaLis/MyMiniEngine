#include "IGameApp.h"
#include "MyWindow.h"
#include "Graphics.h"
#include "CommandContext.h"
#include "Effects.h"
#include "PostEffects.h"
#include "TextRenderer.h"	// TextContext
#include "TextureManager.h"	// Graphics::s_TextureManager
#include "GpuBuffer.h"
#include "GameTimer.h"
#include "GameInput.h"
#include "Model.h"
#include <sstream>
#include <windowsX.h>

#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")

using namespace MyDirectX;

struct alignas(16) ConstantBuffer
{
	XMFLOAT3 _Color;
};

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
	m_Input = std::make_unique<GameInput>();
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

	// Add GameInput Init
	m_Input->Init(hwnd);

	InitAssets();

	return true;
}

void IGameApp::OnResize(UINT width, UINT height, bool minimized)
{
	m_Gfx->Resize(width, height);
	// No need to resize, display resolution != native resolution
	// Effects::Resize(width, height);
}

void IGameApp::Update(float deltaTime)
{
	HWND hwnd = m_Window->GetWindow();
	m_Input->Update(hwnd, deltaTime);
}

void IGameApp::Render()
{
	// m_Gfx->Clear();

	RenderTriangle();
}

void IGameApp::RenderUI()
{
	auto& uiContext = GraphicsContext::Begin(L"Render UI");

	auto& overlayBuffer = Graphics::s_BufferManager.m_OverlayBuffer;
	uiContext.TransitionResource(overlayBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
	uiContext.ClearColor(overlayBuffer);
	uiContext.SetRenderTarget(overlayBuffer.GetRTV());
	uiContext.SetViewportAndScissor(0, 0, overlayBuffer.GetWidth(), overlayBuffer.GetHeight());

	CustomUI(uiContext);

	uiContext.Finish();
}

void IGameApp::Cleanup()
{
	// Be sure GPU finished drawing
	m_Gfx->Terminate();

	CleanCustom();

	m_Input->Shutdown();

	m_Gfx->Shutdown();	
}

int IGameApp::Run()
{
	m_Window->Show();

	m_Timer->Reset();

	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))	// Note: not 'hwnd'
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
			
			PostProcess();

			RenderUI();

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

	case WM_SIZE:
	{
		RECT windowRect = {};
		GetWindowRect(hwnd, &windowRect);

		RECT clientRect = {};
		GetClientRect(hwnd, &clientRect);
		OnResize(clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, wParam == SIZE_MINIMIZED);
		return 0;
	}
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

void IGameApp::InitAssets()
{
	InitViewportAndScissor();

	// a basic triangle
	InitPipelineStates();

	InitGeometryBuffers();

	InitCustom();
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
	Graphics::s_TextureManager.Init(L"Textures/");
	m_Model->Create(Graphics::s_Device);

	ConstantBuffer constantBuffer;
	constantBuffer._Color = XMFLOAT3(0.6f, 0.6f, 0.6f);
	m_ConstantBuffer.Create(Graphics::s_Device, L"ConstantBuffer", 1, sizeof(constantBuffer), &constantBuffer);
}

void IGameApp::InitCustom()
{

}

void IGameApp::PostProcess()
{

}

void IGameApp::CustomUI(GraphicsContext &context)
{
	
}

void IGameApp::CleanCustom()
{
	m_Model->Cleanup();

	m_ConstantBuffer.Destroy();
}

void IGameApp::InitViewportAndScissor()
{
	const auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;

	uint32_t bufferWidth = colorBuffer.GetWidth();
	uint32_t bufferHeight = colorBuffer.GetHeight();
	// main viewport
	m_MainViewport.TopLeftX = m_MainViewport.TopLeftY = 0.0f;
	m_MainViewport.Width = (float)bufferWidth;
	m_MainViewport.Height = (float)bufferHeight;
	m_MainViewport.MinDepth = 0.0f;
	m_MainViewport.MaxDepth = 1.0f;
	// main scissor
	m_MainScissor.left = 0;
	m_MainScissor.top = 0;
	m_MainScissor.right = (LONG)bufferWidth;
	m_MainScissor.bottom = (LONG)bufferHeight;
}

void IGameApp::InitPipelineStates()
{
	// 1.empty root signature
	m_EmptyRS.Finalize(Graphics::s_Device, L"EmptyRootSignature", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// 2.common root signature
	m_CommonRS.Reset(5, 2);
	m_CommonRS[0].InitAsConstants(0, 4);
	m_CommonRS[1].InitAsConstantBuffer(1);
	m_CommonRS[2].InitAsConstantBuffer(2);
	// m_CommonRS[3].InitAsConstantBuffer(3, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_CommonRS[3].InitAsConstants(3, 8, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_CommonRS[4].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 2, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_CommonRS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearWrapDesc);
	m_CommonRS.InitStaticSampler(1, Graphics::s_CommonStates.SamplerPointClampDesc);
	m_CommonRS.Finalize(Graphics::s_Device, L"CommonRS", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// 3.basic triangle root signature
	m_BasicTriangleRS.Reset(2, 1);
	m_BasicTriangleRS[0].InitAsConstantBuffer(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_BasicTriangleRS[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_BasicTriangleRS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearWrapDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_BasicTriangleRS.Finalize(Graphics::s_Device, L"BasicTriangleRS", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	const auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	// or directly use backbuffer
	// const auto& colorBuffer = m_Gfx->GetRenderTarget();
	const auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
	DXGI_FORMAT colorFormat = colorBuffer.GetFormat();
	DXGI_FORMAT depthFormat = depthBuffer.GetFormat();

	D3D12_INPUT_ELEMENT_DESC basicInputElements[] =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA},
		{"COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA},

		// DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM
		// Choose different format
		// DXGI_FORMAT_B8G8R8A8_UNORM		// XMCOLOR
		// DXGI_FORMAT_R10G10B10A2_UNORM		// XMXDECN4
		// DXGI_FORMAT_R32G32B32A32_FLOAT	// XMFLOAT4
	};

	m_BasicTrianglePSO.SetRootSignature(m_BasicTriangleRS);	// m_EmptyRS
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
	
	auto &colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	// Or directly use backbuffer
	// auto& colorBuffer = m_Gfx->GetRenderTarget();

	auto bufferWidth = colorBuffer.GetWidth(), bufferHeight = colorBuffer.GetHeight();

	// Need flushImmediate. ClearRenderTargetView need state 'D3D12_RESOURCE_STATE_RENDER_TARGET'.
	gfxContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);

	gfxContext.SetRootSignature(m_BasicTriangleRS);	// m_EmptyRS
	gfxContext.SetPipelineState(m_BasicTrianglePSO);
	gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	/**
		MSDN ClearRenderTarget
		the debug layer will issue an error if the subresources referenced by the view are not in the appropriate state.
	For ClearRenderTargetView the state must be D3D12_RESOURCE_STATE_RENDER_TARGET.
	*/
	gfxContext.ClearColor(colorBuffer);		// colorBuffer's state needs to be D3D12_RESOURCE_STATE_RENDER_TARGET
	D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] =
	{
		colorBuffer.GetRTV()
	};
	gfxContext.SetRenderTargets(_countof(rtvs), rtvs);
	gfxContext.SetViewportAndScissor(0, 0, bufferWidth, bufferHeight);

	gfxContext.SetConstantBuffer(0, m_ConstantBuffer.GetGpuVirtualAddress());
	// Use CommandContext to set DescriptorHeap
	// gfxContext.SetDescriptorHeap()
	// gfxContext.SetDescriptorTable()
	gfxContext.SetDynamicDescriptor(1, 0, m_Model->GetDefaultSRV());

	auto vertexBufferView = m_Model->m_VertexBuffer.VertexBufferView();
	gfxContext.SetVertexBuffer(0, vertexBufferView);
	auto indexBufferView = m_Model->m_IndexBuffer.IndexBufferView();
	gfxContext.SetIndexBuffer(indexBufferView);
	gfxContext.DrawIndexed(m_Model->m_IndexCount);

	// Or directly use backbuffer
	// gfxContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_PRESENT);

	gfxContext.Finish();
}
