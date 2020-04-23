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
	if (m_DeferredRendering)
		RenderDeferred();
	else
		RenderForward();
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
	//MFalcor::Matrix4x4 scale = MFalcor::MMATH::scale(MFalcor::Vector3(100.0f, 100.0f, 100.0f));
	//instanceMats.emplace_back(scale);
	m_MainScene->Init(Graphics::s_Device, "Models/sponza.obj", m_Input.get(), instanceMats);	// sponza.obj, SponzaGLTF
}

void SceneViewer::RenderForward()
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
	gfx.SetRootSignature(m_IndirectRendering ? m_MainScene->m_CommonIndirectRS : m_MainScene->m_CommonRS);

	// camera
	const auto pCamera = m_MainScene->GetCamera();
	m_MainScene->SetRenderCamera(gfx, MFalcor::Cast(pCamera->GetViewProjMatrix()), MFalcor::Cast(pCamera->GetPosition()),
		m_IndirectRendering ? (UINT)CommonIndirectRSId::CBPerCamera : (UINT)CommonRSId::CBPerCamera);

	// lights
	m_MainScene->SetAmbientLight(Vector3(0.8f, 0.2f, 0.2f));
	m_MainScene->SetCommonLights(gfx, m_IndirectRendering ?
	(UINT)CommonIndirectRSId::CBLights : (UINT)CommonRSId::CBLights);

	if (m_IndirectRendering)
		m_MainScene->IndirectRender(gfx, m_MainScene->m_OpaqueIndirectPSO);
	else
		m_MainScene->Render(gfx);

	gfx.Finish();
}

void SceneViewer::RenderDeferred()
{
	GraphicsContext& gfx = GraphicsContext::Begin(L"Rendering");

	// gbuffer
	m_MainScene->PrepareGBuffer(gfx, m_MainViewport, m_MainScissor);
	m_MainScene->BeginRendering(gfx);
	gfx.SetRootSignature(m_MainScene->m_GBufferRS);

	// camera
	const auto pCamera = m_MainScene->GetCamera();
	m_MainScene->SetRenderCamera(gfx, MFalcor::Cast(pCamera->GetViewProjMatrix()), MFalcor::Cast(pCamera->GetPosition()),
		(UINT)GBufferRSId::CBPerCamera);

	m_MainScene->RenderToGBuffer(gfx, m_MainScene->m_GBufferPSO);

	// deferred rendering
	ComputeContext& computeContext = gfx.GetComputeContext();	

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	gfx.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
	gfx.ClearColor(colorBuffer);
	
	computeContext.SetRootSignature(m_MainScene->m_DeferredCSRS);
	m_MainScene->DeferredRender(computeContext, m_MainScene->m_DeferredCSPSO);

	gfx.Finish();
}

void SceneViewer::PostProcess()
{

}

void SceneViewer::CleanCustom()
{
	m_MainScene->Clean();
}
