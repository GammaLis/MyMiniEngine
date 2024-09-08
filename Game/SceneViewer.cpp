#include "SceneViewer.h"

#include "BindlessDeferred.h"
#include "Graphics.h"
#include "CommandContext.h"
#include "Scenes/AssimpImporter.h"
#include "Math/GLMath.h"

#include "PostEffects.h"
#include "Scenes/DebugPass.h"
#include "MSAAFilter.h"

#include "ProfilingScope.h"

using namespace MyDirectX;
using namespace MFalcor;

#define USE_SPONZA_GLTF 1
static bool s_EnableDebugPass = false;

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
	{
		m_VisibilityRendering ? RenderVisibility(gfx) : RenderDeferred(gfx);
	}
	else
	{
		if (m_EnableMSAAFilter)
			RenderDynamic(gfx);
		else
			RenderForward(gfx);
	}

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

	// MSAA
	if (m_EnableMSAAFilter)
	{
		m_MSAAFilter = std::make_shared<MSAAFilter>();
		m_MSAAFilter->Init(Graphics::s_Device, m_MainScene.get());
	}

	// Post process
	{
		m_PostEffects = std::make_shared<PostEffects>();
		m_PostEffects->m_CommonStates.EnableAdaption = false;
		m_PostEffects->m_CommonStates.EnableBloom = false;
		m_PostEffects->m_CommonStates.EnableHDR = true;
		m_PostEffects->Init(Graphics::s_Device);
	}

	if (s_EnableDebugPass)
	{
		m_DebugPass = std::make_shared<DebugPass>();
		m_DebugPass->Init();
	}
}

void SceneViewer::CleanCustom()
{
	if (m_MSAAFilter) 
	{
		m_MSAAFilter->Destroy();
		m_MSAAFilter = nullptr;
	}

	if (m_PostEffects)
	{
		m_PostEffects->Shutdown();
		m_PostEffects = nullptr;
	}

	if (m_DebugPass)
	{
		m_DebugPass->Cleanup();
		m_DebugPass = nullptr;
	}
	
	m_MainScene->Clean();
}

void SceneViewer::RenderForward(GraphicsContext& gfx)
{
	ProfilingScope profilingScope(L"Render Forward", gfx);
	
	m_MainScene->BeginDrawing(gfx);

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;

	gfx.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
	gfx.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);

	gfx.ClearColor(colorBuffer);
	gfx.ClearDepthAndStencil(depthBuffer);
	gfx.SetViewportAndScissor(m_MainViewport, m_MainScissor);

	// camera
	[[maybe_unused]]
	const auto pCamera = m_MainScene->GetCamera();
	auto &viewUniformParams = m_MainScene->m_ViewUniformParams;
	auto &viewMatrix = viewUniformParams.viewMat;
	auto &projMatrix = viewUniformParams.projMat;
	auto &viewProjMatrix = viewUniformParams.viewProjMat;
	MFalcor::Vector3 camPos = MFalcor::Vector3(viewUniformParams.camPos.x, viewUniformParams.camPos.y, viewUniformParams.camPos.z);

	// Frustum culling
	{
		ProfilingScope profilingFrustumCulling(L"Frustum Culling", gfx);
		
		m_MainScene->FrustumCulling(gfx.GetComputeContext(), viewMatrix, projMatrix, AlphaMode::kOPAQUE);
		m_MainScene->FrustumCulling(gfx.GetComputeContext(), viewMatrix, projMatrix, AlphaMode::kMASK);
		m_MainScene->FrustumCulling(gfx.GetComputeContext(), viewMatrix, projMatrix, AlphaMode::kBLEND);
	}

	// Z-prepass
	gfx.SetRenderTargets(0, nullptr, depthBuffer.GetDSV());
	{
		ProfilingScope profilingPreprocess(L"Preprocess", gfx);
		
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
		ProfilingScope profilingOcclusionCulling(L"Occlusion Culling", gfx);
		
		// update Hi-Z buffer
		m_MainScene->UpdateHiZBuffer(gfx.GetComputeContext(), *m_Gfx);
		m_MainScene->OcclusionCulling(gfx.GetComputeContext(), viewMatrix, projMatrix);
	}

	// lights
	m_MainScene->SetAmbientLight(Vector3(0.2f, 0.6f, 0.6f));
	m_MainScene->SetCommonLights(gfx, m_IndirectRendering ? (UINT)CommonIndirectRSId::CBLights : (UINT)CommonRSId::CBLights);

	// normal pass
	gfx.SetRenderTarget(colorBuffer.GetRTV(), depthBuffer.GetDSV());
	if (m_IndirectRendering)
	{
		ProfilingScope profilingMainRender(L"Main Render", gfx);
		
		gfx.SetRootSignature(m_MainScene->m_CommonIndirectRS);

		m_MainScene->IndirectRender(gfx, m_MainScene->m_OpaqueIndirectPSO, AlphaMode::kOPAQUE);
		m_MainScene->IndirectRender(gfx, m_MainScene->m_MaskIndirectPSO, AlphaMode::kMASK);
		m_MainScene->IndirectRender(gfx, m_MainScene->m_TransparentIndirectPSO, AlphaMode::kBLEND);
	}
	else
	{
		ProfilingScope profilingMainRender(L"Main Render", gfx);
		
		gfx.SetRootSignature(m_MainScene->m_CommonRS);

		m_MainScene->Render(gfx);
	}
}

void SceneViewer::RenderDeferred(GraphicsContext& gfx)
{
	ProfilingScope profilingScope(L"Render Deferred", gfx);
	
	m_MainScene->BeginDrawing(gfx);

	// sun shadow
	{
		ProfilingScope profiling(L"Render Shadow", gfx);
		m_MainScene->RenderSunShadows(gfx);	
	}

	// camera
	[[maybe_unused]]
	const auto pCamera = m_MainScene->GetCamera();

	auto &viewUniformParams = m_MainScene->m_ViewUniformParams;
	auto &viewMatrix = viewUniformParams.viewMat;
	auto &projMatrix = viewUniformParams.projMat;
	auto &viewProjMatrix = viewUniformParams.viewProjMat;
	MFalcor::Vector3 camPos = MFalcor::Vector3(viewUniformParams.camPos.x, viewUniformParams.camPos.y, viewUniformParams.camPos.z);

	// frustum culling
	{
		ProfilingScope profiling(L"Frustum Culling", gfx);
		
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

	{
		ProfilingScope profiling(L"GBuffer Rendering", gfx);
		
		m_MainScene->RenderToGBuffer(gfx, m_MainScene->m_OpaqueGBufferPSO, AlphaMode::kOPAQUE);
		m_MainScene->RenderToGBuffer(gfx, m_MainScene->m_MaskGBufferPSO, AlphaMode::kMASK);
	}

	// deferred rendering
	ComputeContext& computeContext = gfx.GetComputeContext();	

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	gfx.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
	gfx.ClearColor(colorBuffer);

	{
		ProfilingScope profiling(L"Deferred Lighting", gfx);
		
		computeContext.SetRootSignature(m_MainScene->m_DeferredCSRS, false);
		m_MainScene->DeferredRender(computeContext, m_MainScene->m_DeferredCSPSO);
	}

	// transparent
}

void SceneViewer::RenderVisibility(GraphicsContext& commandContext)
{
	ProfilingScope profilingScope(L"Render Visibility", commandContext);
	
	// camera
	[[maybe_unused]]
	const auto pCamera = m_MainScene->GetCamera();

	auto& viewUniformParams = m_MainScene->m_ViewUniformParams;
	auto& viewMatrix = viewUniformParams.viewMat;
	auto& projMatrix = viewUniformParams.projMat;
	[[maybe_unused]]
	auto& viewProjMatrix = viewUniformParams.viewProjMat;
	[[maybe_unused]]
	MFalcor::Vector3 camPos = MFalcor::Vector3(viewUniformParams.camPos.x, viewUniformParams.camPos.y, viewUniformParams.camPos.z);

	// Gradient detection (use prev Luma buffer)
	{
		ComputeContext& computeContext = commandContext.GetComputeContext();

		ProfilingScope profiling(L"Calc Gradient", computeContext);

		m_MainScene->CalcGradient(computeContext);
	}

	// frustum culling
	{
		ProfilingScope profiling(L"Frustum Culling", commandContext);
		
		m_MainScene->FrustumCulling(commandContext.GetComputeContext(), viewMatrix, projMatrix, AlphaMode::kOPAQUE);
		m_MainScene->FrustumCulling(commandContext.GetComputeContext(), viewMatrix, projMatrix, AlphaMode::kMASK);
		m_MainScene->FrustumCulling(commandContext.GetComputeContext(), viewMatrix, projMatrix, AlphaMode::kBLEND);
	}
	
	// draw visibility
	{
		ProfilingScope profiling(L"Preprocess", commandContext);
		
		m_MainScene->PrepareVisibilityBuffer(commandContext);
		commandContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);
		commandContext.SetRootSignature(m_MainScene->m_VisibilityRS, false);
		m_MainScene->BeginIndexDrawing(commandContext);
		m_MainScene->RenderVisibilityBuffer(commandContext, AlphaMode::kOPAQUE);
	}
	
	// visibility compute
	{
		ComputeContext& computeContext = commandContext.GetComputeContext();

		ProfilingScope profiling(L"Visibility Compute", computeContext);
		
		computeContext.SetRootSignature(m_MainScene->m_VisibilityRS, false);
		m_MainScene->VisibilityCompute(computeContext, m_MainScene->m_VisibilityComputePSO);
	}

}

void SceneViewer::RenderDynamic(GraphicsContext & commandContext)
{
	ProfilingScope profilingScope( L"Dynamic Rendering", commandContext);
	
	m_MSAAFilter->BeginRendering(commandContext, m_MainScene.get());
	{
		m_MainScene->BeginDrawing(commandContext);
		
		// MSAA
		commandContext.SetPipelineState(m_MSAAFilter->m_DepthOnlyPSO);
		m_MainScene->RenderDynamic(commandContext, m_MSAAFilter.get());

		// Resolve
		m_MSAAFilter->Resolve(commandContext);
	}
	m_MSAAFilter->EndRendering(commandContext);
}

void SceneViewer::PostProcess()
{
	if (m_PostEffects)
	{
		m_PostEffects->Render();
	}

	if (!m_DebugPass)
		return;

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	uint32_t w = colorBuffer.GetWidth(), h = colorBuffer.GetHeight();

	// DebugPass
	auto &gfx = GraphicsContext::Begin(L"DebugColorBuffer");
	{
		ProfilingScope profiling(L"Debug GradientBuffer", gfx);

		auto& gradientBuffer = m_MainScene->m_BindlessDeferred->GetGradientBuffer();

		gfx.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
		gfx.TransitionResource(gradientBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		gfx.SetRenderTarget(colorBuffer.GetRTV());
		gfx.SetViewportAndScissor(0, 0, w, h);

		m_DebugPass->Render(gfx, gradientBuffer.GetSRV());
	}
	gfx.Finish();
}
