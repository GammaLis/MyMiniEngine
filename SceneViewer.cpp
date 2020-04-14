#include "SceneViewer.h"
#include "Graphics.h"
#include "CommandContext.h"
#include "Scenes/AssimpImporter.h"
#include "Math/GLMath.h"

using namespace MyDirectX;
using namespace MFalcor;

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

	m_MainScene->BeginRendering(gfx, m_IndirectRendering);

	const uint32_t cameraIdx = 2;
	const uint32_t cameraIndirectIdx = 1;	

	const auto pCamera = m_MainScene->GetCamera();
	m_MainScene->SetRenderCamera(gfx, MFalcor::Cast(pCamera->GetViewProjMatrix()), MFalcor::Cast(pCamera->GetPosition()), 
		m_IndirectRendering ? cameraIndirectIdx : cameraIdx);

	if (m_IndirectRendering)
		m_MainScene->IndirectRender(gfx);
	else
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
	MFalcor::InstanceMatrices instanceMats;
	//MFalcor::Matrix4x4 scale = MFalcor::MMATH::scale(MFalcor::Vector3(50.0f, 50.0f, 50.0f));
	//instanceMats.emplace_back(scale);
	m_MainScene->Init(Graphics::s_Device, "Models/buster_drone.gltf", m_Input.get(), instanceMats);	// sponza.obj, SponzaGLTF
}

void SceneViewer::PostProcess()
{

}

void SceneViewer::CleanCustom()
{
	m_MainScene->Clean();
}
