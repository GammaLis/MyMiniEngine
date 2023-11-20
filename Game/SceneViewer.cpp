#include "SceneViewer.h"
#include "Graphics.h"
#include "CommandContext.h"
#include "Scenes/AssimpImporter.h"
#include "Math/GLMath.h"

using namespace MyDirectX;
using namespace MFalcor;

#define USE_SPONZA_GLTF 1

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

	m_MainScene->BeginRendering(gfx);

	if (m_DeferredRendering)
		m_VisibilityRendering ? RenderVisibility(gfx) : RenderDeferred(gfx);
	else
		RenderForward(gfx);

	m_MainScene->EndRendering(gfx);

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
#if 0
	m_MainScene = Scene::Create(Graphics::s_Device, "Models/buster_drone.gltf", this);
#else
	m_MainScene = MFalcor::Scene::Create();
	MFalcor::InstanceMatrices instanceMats;
#if USE_SPONZA_GLTF
	MFalcor::Matrix4x4 scale = MFalcor::MMATH::scale(MFalcor::Vector3(100.0f, 100.0f, 100.0f));
	instanceMats.emplace_back(scale);
#endif
	m_MainScene->Init(Graphics::s_Device, USE_SPONZA_GLTF ? "Models/SponzaGLTF.gltf" : "Models/sponza.obj", this, instanceMats);	// sponza.obj, SponzaGLTF
#endif
}

void SceneViewer::CleanCustom()
{
	m_MainScene->Clean();
}

void SceneViewer::RenderForward(GraphicsContext& gfx)
{
	m_MainScene->BeginDrawing(gfx);

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;

	gfx.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
	gfx.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);

	gfx.ClearColor(colorBuffer);
	gfx.ClearDepthAndStencil(depthBuffer);
	gfx.SetViewportAndScissor(m_MainViewport, m_MainScissor);

	// camera
	const auto pCamera = m_MainScene->GetCamera();
	auto &viewUniformParams = m_MainScene->m_ViewUniformParams;
	auto &viewMatrix = viewUniformParams.viewMat;
	auto &projMatrix = viewUniformParams.projMat;
	auto &viewProjMatrix = viewUniformParams.viewProjMat;
	MFalcor::Vector3 camPos = MFalcor::Vector3(viewUniformParams.camPos.x, viewUniformParams.camPos.y, viewUniformParams.camPos.z);

	// Frustum culling
	{
		m_MainScene->FrustumCulling(gfx.GetComputeContext(), viewMatrix, projMatrix, AlphaMode::kOPAQUE);
		m_MainScene->FrustumCulling(gfx.GetComputeContext(), viewMatrix, projMatrix, AlphaMode::kMASK);
		m_MainScene->FrustumCulling(gfx.GetComputeContext(), viewMatrix, projMatrix, AlphaMode::kBLEND);
	}

	// Z-prepass
	gfx.SetRenderTargets(0, nullptr, depthBuffer.GetDSV());
	{
		// Opaques

		gfx.SetRootSignature(m_IndirectRendering ? m_MainScene->m_CommonIndirectRS : m_MainScene->m_CommonRS);
		m_MainScene->SetRenderCamera(gfx, viewProjMatrix, camPos, m_IndirectRendering ? (UINT)CommonIndirectRSId::CBPerCamera : (UINT)CommonRSId::CBPerCamera);
		m_MainScene->IndirectRender(gfx, m_MainScene->m_DepthIndirectPSO, AlphaMode::kOPAQUE);

		// Masked
		
		// gfx.SetRenderTargets(0, nullptr, depthBuffer.GetDSV());
		gfx.SetRootSignature(m_IndirectRendering ? m_MainScene->m_CommonIndirectRS : m_MainScene->m_CommonRS);
		m_MainScene->SetRenderCamera(gfx, viewProjMatrix, camPos, m_IndirectRendering ? (UINT)CommonIndirectRSId::CBPerCamera : (UINT)CommonRSId::CBPerCamera);
		m_MainScene->IndirectRender(gfx, m_MainScene->m_DepthClipIndirectPSO, AlphaMode::kMASK);
	}

	// FIXME: there are some issues now, not enabled!
	if (m_MainScene->m_EnableOcclusionCulling)
	{
		// update Hi-Z buffer
		m_MainScene->UpdateHiZBuffer(gfx.GetComputeContext(), *m_Gfx);
		m_MainScene->OcclusionCulling(gfx.GetComputeContext(), viewMatrix, projMatrix);
	}

	// lights
	m_MainScene->SetAmbientLight(Vector3(0.8f, 0.2f, 0.2f));
	m_MainScene->SetCommonLights(gfx, m_IndirectRendering ? (UINT)CommonIndirectRSId::CBLights : (UINT)CommonRSId::CBLights);

	// normal pass
	gfx.SetRenderTarget(colorBuffer.GetRTV(), depthBuffer.GetDSV());
	if (m_IndirectRendering)
	{
		gfx.SetRootSignature(m_MainScene->m_CommonIndirectRS);

		m_MainScene->IndirectRender(gfx, m_MainScene->m_OpaqueIndirectPSO, AlphaMode::kOPAQUE);
		m_MainScene->IndirectRender(gfx, m_MainScene->m_MaskIndirectPSO, AlphaMode::kMASK);
		m_MainScene->IndirectRender(gfx, m_MainScene->m_TransparentIndirectPSO, AlphaMode::kBLEND);
	}
	else
	{
		gfx.SetRootSignature(m_MainScene->m_CommonRS);

		m_MainScene->Render(gfx);
	}
}

void SceneViewer::RenderDeferred(GraphicsContext& gfx)
{
	m_MainScene->BeginDrawing(gfx);

	// sun shadow
	m_MainScene->RenderSunShadows(gfx);

	// camera
	const auto pCamera = m_MainScene->GetCamera();

	auto &viewUniformParams = m_MainScene->m_ViewUniformParams;
	auto &viewMatrix = viewUniformParams.viewMat;
	auto &projMatrix = viewUniformParams.projMat;
	auto &viewProjMatrix = viewUniformParams.viewProjMat;
	MFalcor::Vector3 camPos = MFalcor::Vector3(viewUniformParams.camPos.x, viewUniformParams.camPos.y, viewUniformParams.camPos.z);

	// frustum culling
	{
		m_MainScene->FrustumCulling(gfx.GetComputeContext(), viewMatrix, projMatrix, AlphaMode::kOPAQUE);
		m_MainScene->FrustumCulling(gfx.GetComputeContext(), viewMatrix, projMatrix, AlphaMode::kMASK);
		m_MainScene->FrustumCulling(gfx.GetComputeContext(), viewMatrix, projMatrix, AlphaMode::kBLEND);
	}

	// gbuffer
	m_MainScene->PrepareGBuffer(gfx);
	gfx.SetViewportAndScissor(m_MainViewport, m_MainScissor);
	gfx.SetRootSignature(m_MainScene->m_GBufferRS, false);

	// camera
	m_MainScene->SetRenderCamera(gfx, viewProjMatrix, camPos, (UINT)GBufferRSId::CBPerCamera);

	m_MainScene->RenderToGBuffer(gfx, m_MainScene->m_OpaqueGBufferPSO, AlphaMode::kOPAQUE);
	m_MainScene->RenderToGBuffer(gfx, m_MainScene->m_MaskGBufferPSO, AlphaMode::kMASK);

	// deferred rendering
	ComputeContext& computeContext = gfx.GetComputeContext();	

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	gfx.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
	gfx.ClearColor(colorBuffer);
	
	computeContext.SetRootSignature(m_MainScene->m_DeferredCSRS, false);
	m_MainScene->DeferredRender(computeContext, m_MainScene->m_DeferredCSPSO);

	// transparent
}

void SceneViewer::RenderVisibility(GraphicsContext& gfx)
{
	// camera
	const auto pCamera = m_MainScene->GetCamera();

	auto& viewUniformParams = m_MainScene->m_ViewUniformParams;
	auto& viewMatrix = viewUniformParams.viewMat;
	auto& projMatrix = viewUniformParams.projMat;
	auto& viewProjMatrix = viewUniformParams.viewProjMat;
	MFalcor::Vector3 camPos = MFalcor::Vector3(viewUniformParams.camPos.x, viewUniformParams.camPos.y, viewUniformParams.camPos.z);

	// frustum culling
	{
		m_MainScene->FrustumCulling(gfx.GetComputeContext(), viewMatrix, projMatrix, AlphaMode::kOPAQUE);
		m_MainScene->FrustumCulling(gfx.GetComputeContext(), viewMatrix, projMatrix, AlphaMode::kMASK);
		m_MainScene->FrustumCulling(gfx.GetComputeContext(), viewMatrix, projMatrix, AlphaMode::kBLEND);
	}
	// draw visibility
	{
		m_MainScene->PrepareVisibilityBuffer(gfx);
		gfx.SetViewportAndScissor(m_MainViewport, m_MainScissor);
		gfx.SetRootSignature(m_MainScene->m_VisibilityRS, false);
		m_MainScene->BeginIndexDrawing(gfx);
		m_MainScene->RenderVisibilityBuffer(gfx, AlphaMode::kOPAQUE);
	}
	// visibility compute
	ComputeContext& computeContext = gfx.GetComputeContext();
	{
		computeContext.SetRootSignature(m_MainScene->m_VisibilityRS, false);
		m_MainScene->VisibilityCompute(computeContext, m_MainScene->m_VisibilityComputePSO);
	}
}

void SceneViewer::PostProcess()
{

}
