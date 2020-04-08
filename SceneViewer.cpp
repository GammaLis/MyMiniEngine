#include "SceneViewer.h"
#include "Graphics.h"
#include "CommandContext.h"

using namespace MyDirectX;

SceneViewer::SceneViewer(HINSTANCE hInstance, const wchar_t* title, UINT width, UINT height)
	: IGameApp(hInstance, title, width, height)
{
}

void SceneViewer::Update(float deltaTime)
{
	IGameApp::Update(deltaTime);

	m_MainScene->Update(deltaTime);
}

void SceneViewer::Render()
{
	GraphicsContext& gfx = GraphicsContext::Begin(L"Rendering");

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;

	gfx.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
	gfx.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);

	gfx.ClearColor(colorBuffer);
	gfx.ClearDepthAndStencil(depthBuffer);
	gfx.SetRenderTarget(colorBuffer.GetRTV(), depthBuffer.GetDSV());
	gfx.SetViewportAndScissor(m_MainViewport, m_MainScissor);
	
	m_MainScene->BeginRendering(gfx);
	const auto pCamera = m_MainScene->GetCamera();
	m_MainScene->SetRenderCamera(gfx, MFalcor::Cast(pCamera->GetViewProjMatrix()), MFalcor::Cast(pCamera->GetPosition()));

	m_MainScene->Render(gfx);

	gfx.Finish();
}

void SceneViewer::InitPipelineStates()
{
}

void SceneViewer::InitGeometryBuffers()
{
}

void SceneViewer::InitCustom()
{
	// m_MainScene->Create(Graphics::s_Device, "Models/buster_drone.gltf");
	m_MainScene = MFalcor::Scene::Create();
	m_MainScene->Init(Graphics::s_Device, "Models/buster_drone.gltf", m_Input.get());	// sponza.obj, SponzaGLTF
}

void SceneViewer::PostProcess()
{

}

void SceneViewer::CleanCustom()
{
	m_MainScene->Clean();
}
