#include "ModelViewer.h"
#include "Graphics.h"
#include "CommandContext.h"
#include "TextureManager.h"
#include "Model.h"
#include "Effects.h"
#include "ProfilingScope.h"
#include "UniformBuffers.h"

#include "Camera.h"
#include "ShadowCamera.h"
#include "CameraController.h"

#include "Skybox.h"
#include "Scenes/DebugPass.h"

// Compiled shaders
#include "DepthViewerVS.h"
#include "DepthViewerPS.h"
#include "ModelViewerVS.h"
#include "ModelViewerPS.h"

// TODO: put somewhere else
#include "LinearizeDepthCS.h"

/// Raytracing
#include "Raytracing/RaytracingHlslCompat.h"
#include "ModelViewerRaytracing.h"

#include "RayGenShaderLib.h"
#include "RayGenShaderSSRLib.h"
#include "RayGenShaderShadowsLib.h"
#include "MissShaderLib.h"
#include "MissShadowsLib.h"
#include "HitShaderLib.h"
#include "DiffuseHitShaderLib.h"

#include "SimplePathTracing.h"

#include "BufferCopyCS.h"

// Reservoirs
#include "ReservoirSampling.h"
#include "ReSTIRGI.h"

#include "ClearReservoirs.h"
#include "ReSTIRWithDirectLighting.h"
#include "ReSTIR_TraceDiffuse.h"

using namespace MyDirectX;

/// Uniforms
using namespace MyDirectX::UniformBuffers;

struct VSConstants
{
	Math::Matrix4 _ModelToProjection;
	Math::Matrix4 _ModelToShadow;
	XMFLOAT3 _CamPos;
};

struct alignas(16) PSConstants
{
	Math::Vector3 _SunDirection;
	Math::Vector3 _SunLight;
	Math::Vector3 _AmbientLight;
	float _ShadowTexelSize[4];

	float _InvTileDim[4];
	uint32_t _TileCount[4];	// x,y有效，后面字节对齐
	uint32_t _FirstLightIndex[4];

	uint32_t _FrameIndexMod2;
};


/// Raytracing
struct alignas(16) HitShaderConstants
{
	Math::Vector3 _SunDirection;
	Math::Vector3 _SunLight;
	Math::Vector3 _AmbientLight;
	float _ShadowTexelSize[4];
	Math::Matrix4 _ModelToShadow;

	uint32_t _MaxBounces = 1;
	uint32_t _IsReflection = 0;
	uint32_t _UseShadowRays = 0;
};

static constexpr uint32_t c_MaxFrameIndex = 255;
static const auto c_BackgroundColor = DirectX::Colors::White;
static RaytracingMode s_RaytracingMode = RaytracingMode::Off; // ReSTIRWithDirectLights ReSTIRGI

DynamicCB g_DynamicCB = {};
HitShaderConstants g_HitShaderConstants = {};

// Reservoirs
struct FReservoir
{
	uint  LightData;
	float TargetPdf;
	float Weight;
	float M;
};
constexpr uint32_t c_MaxReservoirs = 1; // number of reservoirs per pixel to allocate


using BatchElements = ModelViewer::BatchElements;

void Cull(BatchElements &visibleMeshes, const Math::Camera& camera, const std::vector<const Model*>& models)
{
	visibleMeshes.clear();

	const auto& frustumWS = camera.GetWorldSpaceFrustum();

	// High 16bits - ModelId, low 16bits - MeshId (or SubmeshId)
	uint32_t index = 0;
	for (uint32_t i = 0, imax = (uint32_t)models.size(); i < imax; i++)
	{
		const auto& model = models[i];

		bool bVisible = frustumWS.IntersectBoundingBox(model->GetBoundingBox().min, model->GetBoundingBox().max);
		if (!bVisible)
			continue;

		index = i << 16;
		for (uint32_t j = 0; j < model->m_MeshCount; ++j)
		{
			const auto& mesh = model->m_Meshes[j];
			bVisible = frustumWS.IntersectBoundingBox(mesh.boundingBox.min, mesh.boundingBox.max);
			if (bVisible)
			{
				visibleMeshes.emplace_back(index | j);
			}
		}
	}
}


ModelViewer::ModelViewer(HINSTANCE hInstance, const char *modelName, const wchar_t* title, UINT width, UINT height)
	: IGameApp(hInstance, title, width, height)
	, m_RaytracingDescHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256)
{
	m_ModelName = modelName;

	m_Camera.reset(new Math::Camera);
	m_SunShadow.reset(new ShadowCamera);
	m_ReSTIRGI.reset(new ReSTIRGI());
}

ModelViewer::~ModelViewer() { }

/**
* The keys '0'...'6' can be used to cycle through different modes
* -Off, Full rasterization
* -Bary Rays, Primary rays that return the barycentric of the intersected triangle.
* -Refl Bary, Secondary reflection rays that return the barycentric of the intersected triangle.
* -Shadow Rays, Secondary shadow rays are fired and return black/white depending on if a hit is found.
* -Diffuse&ShadowMaps, Primary rays are fired that calculate diffuse lighting and use a rasterized shadow map.
* -Diffuse&ShadowRays, Fully-raytraced pass that shoots primary rays for diffuse lights and recursively fires shadow rays.
* -Reflection Rays, Hybrid pass that renders primary diffuse with rasterization and if the ground plane is detected, fires reflection rays.
*/
static bool bDenoise = true;
void ModelViewer::Update(float deltaTime)
{
	IGameApp::Update(deltaTime);

	auto& raytracingMode = s_RaytracingMode;
	if (m_Input->IsFirstPressed(DigitalInput::kKey_0))
		raytracingMode = RaytracingMode::Off;
	else if (m_Input->IsFirstPressed(DigitalInput::kKey_1))
		raytracingMode = RaytracingMode::Traversal;
	else if (m_Input->IsFirstPressed(DigitalInput::kKey_2))
		raytracingMode = RaytracingMode::SSR;
	else if (m_Input->IsFirstPressed(DigitalInput::kKey_3))
		raytracingMode = RaytracingMode::Shadows;
	else if (m_Input->IsFirstPressed(DigitalInput::kKey_4))
		raytracingMode = RaytracingMode::DiffuseWithShadowMaps;
	else if (m_Input->IsFirstPressed(DigitalInput::kKey_5))
		raytracingMode = RaytracingMode::DiffuseWithShadowRays;
	else if (m_Input->IsFirstPressed(DigitalInput::kKey_6))
		raytracingMode = RaytracingMode::Reflections;
	else if (m_Input->IsFirstPressed(DigitalInput::kKey_7) && m_bEnablePathTracing)
		raytracingMode = RaytracingMode::ReferencePathTracing;
	else if (m_Input->IsFirstPressed(DigitalInput::kKey_8) && m_bEnableReSTIRDI)
		raytracingMode = RaytracingMode::ReSTIRWithDirectLights;
	else if (m_Input->IsFirstPressed(DigitalInput::kKey_9) && m_bEnableReSTIRGI)
		raytracingMode = RaytracingMode::ReSTIRGI;

	bool bNeedDenoising = raytracingMode == RaytracingMode::ReSTIRWithDirectLights;
	Effects::s_Denoier.SetActive(bNeedDenoising);

	// Update camera params
	auto &cam = *m_Camera.get();
	{
		const auto& prevRotation = cam.GetRotation();
		const auto& prevPosition = cam.GetPosition();
		m_CameraController->Update(deltaTime);

		const auto& currRotation = cam.GetRotation();
		const auto& currPosition = cam.GetPosition();

		const auto& dr = XMVectorAbs(currRotation - prevRotation);
		const auto& dp = currPosition - prevPosition;

		bool bViewportChanged = false;
		bViewportChanged = !XMQuaternionEqual(currRotation, prevRotation);
		const float Eps = 1e-2f;
		if (std::abs(dp.GetX()) > Eps || std::abs(dp.GetY()) > Eps || std::abs(dp.GetZ()) > Eps)
			bViewportChanged = true;

		if (bViewportChanged)
			m_AccumulationIndex = -1;

		m_ViewProjMatrix = cam.GetViewProjMatrix();
	}

	if (raytracingMode != RaytracingMode::ReferencePathTracing)
	{
		m_AccumulationIndex = -1;
	}
	else
	{
		++m_AccumulationIndex;
	}

	float cosTheta = cosf(m_CommonStates.SunOrientation);
	float sinTheta = sinf(m_CommonStates.SunOrientation);
	float cosPhi = cosf(m_CommonStates.SunInclination * Math::Pi * 0.5f);
	float sinPhi = sinf(m_CommonStates.SunInclination * Math::Pi * 0.5f);
	m_SunDirection = Math::Normalize(Math::Vector3(cosTheta * cosPhi, sinPhi, sinTheta * cosPhi));

	//
	// We use viewport offsets to jitter sample positions from frame to frame (for TAA.)
	// D3D has a design quirk with fractional offsets such that the implicit scissor
	// region of a viewport is floor(TopLeftXY) and floor(TopLeftXY + WidthHeight), so
	// having a negative fractional top left, e.g. (-0.25, -0.25) would also shift the
	// BottomRight corner up by a whole integer.  One solution is to pad your viewport
	// dimensions with an extra pixel.  My solution is to only use positive fractional offsets,
	// but that means that the average sample position is +0.5, which I use when I disable
	// temporal AA.
	// 注：MS MiniEngine - 在Graphics::Present之后调用TemporalEffects::Update，为什么？ -20-2-21
	uint64_t frameIndex = m_Gfx->GetFrameCount();
	Effects::s_TemporalAA.Update(frameIndex);
	Effects::s_TemporalAA.GetJitterOffset(m_MainViewport.TopLeftX, m_MainViewport.TopLeftY);

	if (Effects::s_Denoier.IsActive())
	{
		if (!Effects::s_Denoier.HasInited())
			Effects::s_Denoier.Init(Graphics::s_Device);

		Effects::s_Denoier.Update(frameIndex);
	}

	// Viewport & Scissor
	const uint32_t bufferWidth = GfxStates::s_NativeWidth, bufferHeight = GfxStates::s_NativeHeight;
	
	//~ Begin DEBUG: (改变Viewport位置，尺寸，Scissor位置，尺寸)
	// m_MainViewport.TopLeftX = m_MainViewport.TopLeftY = 0.0f;
	// m_MainViewport.TopLeftX = 10;
	// m_MainViewport.TopLeftY = 10;
	//~ End DEBUG
	m_MainViewport.Width = (float)bufferWidth;
	m_MainViewport.Height = (float)bufferHeight;
	m_MainViewport.MinDepth = 0.0f;
	m_MainViewport.MaxDepth = 1.0f;

	m_MainScissor.left = 0;
	m_MainScissor.top = 0;
	m_MainScissor.right = (LONG)bufferWidth;
	m_MainScissor.bottom = (LONG)bufferHeight;

	// View uniforms
	{
		const auto& viewMat = cam.GetViewMatrix();
		const auto& projMat = cam.GetProjMatrix();
		const auto& invViewMat = Math::Invert(viewMat);
		const auto& invProjMat = Math::Invert(projMat);
#if 0
		Math::Matrix4 screenToViewMat = Math::Matrix4(
			Math::Vector4(1, 0, 0, 0),
			Math::Vector4(0, 1, 0, 0),
			Math::Vector4(0, 0, projMat.GetZ().GetZ(), -1),
			Math::Vector4(0, 0, projMat.GetW().GetZ(), 0)) * invProjMat;
#else
		Math::Matrix4 screenToViewMat = Math::Matrix4(
			Math::Vector4(1.0f/projMat.GetX().GetX(), 0, 0, 0),
			Math::Vector4(0, 1.0f/projMat.GetY().GetY(), 0, 0),
			Math::Vector4(0, 0, 1.0f, 0),
			Math::Vector4(0, 0, 0, 1.0f));
#endif
		Math::Matrix4 screenToWorldMat = invViewMat * screenToViewMat; // Transpose later
		g_ViewUniformBufferParameters.ViewMatrix = Math::Transpose(viewMat);
		g_ViewUniformBufferParameters.ProjMatrix = Math::Transpose(projMat);
		g_ViewUniformBufferParameters.ViewProjMatrix = Math::Transpose(cam.GetViewProjMatrix());
		g_ViewUniformBufferParameters.PrevViewProjMatrix = Math::Transpose(cam.GetPrevViewProjMatrix());
		g_ViewUniformBufferParameters.ReprojectMatrix = Math::Transpose(cam.GetReprojectionMatrix());
		g_ViewUniformBufferParameters.InvViewMatrix = Math::Transpose(invViewMat);
		g_ViewUniformBufferParameters.InvProjMatrix = Math::Transpose(invProjMat);
		g_ViewUniformBufferParameters.ScreenToViewMatrix = Math::Transpose(screenToViewMat);
		g_ViewUniformBufferParameters.ScreenToWorldMatrix = Math::Transpose(screenToWorldMat);
		g_ViewUniformBufferParameters.BufferSizeAndInvSize = Math::Vector4((float)bufferWidth, (float)bufferHeight, 1.0f / (float)bufferWidth, 1.0f /(float)bufferHeight);
		g_ViewUniformBufferParameters.InvDeviceZToWorldZTransform = Math::CreateInvDeviceZToWorldZTransform(projMat);
		g_ViewUniformBufferParameters.DebugColor = Math::Vector4(c_BackgroundColor);
		g_ViewUniformBufferParameters.CamPos = cam.GetPosition();

		float farClip = cam.GetFarClip(), nearClip = cam.GetNearClip();
		g_ViewUniformBufferParameters.ZNear = nearClip;
		g_ViewUniformBufferParameters.ZFar = farClip;
		g_ViewUniformBufferParameters.ZMagic = (farClip - nearClip) / nearClip;

		g_ViewUniformBufferParameters.FrameIndex = m_Gfx->GetFrameCount() & c_MaxFrameIndex;

		// FIXME: Use UploadBuffer seems run slower ???
		g_ViewUniformBuffer.CopyToGpu(&g_ViewUniformBufferParameters, sizeof(g_ViewUniformBufferParameters));
	}

	// Update particles
	ComputeContext& computeContext = ComputeContext::Begin(L"Particle Update");
	Effects::s_ParticleEffectManager.Update(computeContext, deltaTime);
	computeContext.Finish();
}

void ModelViewer::Render()
{
	auto frameIndex = m_Gfx->GetFrameCount();
	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
	auto& normalBuffer = Graphics::s_BufferManager.m_SceneNormalBuffer;
	auto& shadowBuffer = Graphics::s_BufferManager.m_ShadowBuffer;

	const auto &mainCamera = *m_Camera.get();

	PSConstants psConstants;
	psConstants._SunDirection = m_SunDirection;
	psConstants._SunLight = Math::Vector3(1.0f) * m_CommonStates.SunLightIntensity;
	psConstants._AmbientLight = Math::Vector3(1.0f) * m_CommonStates.AmbientIntensity;

	psConstants._ShadowTexelSize[0] = 1.0f / shadowBuffer.GetWidth();
	psConstants._ShadowTexelSize[1] = 1.0f / shadowBuffer.GetHeight();

	const auto& forwardPlusLighting = Effects::s_ForwardPlusLighting;
	psConstants._InvTileDim[0] = 1.0f / (float)forwardPlusLighting.m_LightGridDim;
	psConstants._InvTileDim[1] = 1.0f / (float)forwardPlusLighting.m_LightGridDim;

	psConstants._TileCount[0] = Math::DivideByMultiple(colorBuffer.GetWidth(), forwardPlusLighting.m_LightGridDim);
	psConstants._TileCount[1] = Math::DivideByMultiple(colorBuffer.GetHeight(), forwardPlusLighting.m_LightGridDim);
	psConstants._FirstLightIndex[0] = forwardPlusLighting.m_FirstConeLight;
	psConstants._FirstLightIndex[1] = forwardPlusLighting.m_FirstConeShadowedLight;
	psConstants._FrameIndexMod2 = frameIndex & 1;

	RaytracingMode rayTracingMode = s_RaytracingMode;
	const bool bSkipDiffusePass = rayTracingMode == RaytracingMode::DiffuseWithShadowMaps ||
		rayTracingMode == RaytracingMode::DiffuseWithShadowRays ||
		rayTracingMode == RaytracingMode::Traversal;
	const bool bSkipShadowMap = rayTracingMode == RaytracingMode::DiffuseWithShadowRays ||
		rayTracingMode == RaytracingMode::Traversal ||
		rayTracingMode == RaytracingMode::SSR;
	// ...

	// Culling first
	{
		std::vector<const Model*> models = { m_Model.get() };
		
		// Default batch list
		if (m_DefaultBatchList.empty())
		{
			uint32_t index = 0;
			for (uint32_t modelIndex = 0, modelNum = (uint32_t)models.size(); modelIndex < modelNum; modelIndex++) {
				const auto model = models[modelIndex];
				index = modelIndex << 16;

				for (uint32_t meshIndex = 0, meshNum = model->m_MeshCount; meshIndex < meshNum; meshIndex++) {
					m_DefaultBatchList.emplace_back( index | meshIndex);
				}
			}
		}

		// 0. Main camera
		// Always update
		if (m_MainCullingIndex == INDEX_NONE) 
		{
			m_MainCullingIndex = static_cast<uint32_t>(m_PassCullingResults.size());
			m_PassCullingResults.emplace_back();
		}

		auto &batchList = m_PassCullingResults[m_MainCullingIndex];
		Cull(batchList, mainCamera, models);
	}

	GraphicsContext &gfxContext = GraphicsContext::Begin(L"Scene Render");

	// Set the default state for command lists
	auto pfnSetupGraphicsState = [&]()
	{
		gfxContext.SetRootSignature(m_RootSig);
		gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		gfxContext.SetVertexBuffer(0, m_Model->m_VertexBuffer.VertexBufferView());
		gfxContext.SetIndexBuffer(m_Model->m_IndexBuffer.IndexBufferView());
	};
	pfnSetupGraphicsState();

	// 'MaxLights' shadow pass, one pass per frame
	RenderLightShadows(gfxContext);

	// Z prepass
	{
		ProfilingScope profilingScope(L"Render PreDepth", gfxContext);

		// Opaque 
		{
			gfxContext.SetDynamicConstantBufferView((uint32_t)RSId::kMaterialConstants, sizeof(psConstants), &psConstants);

			gfxContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
			gfxContext.ClearDepth(depthBuffer);

			gfxContext.SetDepthStencilTarget(depthBuffer.GetDSV());
			gfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);

			gfxContext.SetPipelineState(m_DepthPSO);
			RenderObjects(gfxContext, m_ViewProjMatrix, ObjectFilter::kOpaque, m_MainCullingIndex);
		}

		// Cutout
		{
			gfxContext.SetPipelineState(m_CutoutDepthPSO);
			RenderObjects(gfxContext, m_ViewProjMatrix, ObjectFilter::kCutout, m_MainCullingIndex);
		}
	}

	// 
	{
		// Linearize depth
		{
			ProfilingScope profilingScope(L"Compute Linear Depth", gfxContext);

			auto& computeContext = gfxContext.GetComputeContext();
			computeContext.SetRootSignature(m_LinearDepthRS);
			computeContext.SetPipelineState(m_LinearDepthCS);

			auto& linearDepth = Graphics::s_BufferManager.m_LinearDepth[frameIndex % 2];
			computeContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			computeContext.TransitionResource(linearDepth, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			
			float farClip = mainCamera.GetFarClip(), nearClip = mainCamera.GetNearClip();
			const float zMagic = (farClip - nearClip) / nearClip;
			computeContext.SetConstant(0, zMagic);
			computeContext.SetDynamicDescriptor(1, 0, depthBuffer.GetDepthSRV());
			computeContext.SetDynamicDescriptor(2, 0, linearDepth.GetUAV());
			computeContext.Dispatch2D(linearDepth.GetWidth(), linearDepth.GetHeight(), 16, 16);

		}

		// CS - fill light grid
		Effects::s_ForwardPlusLighting.FillLightGrid(gfxContext, mainCamera, frameIndex);
	}

	// Main render
	{
		gfxContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfxContext.TransitionResource(normalBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfxContext.ClearColor(colorBuffer);
		gfxContext.ClearColor(normalBuffer);

		pfnSetupGraphicsState();
		// Render shadow map
		{
			ProfilingScope profilingScope(L"Render Shadowmap", gfxContext);

			m_SunShadow->UpdateMatrix(-m_SunDirection, Math::Vector3(0, -500.0f, 0),
				Math::Vector3(m_CommonStates.ShadowDimX, m_CommonStates.ShadowDimY, m_CommonStates.ShadowDimZ),
				shadowBuffer.GetWidth(), shadowBuffer.GetHeight(), 16);

			shadowBuffer.BeginRendering(gfxContext);

			gfxContext.SetPipelineState(m_ShadowPSO);
			RenderObjects(gfxContext, m_SunShadow->GetViewProjMatrix(), ObjectFilter::kOpaque);
			gfxContext.SetPipelineState(m_CutoutShadowPSO);
			RenderObjects(gfxContext, m_SunShadow->GetViewProjMatrix(), ObjectFilter::kCutout);

			shadowBuffer.EndRendering(gfxContext);
		}

		// Render color
		{
			ProfilingScope profilingScope(L"Render Main Color", gfxContext);

			gfxContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);

			D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = { colorBuffer.GetRTV(), normalBuffer.GetRTV() };
			gfxContext.SetRenderTargets(ARRAYSIZE(rtvs), rtvs, depthBuffer.GetDSV_DepthReadOnly());
			gfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);

			gfxContext.SetDynamicConstantBufferView((uint32_t)RSId::kMaterialConstants, sizeof(psConstants), &psConstants);
			gfxContext.SetDynamicDescriptors((uint32_t)RSId::kCommonSRVs, 0, _countof(m_ExtraTextures), m_ExtraTextures);

			// Opaques
			gfxContext.SetPipelineState(m_ModelPSO);
			RenderObjects(gfxContext, m_ViewProjMatrix, ObjectFilter::kOpaque, m_MainCullingIndex);

			// Cutouts
			gfxContext.SetPipelineState(m_CutoutModelPSO);
			RenderObjects(gfxContext, m_ViewProjMatrix, ObjectFilter::kCutout, m_MainCullingIndex);
		}
	}

	// Skybox
	if (m_Skybox)
	{
		m_Skybox->Render(gfxContext, mainCamera);
	}

	// Effects
	{
		// Some systems generate a per-pixel velocity buffer to better track dynamic and skinned meshes.
		// everything is static in our scene, so we generate velocity from camera motion and the depth buffer.
		// a velocity buffer is necessary for all temporal effects (and motion blur)
		Effects::s_MotionBlur.GenerateCameraVelocityBuffer(gfxContext, mainCamera, frameIndex, true);

		Effects::s_TemporalAA.ResolveImage(gfxContext);

		// Particle effects
		auto& linearDepth = Graphics::s_BufferManager.m_LinearDepth[frameIndex % 2];
		Effects::s_ParticleEffectManager.Render(gfxContext, mainCamera, colorBuffer, depthBuffer, linearDepth);
	}
	
	// No ZPrepass
	/**
	{
		gfxContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfxContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
		gfxContext.ClearColor(colorBuffer);
		gfxContext.ClearDepth(depthBuffer);
		gfxContext.SetRenderTarget(colorBuffer.GetRTV(), depthBuffer.GetDSV());	// GetDSV_DepthReadOnly
		gfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);

		gfxContext.SetPipelineState(m_ModelPSO);

		gfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);

		RenderObjects(gfxContext, m_ViewProjMatrix, ObjectFilter::kOpaque);
	}
	*/

	// Raytracing
	if (rayTracingMode != RaytracingMode::Off)
		Raytrace(gfxContext);

	// Denoising
	if (Effects::s_Denoier.IsActive())
		Effects::s_Denoier.Render(gfxContext);

	// DEBUG
#if 1
	if (m_bEnableDebug)
	{
		gfxContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
		gfxContext.SetRenderTarget(colorBuffer.GetRTV());

		if (Effects::s_Denoier.IsActive())
		{
			m_DebugPass->Render(gfxContext, Effects::s_Denoier.GetDebugOutput());
		}
		else if (m_bEnableReSTIRGI)
		{
			m_DebugPass->Render(gfxContext, m_ReSTIRGI->m_Irradiance.GetSRV());
		}
	}
#endif

	// Update histories
	if (GfxStates::s_bEnableTemporalEffects)
	{
		if (!Effects::s_TemporalEffects.HasInited())
			Effects::s_TemporalEffects.Init(Graphics::s_Device);

		Effects::s_TemporalEffects.UpdateHistory(gfxContext);
	}

	gfxContext.Finish();
}

void ModelViewer::InitPipelineStates()
{
	SamplerDesc DefaultSamplerDesc;
	DefaultSamplerDesc.MaxAnisotropy = 8;

	SamplerDesc CubeMapSamplerDesc = DefaultSamplerDesc;
	// CubeMapSamplerDesc.MaxLOD = 6.0f;

	// Root signature
	m_RootSig.Reset((UINT)RSId::kNum, 3);
	m_RootSig[(UINT)RSId::kMeshConstants].InitAsConstantBuffer(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig[(UINT)RSId::kMaterialConstants].InitAsConstantBuffer(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[(UINT)RSId::kMaterialSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 10, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[(UINT)RSId::kMaterialSamplers].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0, 10, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[(UINT)RSId::kCommonCBV].InitAsConstants(1, 2);
	m_RootSig[(UINT)RSId::kCommonSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 10, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[(UINT)RSId::kSkinMatrices].InitAsBufferSRV(20, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig.InitStaticSampler(10, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig.InitStaticSampler(11, Graphics::s_CommonStates.SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig.InitStaticSampler(12, CubeMapSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig.Finalize(Graphics::s_Device, L"ModelViewer", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	DXGI_FORMAT colorFormat = Graphics::s_BufferManager.m_SceneColorBuffer.GetFormat();
	DXGI_FORMAT normalFormat = Graphics::s_BufferManager.m_SceneNormalBuffer.GetFormat();
	DXGI_FORMAT depthFormat = Graphics::s_BufferManager.m_SceneDepthBuffer.GetFormat();
	DXGI_FORMAT shadowFormat = Graphics::s_BufferManager.m_ShadowBuffer.GetFormat();

	// Input elements
	D3D12_INPUT_ELEMENT_DESC inputElements[] =
	{
		{ "POSITION",	0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",	0, DXGI_FORMAT_R32G32_FLOAT,	0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",		0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",	0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BITANGENT",	0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	// PSOs
	// Depth pso
	// 只渲染深度，允许深度读写，不需PixelShader，不需颜色绘制
	m_DepthPSO.SetRootSignature(m_RootSig);
	m_DepthPSO.SetInputLayout(_countof(inputElements), inputElements);
	m_DepthPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	m_DepthPSO.SetVertexShader(DepthViewerVS, sizeof(DepthViewerVS));
	m_DepthPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerDefault);	// RasterizerDefault RasterizerDefaultWireframe
	m_DepthPSO.SetBlendState(Graphics::s_CommonStates.BlendNoColorWrite);
	m_DepthPSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateReadWrite);
	m_DepthPSO.SetRenderTargetFormats(0, nullptr, depthFormat);
	m_DepthPSO.Finalize(Graphics::s_Device);

	// Depth-only shading but with alpha-testing
	m_CutoutDepthPSO = m_DepthPSO;
	m_CutoutDepthPSO.SetPixelShader(DepthViewerPS, sizeof(DepthViewerPS));
	m_CutoutDepthPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerTwoSided);
	m_CutoutDepthPSO.Finalize(Graphics::s_Device);

	// Depth-only but with a depth bias and/or render only backfaces
	m_ShadowPSO = m_DepthPSO;
	m_ShadowPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerShadow);
	m_ShadowPSO.SetRenderTargetFormats(0, nullptr, shadowFormat);
	m_ShadowPSO.Finalize(Graphics::s_Device);

	// Shadows with alpha testing
	m_CutoutShadowPSO = m_ShadowPSO;
	m_CutoutShadowPSO.SetPixelShader(DepthViewerPS, sizeof(DepthViewerPS));
	m_CutoutShadowPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerShadowTwoSided);
	m_CutoutShadowPSO.Finalize(Graphics::s_Device);

	DXGI_FORMAT modelViewerFormats[2] = { colorFormat, normalFormat };

	// Model viewer pso
	m_ModelPSO = m_DepthPSO;
	m_ModelPSO.SetVertexShader(ModelViewerVS, sizeof(ModelViewerVS));
	m_ModelPSO.SetPixelShader(ModelViewerPS, sizeof(ModelViewerPS));
	m_ModelPSO.SetBlendState(Graphics::s_CommonStates.BlendDisable);
	m_ModelPSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateTestEqual);
	m_ModelPSO.SetRenderTargetFormats(_countof(modelViewerFormats), modelViewerFormats, depthFormat);
	m_ModelPSO.Finalize(Graphics::s_Device);

	m_CutoutModelPSO = m_ModelPSO;
	m_CutoutModelPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerTwoSided);
	m_CutoutModelPSO.Finalize(Graphics::s_Device);

	// FIXME: 临时设置，后面需要放到别处 -20-2-21
	// Linear depth
	m_LinearDepthRS.Reset(3, 0);
	m_LinearDepthRS[0].InitAsConstants(0, 1);
	m_LinearDepthRS[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
	m_LinearDepthRS[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
	m_LinearDepthRS.Finalize(Graphics::s_Device, L"Linear Depth");

	m_LinearDepthCS.SetRootSignature(m_LinearDepthRS);
	m_LinearDepthCS.SetComputeShader(LinearizeDepthCS, sizeof(LinearizeDepthCS));
	m_LinearDepthCS.Finalize(Graphics::s_Device);
}

void ModelViewer::InitGeometryBuffers()
{
	ASSERT(m_ModelName != nullptr, "Model Name is null!");
	Graphics::s_TextureManager.Init(L"Textures/");
	m_Model->CreateFromAssimp(Graphics::s_Device, m_ModelName); // "Models/sponza.obj"
	ASSERT(m_Model->m_MeshCount > 0, "Model contains no meshes");
}

void ModelViewer::InitCustom()
{
	// Camera
	auto &mainCamera = *m_Camera.get();

	float modelRadius = Math::Length(m_Model->m_BoundingBox.max - m_Model->m_BoundingBox.min) * 0.5f;
	const Math::Vector3 eye = (m_Model->m_BoundingBox.min + m_Model->m_BoundingBox.max) * 0.5f + Math::Vector3(modelRadius * 0.5f, 0.0f, 0.0f);
	mainCamera.SetEyeAtUp(eye, Math::Vector3(Math::kZero), Math::Vector3(Math::kYUnitVector));
	mainCamera.SetZRange(1.0f, 10000.0f);
	// m_Camera.Update();	// If no CameraController, need update manually
	m_CameraController.reset(new CameraController(mainCamera, Math::Vector3(Math::kYUnitVector), *m_Input));

	// Common uniforms
	{
		g_ViewUniformBuffer.Create(Graphics::s_Device, L"View Uniform Buffer", 1, sizeof(FViewUniformBufferParameters), true);
	}

	// Resources
	// ...
	m_ExtraTextures[0] = Graphics::s_TextureManager.GetWhiteTex2D().GetSRV();	// Null yet
	m_ExtraTextures[1] = Graphics::s_BufferManager.m_ShadowBuffer.GetSRV();

	// Effects
	Effects::Init(Graphics::s_Device);

	// Forward+ lighting
	const auto &boundingBox = m_Model->GetBoundingBox();
	auto& forwardPlusLighting = Effects::s_ForwardPlusLighting;
	forwardPlusLighting.CreateRandomLights(Graphics::s_Device, boundingBox.min, boundingBox.max);

	m_ExtraTextures[2] = forwardPlusLighting.m_LightBuffer.GetSRV();
	m_ExtraTextures[3] = forwardPlusLighting.m_LightGrid.GetSRV();
	m_ExtraTextures[4] = forwardPlusLighting.m_LightGridBitMask.GetSRV();
	m_ExtraTextures[5] = forwardPlusLighting.m_LightShadowArray.GetSRV();

	// Skybox
	m_Skybox.reset( new Skybox() );
	m_Skybox->Init(Graphics::s_Device, L"grasscube1024");

	// Particle effects
	CreateParticleEffects();

	// TODO: in Update() ???
	bool bRaytracingEnabled = s_RaytracingMode != RaytracingMode::Off;
	Effects::s_MotionBlur.m_Enabled = true;
	Effects::s_TemporalAA.m_Enabled = !bRaytracingEnabled;
	Effects::s_PostEffects.m_CommonStates.EnableHDR = true;
	Effects::s_PostEffects.m_CommonStates.EnableAdaption = !bRaytracingEnabled;
	Effects::s_PostEffects.m_CommonStates.EnableBloom = !bRaytracingEnabled;

	// Raytracing
	InitRaytracing();

	if (m_bEnableDebug)
	{
		m_DebugPass.reset( new DebugPass() );
		m_DebugPass->Init();
	}
}

void ModelViewer::CleanCustom()
{
	Effects::Shutdown();
	CleanRaytracing();

	m_Model->Cleanup();

	if (m_Skybox)
	{
		m_Skybox->Shutdown();
	}

	if (m_bEnableDebug)
	{
		m_DebugPass->Cleanup();
	}

	// Globals
	{
		g_ViewUniformBuffer.Destroy();
	}
}

void ModelViewer::PostProcess()
{
	Effects::s_PostEffects.Render();
}

void ModelViewer::CustomUI(GraphicsContext &context) 
{
	TextContext textContext(context);
	textContext.Begin();

	textContext.SetTextSize(48.f);
	textContext.SetColor(Color(1.0f, 0.0f, 0.0f));
	textContext.ResetCursor(100.f, 100.f);
	textContext.DrawString("Hello, World!");

	textContext.End();
}

// Render light shadows
// Draw shadow depth to Light::m_LightShadowTempBuffer first, then CopySubResource to Light::m_LightShadowArray.
void ModelViewer::RenderLightShadows(GraphicsContext& gfxContext)
{
	ProfilingScope profilingScope(L"Render Light Shadows", gfxContext);

	static uint32_t LightIndex = 0;

	auto& forwardPlusLighting = Effects::s_ForwardPlusLighting;
	if (LightIndex >= forwardPlusLighting.MaxLights)
		return;

	forwardPlusLighting.m_LightShadowTempBuffer.BeginRendering(gfxContext);
	{
		gfxContext.SetPipelineState(m_ShadowPSO);
		RenderObjects(gfxContext, forwardPlusLighting.m_LightShadowMatrix[LightIndex], ObjectFilter::kOpaque);
		gfxContext.SetPipelineState(m_CutoutShadowPSO);
		RenderObjects(gfxContext, forwardPlusLighting.m_LightShadowMatrix[LightIndex], ObjectFilter::kCutout);
	}
	forwardPlusLighting.m_LightShadowTempBuffer.EndRendering(gfxContext);

	gfxContext.TransitionResource(forwardPlusLighting.m_LightShadowTempBuffer, D3D12_RESOURCE_STATE_GENERIC_READ);
	gfxContext.TransitionResource(forwardPlusLighting.m_LightShadowArray, D3D12_RESOURCE_STATE_COPY_DEST);

	gfxContext.CopySubresource(forwardPlusLighting.m_LightShadowArray, LightIndex, forwardPlusLighting.m_LightShadowTempBuffer, 0);

	gfxContext.TransitionResource(forwardPlusLighting.m_LightShadowArray, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	++LightIndex;
}

void ModelViewer::RenderObjects(GraphicsContext& gfxContext, const Math::Matrix4 &viewProjMat, ObjectFilter filter, uint32_t cullingIndex)
{
	using std::wstring;
	static const wstring RenderOpaqueObjectsString = L"Render Objects Opaque";
	static const wstring RenderCutoutObjectsString = L"Render Objects Cutout";
	static const wstring RenderTransparentObjectsString = L"Render Objects Transparent";
	static const wstring RenderAllObjectsString = L"Render Objects All";
	
	wstring ProfilingString;
	if (filter == ObjectFilter::kOpaque)
		ProfilingString = RenderOpaqueObjectsString;
	else if (filter == ObjectFilter::kCutout)
		ProfilingString = RenderCutoutObjectsString;
	else if (filter == ObjectFilter::kTransparent)
		ProfilingString = RenderTransparentObjectsString;
	else if (filter == ObjectFilter::kAll)
		ProfilingString = RenderAllObjectsString;

	ProfilingScope profilingScope(ProfilingString, gfxContext);

	// Draw

	const BatchElements *batchList = nullptr;
	if (cullingIndex == INDEX_NONE)
		batchList = &m_DefaultBatchList;
	else 
		batchList = &m_PassCullingResults[cullingIndex];

	VSConstants vsConstants;
	vsConstants._ModelToProjection = Math::Transpose(viewProjMat);	// HLSL - mul(float4(pos), mat)
	// vsConstants._ModelToProjection = (viewProjMat);	// HLSL - mul(mat, float4(pos))
	vsConstants._ModelToShadow = Math::Transpose(m_SunShadow->GetShadowMatrix());
	XMStoreFloat3(&vsConstants._CamPos, m_Camera->GetPosition());

	gfxContext.SetDynamicConstantBufferView((uint32_t)RSId::kMeshConstants, sizeof(vsConstants), &vsConstants);

	uint32_t curMatIdx = 0xFFFFFFFFul;

	for (uint32_t i = 0, imax = (uint32_t)batchList->size(); i < imax; i++) 
	{
		uint32_t index = (*batchList)[i];
		uint32_t modelIndex = index >> 16, meshIndex = index & 0xFFFF;

		const auto &mesh = m_Model->m_Meshes[meshIndex];
		Model::DrawParams drawParams = mesh.GetDrawParams();

		if (mesh.materialIndex != curMatIdx)
		{
			// filter objects
			bool bCutoutMat = m_Model->m_MaterialIsCutout[mesh.materialIndex];
			if (bCutoutMat && !((uint32_t)filter & (uint32_t)ObjectFilter::kCutout) ||
				!bCutoutMat && !((uint32_t)filter & (uint32_t)ObjectFilter::kOpaque))
				continue;

			curMatIdx = mesh.materialIndex;
			gfxContext.SetDynamicDescriptors((uint32_t)RSId::kMaterialSRVs, 0, Model::kTexturesPerMaterial, m_Model->GetSRVs(curMatIdx));

			gfxContext.SetConstants((uint32_t)RSId::kCommonCBV, drawParams.baseVertex, curMatIdx);
		}

		gfxContext.DrawIndexed(drawParams.indexCount, drawParams.startIndex, drawParams.baseVertex);
	}

	#if 0
	for (uint32_t meshIndex = 0; meshIndex < m_Model->m_MeshCount; ++meshIndex)
	{
		const Model::Mesh& mesh = m_Model->m_Meshes[meshIndex];
		Model::DrawParams drawParams = mesh.GetDrawParams();

		if (mesh.materialIndex != curMatIdx)
		{
			// filter objects
			bool bCutoutMat = m_Model->m_MaterialIsCutout[mesh.materialIndex];
			if ( bCutoutMat && !((uint32_t)filter & (uint32_t)ObjectFilter::kCutout) ||
				!bCutoutMat && !((uint32_t)filter & (uint32_t)ObjectFilter::kOpaque))
				continue;

			curMatIdx = mesh.materialIndex;
			gfxContext.SetDynamicDescriptors((uint32_t)RSId::kMaterialSRVs, 0, Model::kTexturesPerMaterial, m_Model->GetSRVs(curMatIdx));
			
			gfxContext.SetConstants((uint32_t)RSId::kCommonCBV, drawParams.baseVertex, curMatIdx);
		}

		gfxContext.DrawIndexed(drawParams.indexCount, drawParams.startIndex, drawParams.baseVertex);
	}
	#endif

}

void ModelViewer::CreateParticleEffects()
{
	using namespace ParticleEffects;
	auto& particleEffectManager = Effects::s_ParticleEffectManager;
	Vector3 camPos = m_Camera->GetPosition();

	ParticleEffectProperties effect;
	effect.MinStartColor = effect.MaxStartColor = effect.MinEndColor = effect.MaxEndColor = Color(1.0f, 1.0f, 1.0f, 1.0f);
	effect.TexturePath = L"sparkTex.dds";

	effect.TotalActiveLifeTime = FLT_MAX;
	effect.Size = Vector4(4.0f, 8.0f, 4.0f, 8.0f);	// startSize minmax, endSize minmax
	effect.Velocity = Vector4(20.0f, 200.0f, 50.0f, 180.0f);	// x minmax, y minmax
	effect.LifeMinMax = XMFLOAT2(1.0f, 3.0f);
	effect.MassMinMax = XMFLOAT2(4.5f, 15.0f);
	effect.EmitRate = 64.0f;
	effect.Spread.x = 20.0f;
	effect.Spread.y = 50.0f;
	effect.EmitProperties.Gravity = XMFLOAT3(0.0f, -100.0f, 0.0f);
	effect.EmitProperties.FloorHeight = -0.5f;
	// effect.EmitProperties.EmitPosW = effect.EmitProperties.LastEmitPosW = XMFLOAT3(camPos.GetX(), camPos.GetY(), camPos.GetZ());
	effect.EmitProperties.EmitPosW = effect.EmitProperties.LastEmitPosW = XMFLOAT3(-1200.0f, 185.0f, -445.0f);
	effect.EmitProperties.MaxParticles = 800;
	particleEffectManager.InstantiateEffect(effect);

	// smoke
	ParticleEffectProperties smoke;
	smoke.TexturePath = L"smoke.dds";
	smoke.TotalActiveLifeTime = FLT_MAX;
	smoke.EmitProperties.MaxParticles = 25;
	smoke.EmitProperties.EmitPosW = smoke.EmitProperties.LastEmitPosW = XMFLOAT3(camPos.GetX() + 10.0f, camPos.GetY(), camPos.GetZ() + 400.0f);
	// smoke.EmitProperties.EmitPosW = smoke.EmitProperties.LastEmitPosW = XMFLOAT3(1120.0f, 185.0f, -445.0f);
	smoke.EmitRate = 64.0f;
	smoke.LifeMinMax = XMFLOAT2(2.5f, 4.0f);
	smoke.Size = Vector4(60.0f, 108.0f, 30.0f, 208.0f);
	smoke.Velocity = Vector4(30.0f, 30.0f, 10.0f, 40.0f);
	smoke.MassMinMax = XMFLOAT2(1.0, 3.5);
	smoke.Spread.x = 60.0f;
	smoke.Spread.y = 70.0f;
	smoke.Spread.z = 20.0f;
	particleEffectManager.InstantiateEffect(smoke);
}

/// RayTracing

struct MaterialRootConstant
{
	UINT MaterialID;
};

RaytracingDispatchRayInputs::RaytracingDispatchRayInputs(
	ID3D12Device* pDevice,
	ID3D12StateObject* pPSO,
	void* pHitGroupShaderTable,
	UINT HitGroupStride,
	UINT HitGroupTableSize,
	LPCWSTR rayGenExportName,
	LPCWSTR missExportName)
	: m_pPSO{ pPSO }
{
	const UINT shaderTableSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	ID3D12StateObjectProperties* stateObjectProperties = nullptr;
	ThrowIfFailed(pPSO->QueryInterface(IID_PPV_ARGS(&stateObjectProperties)));
	void* pRayGenShaderData = stateObjectProperties->GetShaderIdentifier(rayGenExportName);
	void* pMissShaderData = stateObjectProperties->GetShaderIdentifier(missExportName);

	m_HitGroupStride = HitGroupStride;

	// MiniEngine requires that all initial data be aligned to 16 bytes
	constexpr UINT alignment = 16;
	std::vector<BYTE> alignedShaderTableData(shaderTableSize + alignment - 1);
	UINT64 rem = (UINT64)alignedShaderTableData.data() % alignment;
	BYTE* pAlignedShaderTableData = alignedShaderTableData.data() + (rem == 0 ? 0 : alignment - rem);
	memcpy(pAlignedShaderTableData, pRayGenShaderData, shaderTableSize);

	m_RayGenShaderTable.Create(pDevice, L"Ray Gen Shader Table", 1, shaderTableSize, pAlignedShaderTableData);
	// ??? alignedShaderTableData.data() -> 

	memcpy(pAlignedShaderTableData, pMissShaderData, shaderTableSize);
	m_MissShaderTable.Create(pDevice, L"Miss Shader Table", 1, shaderTableSize, pAlignedShaderTableData);

	m_HitShaderTable.Create(pDevice, L"Hit Shader Table", 1, HitGroupTableSize, pHitGroupShaderTable);
}

void RaytracingDispatchRayInputs::Cleanup()
{
	m_RayGenShaderTable.Destroy();
	m_MissShaderTable.Destroy();
	m_HitShaderTable.Destroy();
}

D3D12_DISPATCH_RAYS_DESC RaytracingDispatchRayInputs::GetDispatchRayDesc(UINT DispatchWidth, UINT DispatchHeight)
{
	D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = {};

	dispatchRaysDesc.RayGenerationShaderRecord.StartAddress = m_RayGenShaderTable.GetGpuVirtualAddress();
	dispatchRaysDesc.RayGenerationShaderRecord.SizeInBytes = m_RayGenShaderTable.GetBufferSize();
	dispatchRaysDesc.HitGroupTable.StartAddress = m_HitShaderTable.GetGpuVirtualAddress();
	dispatchRaysDesc.HitGroupTable.SizeInBytes = m_HitShaderTable.GetBufferSize();
	dispatchRaysDesc.HitGroupTable.StrideInBytes = m_HitGroupStride;
	dispatchRaysDesc.MissShaderTable.StartAddress = m_MissShaderTable.GetGpuVirtualAddress();
	dispatchRaysDesc.MissShaderTable.SizeInBytes = m_MissShaderTable.GetBufferSize();
	dispatchRaysDesc.MissShaderTable.StrideInBytes = dispatchRaysDesc.MissShaderTable.SizeInBytes; // Only one entry
	dispatchRaysDesc.Width = DispatchWidth;
	dispatchRaysDesc.Height = DispatchHeight;
	dispatchRaysDesc.Depth = 1;

	return dispatchRaysDesc;
}

void ModelViewer::Raytrace(GraphicsContext& gfxContext)
{
	ProfilingScope profilingScope(L"Raytrace", gfxContext);

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& shadowBuffer = Graphics::s_BufferManager.m_ShadowBuffer;
	const auto W = colorBuffer.GetWidth();
	const auto H = colorBuffer.GetHeight();

	auto &mainCamera = *m_Camera.get();

	// Update constants
	{
		DynamicCB& inputs = g_DynamicCB;
		auto& m0 = mainCamera.GetViewProjMatrix();
		auto& m1 = Math::Transpose(Math::Invert(m0));
		memcpy(&inputs.cameraToWorld, &m1, sizeof(inputs.cameraToWorld));
		memcpy(&inputs.worldCameraPosition, &mainCamera.GetPosition(), sizeof(inputs.worldCameraPosition));
		memcpy(&inputs.backgroundColor, &c_BackgroundColor, sizeof(inputs.backgroundColor));
		inputs.resolution = { (float)W, (float)H };
		inputs.frameIndex = m_Gfx->GetFrameCount() & c_MaxFrameIndex;
		inputs.accumulationIndex = m_AccumulationIndex;
		
		HitShaderConstants &hitShaderConstants = g_HitShaderConstants;
		hitShaderConstants._SunDirection = m_SunDirection;
		hitShaderConstants._SunLight = Math::Vector3(1.0f) * m_CommonStates.SunLightIntensity;
		hitShaderConstants._AmbientLight = Math::Vector3(1.0f) * m_CommonStates.AmbientIntensity;
		hitShaderConstants._ShadowTexelSize[0] = 1.0f / shadowBuffer.GetWidth();
		hitShaderConstants._ModelToShadow = Math::Transpose(m_SunShadow->GetShadowMatrix());
		hitShaderConstants._MaxBounces = 1;  // Bounce count (direct lighting w/ indirect lighting)
		hitShaderConstants._IsReflection = false;
		hitShaderConstants._UseShadowRays = true;
	}

	RaytracingMode rayTracingMode = s_RaytracingMode;
	switch (rayTracingMode)
	{
	case RaytracingMode::Traversal:
		RaytraceBarycentrics(gfxContext);
		break;

	case RaytracingMode::SSR:
		RaytraceBarycentricsSSR(gfxContext);
		break;

	case RaytracingMode::Shadows:
		RaytraceShadows(gfxContext);
		break;
	
	case RaytracingMode::DiffuseWithShadowMaps:
	case RaytracingMode::DiffuseWithShadowRays:
		RaytraceDiffuse(gfxContext);
		break;

	case RaytracingMode::ReferencePathTracing:
		ReferencePathTracing(gfxContext);
		break;

	default:
	case RaytracingMode::ReSTIRWithDirectLights:
		ReSTIRWithDirectLights(gfxContext);
		break;

	case RaytracingMode::ReSTIRGI:
		RaytraceReSTIRGI(gfxContext);
		break;
	}

	// Clear the gfxContext's descriptor heap since ray tracing changes this underneath the sheets
	gfxContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, nullptr);
}


/// Tests traversal
void ModelViewer::RaytraceBarycentrics(CommandContext& context)
{
	ProfilingScope profilingScope(L"Raytrace Barycentrics", context);

	ColorBuffer& colorTarget = Graphics::s_BufferManager.m_SceneColorBuffer;
	const auto W = colorTarget.GetWidth();
	const auto H = colorTarget.GetHeight();

	context.WriteBuffer(m_DynamicConstantBuffer, 0, &g_DynamicCB, sizeof(g_DynamicCB));
	context.WriteBuffer(m_HitConstantBuffer, 0, &g_HitShaderConstants, sizeof(g_HitShaderConstants));

	context.TransitionResource(m_DynamicConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	context.TransitionResource(m_HitConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	context.TransitionResource(colorTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.FlushResourceBarriers();

	ID3D12GraphicsCommandList* pCmdList = context.GetCommandList();

	ComPtr<ID3D12GraphicsCommandList4> pRaytracingCmdList;
	pCmdList->QueryInterface(IID_PPV_ARGS(&pRaytracingCmdList));

	ID3D12DescriptorHeap* pDescHeaps[] = { m_RaytracingDescHeap.GetHeapPointer() };
	pRaytracingCmdList->SetDescriptorHeaps(ARRAYSIZE(pDescHeaps), pDescHeaps);

	pCmdList->SetComputeRootSignature(m_GlobalRaytracingRS.GetSignature());
	pCmdList->SetComputeRootDescriptorTable((UINT)RTGlobalRSId::SceneBuffers, m_SceneSrvs);
	pCmdList->SetComputeRootConstantBufferView((UINT)RTGlobalRSId::HitConstants, m_HitConstantBuffer.GetGpuVirtualAddress());
	pCmdList->SetComputeRootConstantBufferView((UINT)RTGlobalRSId::DynamicCB, m_DynamicConstantBuffer.GetGpuVirtualAddress());
	pCmdList->SetComputeRootDescriptorTable((UINT)RTGlobalRSId::Outputs, m_OutColorUAV);
	pCmdList->SetComputeRootShaderResourceView((UINT)RTGlobalRSId::AccelerationStructure, m_TLAS->GetGPUVirtualAddress());

	D3D12_DISPATCH_RAYS_DESC dispathRaysDesc = m_RaytracingInputs[(int)RaytracingType::Primarybarycentric].GetDispatchRayDesc(W, H);
	pRaytracingCmdList->SetPipelineState1(m_RaytracingInputs[(int)RaytracingType::Primarybarycentric].m_pPSO.Get());
	pRaytracingCmdList->DispatchRays(&dispathRaysDesc);
}

void ModelViewer::RaytraceBarycentricsSSR(CommandContext& context)
{
	ProfilingScope profilingScope(L"Raytrace BarycentricsSSR", context);

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
	auto& normalBuffer = Graphics::s_BufferManager.m_SceneNormalBuffer;

	const auto& W = colorBuffer.GetWidth();
	const auto& H = colorBuffer.GetHeight();

	context.WriteBuffer(m_DynamicConstantBuffer, 0, &g_DynamicCB, sizeof(g_DynamicCB));
	context.WriteBuffer(m_HitConstantBuffer, 0, &g_HitShaderConstants, sizeof(g_HitShaderConstants));

	// Transitions
	context.TransitionResource(m_DynamicConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	context.TransitionResource(m_HitConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	context.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(normalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.FlushResourceBarriers();

	ID3D12GraphicsCommandList* pCmdList = context.GetCommandList();
 	ComPtr<ID3D12GraphicsCommandList4> pRaytracingCmdList;
	pCmdList->QueryInterface(IID_PPV_ARGS(&pRaytracingCmdList));

	ID3D12DescriptorHeap* pDescHeaps[] = { m_RaytracingDescHeap.GetHeapPointer() };
	pRaytracingCmdList->SetDescriptorHeaps(ARRAYSIZE(pDescHeaps), pDescHeaps);

	pCmdList->SetComputeRootSignature(m_GlobalRaytracingRS.GetSignature());
	pCmdList->SetComputeRootConstantBufferView((UINT)RTGlobalRSId::HitConstants, m_HitConstantBuffer.GetGpuVirtualAddress());
	pCmdList->SetComputeRootConstantBufferView((UINT)RTGlobalRSId::DynamicCB, m_DynamicConstantBuffer.GetGpuVirtualAddress());
	pCmdList->SetComputeRootDescriptorTable((UINT)RTGlobalRSId::InputTextures, m_DepthAndNormalsTable);
	pCmdList->SetComputeRootDescriptorTable((UINT)RTGlobalRSId::Outputs, m_OutColorUAV);
	pCmdList->SetComputeRootShaderResourceView((UINT)RTGlobalRSId::AccelerationStructure, m_TLAS->GetGPUVirtualAddress());

	D3D12_DISPATCH_RAYS_DESC dispathRaysDesc = m_RaytracingInputs[(int)RaytracingType::Reflectionbarycentric].GetDispatchRayDesc(W, H);
	pRaytracingCmdList->SetPipelineState1(m_RaytracingInputs[(int)RaytracingType::Reflectionbarycentric].m_pPSO.Get());
	pRaytracingCmdList->DispatchRays(&dispathRaysDesc);
}

void ModelViewer::RaytraceDiffuse(GraphicsContext& gfxContext)
{
	ProfilingScope profilingScope(L"Raytrace Diffuse", gfxContext);

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
	auto& normalBuffer = Graphics::s_BufferManager.m_SceneNormalBuffer;
	auto& shadowBuffer = Graphics::s_BufferManager.m_ShadowBuffer;

	const auto& W = colorBuffer.GetWidth();
	const auto& H = colorBuffer.GetHeight();

	gfxContext.WriteBuffer(m_DynamicConstantBuffer, 0, &g_DynamicCB, sizeof(g_DynamicCB));
	gfxContext.WriteBuffer(m_HitConstantBuffer, 0, &g_HitShaderConstants, sizeof(g_HitShaderConstants));

	// Transitions
	gfxContext.TransitionResource(m_DynamicConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	gfxContext.TransitionResource(m_HitConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	gfxContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	gfxContext.TransitionResource(normalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	gfxContext.TransitionResource(shadowBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	gfxContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	gfxContext.FlushResourceBarriers();

	ID3D12GraphicsCommandList* pCmdList = gfxContext.GetCommandList();
	ComPtr<ID3D12GraphicsCommandList4> pRaytracingCmdList;
	pCmdList->QueryInterface(IID_PPV_ARGS(&pRaytracingCmdList));

	ID3D12DescriptorHeap* pDescHeaps[] = { m_RaytracingDescHeap.GetHeapPointer() };
	pRaytracingCmdList->SetDescriptorHeaps(ARRAYSIZE(pDescHeaps), pDescHeaps);

	pCmdList->SetComputeRootSignature(m_GlobalRaytracingRS.GetSignature());
	pCmdList->SetComputeRootDescriptorTable((UINT)RTGlobalRSId::SceneBuffers, m_SceneSrvs);
	pCmdList->SetComputeRootConstantBufferView((UINT)RTGlobalRSId::HitConstants, m_HitConstantBuffer.GetGpuVirtualAddress());
	pCmdList->SetComputeRootConstantBufferView((UINT)RTGlobalRSId::DynamicCB, m_DynamicConstantBuffer.GetGpuVirtualAddress());
	pCmdList->SetComputeRootDescriptorTable((UINT)RTGlobalRSId::InputTextures, m_DepthAndNormalsTable);
	pCmdList->SetComputeRootDescriptorTable((UINT)RTGlobalRSId::Outputs, m_OutColorUAV);
	pCmdList->SetComputeRootShaderResourceView((UINT)RTGlobalRSId::AccelerationStructure, m_TLAS->GetGPUVirtualAddress());

	D3D12_DISPATCH_RAYS_DESC dispathRaysDesc = m_RaytracingInputs[(int)RaytracingType::DiffuseHitShader].GetDispatchRayDesc(W, H);
	pRaytracingCmdList->SetPipelineState1(m_RaytracingInputs[(int)RaytracingType::DiffuseHitShader].m_pPSO.Get());
	pRaytracingCmdList->DispatchRays(&dispathRaysDesc);
}

void ModelViewer::RaytraceShadows(GraphicsContext& gfxContext)
{
	ProfilingScope profilingScope(L"Raytrace Shadows", gfxContext);

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
	auto& normalBuffer = Graphics::s_BufferManager.m_SceneNormalBuffer;
	auto& shadowBuffer = Graphics::s_BufferManager.m_ShadowBuffer;

	const auto& W = colorBuffer.GetWidth();
	const auto& H = colorBuffer.GetHeight();

	gfxContext.WriteBuffer(m_DynamicConstantBuffer, 0, &g_DynamicCB, sizeof(g_DynamicCB));
	gfxContext.WriteBuffer(m_HitConstantBuffer, 0, &g_HitShaderConstants, sizeof(g_HitShaderConstants));

	// Transitions
	gfxContext.TransitionResource(m_DynamicConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	gfxContext.TransitionResource(m_HitConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	gfxContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	gfxContext.TransitionResource(normalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	gfxContext.TransitionResource(shadowBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	gfxContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	gfxContext.FlushResourceBarriers();

	ID3D12GraphicsCommandList* pCmdList = gfxContext.GetCommandList();
	ComPtr<ID3D12GraphicsCommandList4> pRaytracingCmdList;
	pCmdList->QueryInterface(IID_PPV_ARGS(&pRaytracingCmdList));

	ID3D12DescriptorHeap* pDescHeaps[] = { m_RaytracingDescHeap.GetHeapPointer() };
	pRaytracingCmdList->SetDescriptorHeaps(ARRAYSIZE(pDescHeaps), pDescHeaps);

	pCmdList->SetComputeRootSignature(m_GlobalRaytracingRS.GetSignature());
	pCmdList->SetComputeRootConstantBufferView((UINT)RTGlobalRSId::HitConstants, m_HitConstantBuffer.GetGpuVirtualAddress());
	pCmdList->SetComputeRootConstantBufferView((UINT)RTGlobalRSId::DynamicCB, m_DynamicConstantBuffer.GetGpuVirtualAddress());
	pCmdList->SetComputeRootDescriptorTable((UINT)RTGlobalRSId::InputTextures, m_DepthAndNormalsTable);
	pCmdList->SetComputeRootDescriptorTable((UINT)RTGlobalRSId::Outputs, m_OutColorUAV);
	pCmdList->SetComputeRootShaderResourceView((UINT)RTGlobalRSId::AccelerationStructure, m_TLAS->GetGPUVirtualAddress());

	D3D12_DISPATCH_RAYS_DESC dispathRaysDesc = m_RaytracingInputs[(int)RaytracingType::Shadows].GetDispatchRayDesc(W, H);
	pRaytracingCmdList->SetPipelineState1(m_RaytracingInputs[(int)RaytracingType::Shadows].m_pPSO.Get());
	pRaytracingCmdList->DispatchRays(&dispathRaysDesc);
}

void ModelViewer::RaytraceReflections(GraphicsContext& gfxContext)
{
	ProfilingScope profilingScope(L"Raytrace Reflections", gfxContext);

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
	auto& normalBuffer= Graphics::s_BufferManager.m_SceneNormalBuffer;

	// TODO...
}

void ModelViewer::ReferencePathTracing(GraphicsContext& gfxContext)
{
	ProfilingScope profilingScope(L"Reference PathTracing", gfxContext);

	static int s_GroupSize = 8;

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
	auto& normalBuffer = Graphics::s_BufferManager.m_SceneNormalBuffer;
	auto& shadowBuffer = Graphics::s_BufferManager.m_ShadowBuffer;

	const auto& W = colorBuffer.GetWidth();
	const auto& H = colorBuffer.GetHeight();

	gfxContext.WriteBuffer(m_DynamicConstantBuffer, 0, &g_DynamicCB, sizeof(g_DynamicCB));
	gfxContext.WriteBuffer(m_HitConstantBuffer, 0, &g_HitShaderConstants, sizeof(g_HitShaderConstants));

	ID3D12Device* pDevice = Graphics::s_Device;

	// Transitions
	gfxContext.TransitionResource(m_DynamicConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	gfxContext.TransitionResource(m_HitConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	gfxContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	gfxContext.TransitionResource(normalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	gfxContext.TransitionResource(shadowBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	gfxContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	gfxContext.TransitionResource(m_AccumulationBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	gfxContext.FlushResourceBarriers();

	ID3D12GraphicsCommandList* pCmdList = gfxContext.GetCommandList();
	ComPtr<ID3D12GraphicsCommandList4> pRaytracingCmdList;
	pCmdList->QueryInterface(IID_PPV_ARGS(&pRaytracingCmdList));

	ID3D12DescriptorHeap* pDescHeaps[] = { m_RaytracingDescHeap.GetHeapPointer() };
	pRaytracingCmdList->SetDescriptorHeaps(ARRAYSIZE(pDescHeaps), pDescHeaps);

	pCmdList->SetComputeRootSignature(m_GlobalRaytracingRS.GetSignature());
	pCmdList->SetComputeRootConstantBufferView((UINT)RTGlobalRSId::ViewUniforms, g_ViewUniformBuffer.GetGpuPointer());
	pCmdList->SetComputeRootDescriptorTable((UINT)RTGlobalRSId::SceneBuffers, m_SceneSrvs);
	pCmdList->SetComputeRootConstantBufferView((UINT)RTGlobalRSId::HitConstants, m_HitConstantBuffer.GetGpuVirtualAddress());
	pCmdList->SetComputeRootConstantBufferView((UINT)RTGlobalRSId::DynamicCB, m_DynamicConstantBuffer.GetGpuVirtualAddress());
	pCmdList->SetComputeRootDescriptorTable((UINT)RTGlobalRSId::InputTextures, m_DepthAndNormalsTable);
	pCmdList->SetComputeRootDescriptorTable((UINT)RTGlobalRSId::Outputs, m_OutColorUAV);
	pCmdList->SetComputeRootShaderResourceView((UINT)RTGlobalRSId::AccelerationStructure, m_TLAS->GetGPUVirtualAddress());

	D3D12_DISPATCH_RAYS_DESC dispathRaysDesc = m_RaytracingInputs[(int)RaytracingType::ReferencePathTracing].GetDispatchRayDesc(W, H);
	pRaytracingCmdList->SetPipelineState1(m_RaytracingInputs[(int)RaytracingType::ReferencePathTracing].m_pPSO.Get());
	pRaytracingCmdList->DispatchRays(&dispathRaysDesc);

	// Copy
	{
		pDevice->CopyDescriptorsSimple(1, m_AccumulationBufferSRV.GetCpuHandle(), m_AccumulationBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		gfxContext.TransitionResource(m_AccumulationBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		pCmdList->SetPipelineState(m_BufferCopyPSO.GetPipelineStateObject());

		pCmdList->SetDescriptorHeaps(ARRAYSIZE(pDescHeaps), pDescHeaps);
		pCmdList->SetComputeRootDescriptorTable((UINT)RTGlobalRSId::InputTextures, m_AccumulationBufferSRV.GetGpuHandle());
		pCmdList->SetComputeRootDescriptorTable((UINT)RTGlobalRSId::Outputs, m_OutColorUAV);

		pCmdList->Dispatch(Math::DivideByMultiple(W, s_GroupSize), Math::DivideByMultiple(H, s_GroupSize), 1);
	}
}

void ModelViewer::ReSTIRWithDirectLights(GraphicsContext& gfxContext)
{
	ProfilingScope profilingScope(L"ReSTIR with Direct Lighting", gfxContext);

	static int s_GroupSizeX = 64;
	static int s_GroupSizeXY = 8;

	ComputeContext &computeContext = gfxContext.GetComputeContext();

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
	auto& normalBuffer = Graphics::s_BufferManager.m_SceneNormalBuffer;
	auto& velocityBuffer = Graphics::s_BufferManager.m_VelocityBuffer;
	auto& shadowBuffer = Graphics::s_BufferManager.m_ShadowBuffer;

	const auto& W = colorBuffer.GetWidth();
	const auto& H = colorBuffer.GetHeight();
	const auto& N = W * H;

	const auto& frameIndex = m_Gfx->GetFrameCount();
	uint32_t currReservoirIndex = frameIndex & 1;
	uint32_t prevReservoirIndex = 1 - currReservoirIndex;

	auto pCurrReservoirBuffer = m_ReservoirBuffer + currReservoirIndex;
	auto pPrevReservoirBuffer = m_ReservoirBuffer + prevReservoirIndex;

	computeContext.SetRootSignature(m_GlobalRaytracingRS);

	if (m_bNeedClearReservoirs)
	{
		// Clear prev reservoirs
		computeContext.TransitionResource(*pPrevReservoirBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		computeContext.SetPipelineState(m_ClearReservoirPSO);
		computeContext.SetDynamicDescriptor((uint32_t)RTGlobalRSId::Outputs, 2, pPrevReservoirBuffer->GetUAV());

		computeContext.Dispatch2D(N, 1, s_GroupSizeX, 1);

		m_bNeedClearReservoirs = false;
	}

	computeContext.WriteBuffer(m_DynamicConstantBuffer, 0, &g_DynamicCB, sizeof(g_DynamicCB));
	computeContext.WriteBuffer(m_HitConstantBuffer, 0, &g_HitShaderConstants, sizeof(g_HitShaderConstants));

	computeContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	computeContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	computeContext.TransitionResource(normalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	computeContext.TransitionResource(velocityBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	computeContext.TransitionResource(shadowBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	computeContext.TransitionResource(*pPrevReservoirBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	computeContext.TransitionResource(*pCurrReservoirBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	computeContext.FlushResourceBarriers();

	ID3D12GraphicsCommandList* pCmdList = computeContext.GetCommandList();
	ComPtr<ID3D12GraphicsCommandList4> pRaytracingCmdList;
	pCmdList->QueryInterface(IID_PPV_ARGS(&pRaytracingCmdList));

	ID3D12Device* pDevice = Graphics::s_Device;

	computeContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_RaytracingDescHeap.GetHeapPointer());

	pDevice->CopyDescriptorsSimple(1, m_ReservoirBufferSRV.GetCpuHandle(), pPrevReservoirBuffer->GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	pDevice->CopyDescriptorsSimple(1, m_ReservoirBufferUAV.GetCpuHandle(), pCurrReservoirBuffer->GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	computeContext.SetConstantBuffer((UINT)RTGlobalRSId::ViewUniforms, g_ViewUniformBuffer.GetGpuPointer());
	computeContext.SetDescriptorTable((UINT)RTGlobalRSId::SceneBuffers, m_SceneSrvs);
	computeContext.SetConstantBuffer((UINT)RTGlobalRSId::HitConstants, m_HitConstantBuffer.GetGpuVirtualAddress());
	computeContext.SetConstantBuffer((UINT)RTGlobalRSId::DynamicCB, m_DynamicConstantBuffer.GetGpuVirtualAddress());
	computeContext.SetDescriptorTable((UINT)RTGlobalRSId::InputTextures, m_DepthAndNormalsTable);
	computeContext.SetDescriptorTable((UINT)RTGlobalRSId::Outputs, m_OutColorUAV);
	computeContext.SetShaderResourceView((UINT)RTGlobalRSId::AccelerationStructure, m_TLAS->GetGPUVirtualAddress());

#if 0
	// WRONG::Overlapped views!
	computeContext.SetDynamicDescriptor(4, 2, pCurrReservoirBuffer->GetUAV());
	computeContext.SetDynamicDescriptor(0, 5 - 1, pPrevReservoirBuffer->GetSRV());
	computeContext.GetDynamicViewDescriptorHeap().CommitComputeRootDescriptorTables(pCmdList);
#endif

	D3D12_DISPATCH_RAYS_DESC dispathRaysDesc = m_RaytracingInputs[(int)RaytracingType::ReSTIRWithDirectLights].GetDispatchRayDesc(W, H);
	pRaytracingCmdList->SetPipelineState1(m_RaytracingInputs[(UINT)RaytracingType::ReSTIRWithDirectLights].m_pPSO.Get());
	pRaytracingCmdList->DispatchRays(&dispathRaysDesc);
}

void ModelViewer::RaytraceReSTIRGI(GraphicsContext& gfxContext)
{
	ProfilingScope profilingScope(L"ReSTIR GI", gfxContext);

	static int s_GroupSizeX = 64;
	static int s_GroupSizeXY = 8;

	ID3D12Device* pDevice = Graphics::s_Device;
	ComputeContext& computeContext = gfxContext.GetComputeContext();

	auto *pRestirGI = m_ReSTIRGI.get();

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
	auto& normalBuffer = Graphics::s_BufferManager.m_SceneNormalBuffer;
	auto& velocityBuffer = Graphics::s_BufferManager.m_VelocityBuffer;
	auto& shadowBuffer = Graphics::s_BufferManager.m_ShadowBuffer;

	const auto& W = colorBuffer.GetWidth();
	const auto& H = colorBuffer.GetHeight();
	const auto& halfW = W / 2, halfH = H / 2;

	const auto& frameIndex = m_Gfx->GetFrameCount();
	uint32_t currFrameIndex = frameIndex & 1;
	uint32_t prevFrameIndex = 1 - currFrameIndex;

	auto& currSampleRadiance = pRestirGI->m_TemporalSampleRadiance[currFrameIndex];
	auto& currSampleNormal = pRestirGI->m_TemporalSampleNormal[currFrameIndex];
	auto& currSampleHitInfo = pRestirGI->m_TemporalSampleHitInfo[currFrameIndex];
	auto& currRayOrigin = pRestirGI->m_TemporalRayOrigin[currFrameIndex];
	auto& currReservoir = pRestirGI->m_TemporalReservoir[currFrameIndex];

	auto& prevSampleRadiance = pRestirGI->m_TemporalSampleRadiance[prevFrameIndex];
	auto& prevSampleNormal = pRestirGI->m_TemporalSampleNormal[prevFrameIndex];
	auto& prevSampleHitInfo = pRestirGI->m_TemporalSampleHitInfo[prevFrameIndex];
	auto& prevRayOrigin = pRestirGI->m_TemporalRayOrigin[prevFrameIndex];
	auto& prevReservoir = pRestirGI->m_TemporalReservoir[prevFrameIndex];

	computeContext.SetRootSignature(m_GlobalRaytracingRS);

	if (m_bNeedClearReservoirs)
	{
		computeContext.TransitionResource(prevSampleRadiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		computeContext.TransitionResource(prevSampleNormal, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		computeContext.TransitionResource(prevSampleHitInfo, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		computeContext.TransitionResource(prevRayOrigin, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		computeContext.TransitionResource(prevReservoir, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		computeContext.FlushResourceBarriers();

		computeContext.ClearUAV(prevSampleRadiance);
		computeContext.ClearUAV(prevSampleNormal);
		computeContext.ClearUAV(prevSampleHitInfo);
		computeContext.ClearUAV(prevRayOrigin);
		computeContext.ClearUAV(prevReservoir);

		m_bNeedClearReservoirs = false;
	}

	computeContext.WriteBuffer(m_DynamicConstantBuffer, 0, &g_DynamicCB, sizeof(g_DynamicCB));
	computeContext.WriteBuffer(m_HitConstantBuffer, 0, &g_HitShaderConstants, sizeof(g_HitShaderConstants));
	
	computeContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_RaytracingDescHeap.GetHeapPointer());

	// New samples
	{
		computeContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(normalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(pRestirGI->m_SampleRadiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		computeContext.TransitionResource(pRestirGI->m_SampleNormal, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		computeContext.TransitionResource(pRestirGI->m_SampleHitInfo, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		computeContext.FlushResourceBarriers();

		computeContext.SetConstantBuffer((UINT)RTGlobalRSId::ViewUniforms, g_ViewUniformBuffer.GetGpuPointer());
		computeContext.SetDescriptorTable((UINT)RTGlobalRSId::SceneBuffers, m_SceneSrvs);
		computeContext.SetConstantBuffer((UINT)RTGlobalRSId::HitConstants, m_HitConstantBuffer.GetGpuVirtualAddress());
		computeContext.SetConstantBuffer((UINT)RTGlobalRSId::DynamicCB, m_DynamicConstantBuffer.GetGpuVirtualAddress());
		computeContext.SetDescriptorTable((UINT)RTGlobalRSId::InputTextures, m_DepthAndNormalsTable);
		computeContext.SetDescriptorTable((UINT)RTGlobalRSId::Outputs, m_OutColorUAV);
		computeContext.SetShaderResourceView((UINT)RTGlobalRSId::AccelerationStructure, m_TLAS->GetGPUVirtualAddress());
		computeContext.SetDescriptorTable((UINT)RTGlobalRSId::ReSTIRGIOutputs, pRestirGI->m_SampleRadianceUAV.GetGpuHandle());

		ID3D12GraphicsCommandList* pCmdList = computeContext.GetCommandList();
		ComPtr<ID3D12GraphicsCommandList4> pRaytracingCmdList;
		pCmdList->QueryInterface(IID_PPV_ARGS(&pRaytracingCmdList));

		D3D12_DISPATCH_RAYS_DESC dispathRaysDesc = pRestirGI->GetDispatchRayDesc(W, H);
		pRaytracingCmdList->SetPipelineState1(pRestirGI->m_ReSTIRGIInputs.GetPSO());
		pRaytracingCmdList->DispatchRays(&dispathRaysDesc);
	}

	const UINT DescriptorStepSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	// Temporal resampling
	{
		computeContext.TransitionResource(pRestirGI->m_SampleRadiance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(pRestirGI->m_SampleNormal, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(pRestirGI->m_SampleHitInfo, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(prevSampleRadiance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(prevSampleNormal, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(prevSampleHitInfo, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(prevRayOrigin, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(prevReservoir, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		computeContext.TransitionResource(currSampleRadiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		computeContext.TransitionResource(currSampleNormal, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		computeContext.TransitionResource(currSampleHitInfo, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		computeContext.TransitionResource(currRayOrigin, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		computeContext.TransitionResource(currReservoir, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		computeContext.FlushResourceBarriers();

		// Prev sample srvs
		{
			auto descHandle = pRestirGI->m_TemporalSampleRadianceSRV;
			pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), prevSampleRadiance.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			descHandle += DescriptorStepSize;
			pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), prevSampleNormal.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			descHandle += DescriptorStepSize;
			pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), prevSampleHitInfo.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			descHandle += DescriptorStepSize;
			pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), prevRayOrigin.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			descHandle += DescriptorStepSize;
			pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), prevReservoir.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		// Curr sample uavs
		{
			auto descHandle = pRestirGI->m_TemporalSampleRadianceUAV;
			pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), currSampleRadiance.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			descHandle += DescriptorStepSize;
			pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), currSampleNormal.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			descHandle += DescriptorStepSize;
			pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), currSampleHitInfo.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			descHandle += DescriptorStepSize;
			pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), currRayOrigin.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			descHandle += DescriptorStepSize;
			pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), currReservoir.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}

		computeContext.SetPipelineState(pRestirGI->m_TemporalResamplingPSO);

		computeContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_RaytracingDescHeap.GetHeapPointer());

		computeContext.SetConstantBuffer((UINT)RTGlobalRSId::ViewUniforms, g_ViewUniformBuffer.GetGpuPointer());
		computeContext.SetDescriptorTable((UINT)RTGlobalRSId::InputTextures, m_DepthAndNormalsTable);
		computeContext.SetDescriptorTable((UINT)RTGlobalRSId::ReSTIRGINewSamples, pRestirGI->m_SampleRadianceSRV.GetGpuHandle());
		computeContext.SetDescriptorTable((UINT)RTGlobalRSId::ReSTIRGISPSamples, pRestirGI->m_TemporalSampleRadianceSRV.GetGpuHandle());
		computeContext.SetDescriptorTable((UINT)RTGlobalRSId::ReSTIRGIOutputs, pRestirGI->m_TemporalSampleRadianceUAV.GetGpuHandle());

		if (pRestirGI->IsHalfSize())
			computeContext.Dispatch2D(halfW, halfH);
		else
			computeContext.Dispatch2D(W, H);

	}
#if 1
	// Spatial resampling
	{
		// TODO...
	}
	// Resolve
	{
		computeContext.TransitionResource(currSampleRadiance, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(currSampleNormal, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(currSampleHitInfo, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(currRayOrigin, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(currReservoir, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(pRestirGI->m_Irradiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		computeContext.FlushResourceBarriers();

		{
			auto descHandle = pRestirGI->m_ResolveSampleRadianceSRV;
			pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), currSampleRadiance.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			descHandle += DescriptorStepSize;
			pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), currSampleNormal.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			descHandle += DescriptorStepSize;
			pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), currSampleHitInfo.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			descHandle += DescriptorStepSize;
			pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), currRayOrigin.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			descHandle += DescriptorStepSize;
			pDevice->CopyDescriptorsSimple(1, descHandle.GetCpuHandle(), currReservoir.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}

		computeContext.SetPipelineState(pRestirGI->m_ResolvePSO);

		computeContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_RaytracingDescHeap.GetHeapPointer());

		computeContext.SetConstantBuffer((UINT)RTGlobalRSId::ViewUniforms, g_ViewUniformBuffer.GetGpuPointer());
		computeContext.SetDescriptorTable((UINT)RTGlobalRSId::InputTextures, m_DepthAndNormalsTable);
		computeContext.SetDescriptorTable((UINT)RTGlobalRSId::ReSTIRGINewSamples, pRestirGI->m_SampleRadianceSRV.GetGpuHandle());
		computeContext.SetDescriptorTable((UINT)RTGlobalRSId::ReSTIRGISPSamples, pRestirGI->m_ResolveSampleRadianceSRV.GetGpuHandle());
		computeContext.SetDescriptorTable((UINT)RTGlobalRSId::ReSTIRGIOutputs, pRestirGI->m_IrradianceUAV.GetGpuHandle());

		computeContext.Dispatch2D(W, H);

		computeContext.TransitionResource(pRestirGI->m_Irradiance, D3D12_RESOURCE_STATE_GENERIC_READ);
	}

#endif

}

D3D12_STATE_SUBOBJECT CreateDxilLibrary(LPCWSTR entryPoint, const void* pShaderByteCode, SIZE_T bytecodeLength, D3D12_DXIL_LIBRARY_DESC& dxilLibDesc, D3D12_EXPORT_DESC& exportDesc)
{
	exportDesc = { entryPoint, nullptr, D3D12_EXPORT_FLAG_NONE };
	D3D12_STATE_SUBOBJECT dxilLibSubObject = {};
	dxilLibDesc.DXILLibrary.pShaderBytecode = pShaderByteCode;
	dxilLibDesc.DXILLibrary.BytecodeLength = bytecodeLength;
	dxilLibDesc.NumExports = 1;
	dxilLibDesc.pExports = &exportDesc;
	dxilLibSubObject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
	dxilLibSubObject.pDesc = &dxilLibDesc;

	return dxilLibSubObject;
}

void SetPipelineStateStackSize(LPCWSTR raygen, LPCWSTR closestHit, LPCWSTR miss, UINT maxRecursion, ID3D12StateObject* pStateObject)
{
	ID3D12StateObjectProperties* stateObjectProperties = nullptr;
	ThrowIfFailed(pStateObject->QueryInterface(IID_PPV_ARGS(&stateObjectProperties)));
	UINT64 closestHitStackSize = stateObjectProperties->GetShaderStackSize(closestHit);
	UINT64 missStackSize = stateObjectProperties->GetShaderStackSize(miss);
	UINT64 raygenStackSize = stateObjectProperties->GetShaderStackSize(raygen);

	UINT64 totalStackSize = raygenStackSize + std::max(missStackSize, closestHitStackSize) * maxRecursion;
	stateObjectProperties->SetPipelineStackSize(totalStackSize);
}

void AllocateBufferSrv(ID3D12Device *pDevice, ID3D12Resource& resource, UserDescriptorHeap &descHeap)
{
	auto& descHandle = descHeap.Alloc(1);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.NumElements = (UINT)(resource.GetDesc().Width / sizeof(UINT32));
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
	srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	pDevice->CreateShaderResourceView(&resource, &srvDesc, descHandle.GetCpuHandle());
}

void AllocateBufferUav(ID3D12Device* pDevice, ID3D12Resource& resource, UserDescriptorHeap& descHeap)
{
	auto& descHandle = descHeap.Alloc(1);
	
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.NumElements = (UINT)(resource.GetDesc().Width / sizeof(UINT32));
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
	uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;

	pDevice->CreateUnorderedAccessView(&resource, nullptr, &uavDesc, descHandle.GetCpuHandle());
}

void CreateVertexBufferByteAddressSRV(ID3D12Device *pDevice, StructuredBuffer vertexBuffer, DescriptorHandle handle)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = (UINT)vertexBuffer.GetBufferSize() / 4;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
	srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	pDevice->CreateShaderResourceView(const_cast<ID3D12Resource*>(vertexBuffer.GetResource()), &srvDesc, handle.GetCpuHandle());
}

void ModelViewer::InitRaytracing()
{
	ID3D12Device* pDevice = Graphics::s_Device;
	ThrowIfFailed(pDevice->QueryInterface(IID_PPV_ARGS(&m_RaytracingDevice)), L"Couldn't get DirectX Raytracing interface for the device.\n");

	// Descriptor Heap
	uint32_t descOffset = 0;
	{
		m_RaytracingDescHeap.Create(pDevice, L"RaytracingDescHeap");
	}

	D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1;
	pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1));

	// Common buffers
	{
		m_HitConstantBuffer.Create(pDevice, L"Hit Contant Buffer", 1, sizeof(HitShaderConstants));
		m_DynamicConstantBuffer.Create(pDevice, L"Dynamic Constant Buffer", 1, sizeof(DynamicCB));

		const auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
		const uint32_t& W = colorBuffer.GetWidth();
		const uint32_t& H = colorBuffer.GetHeight();
		if (m_bEnablePathTracing)
			m_AccumulationBuffer.Create(pDevice, L"Accumulation Buffer", W, H, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);

		const uint32_t N = W * H;
		if (m_bEnableReSTIRDI)
		{
			m_ReservoirBuffer[0].Create(pDevice, L"Reservoir Buffer 0", N * c_MaxReservoirs, sizeof(FReservoir));
			m_ReservoirBuffer[1].Create(pDevice, L"Reservoir Buffer 1", N * c_MaxReservoirs, sizeof(FReservoir));
		}
	}

	// RT resources & pipelines
	{
		InitRaytracingViews(pDevice);

		InitRaytracingAS(pDevice);
		InitRaytracingStateObjects(pDevice);
	}

	if (m_bEnableReSTIRGI)
	{
		m_ReSTIRGI->Init(pDevice, m_GlobalRaytracingRS);
	}
}

void ModelViewer::InitRaytracingViews(ID3D12Device* pDevice)
{
	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
	auto& normalBuffer= Graphics::s_BufferManager.m_SceneNormalBuffer;
	auto& velocityBuffer = Graphics::s_BufferManager.m_VelocityBuffer;

	/// Scene textures
	{
		// UAVs
		{
			DescriptorHandle colorUAV = m_RaytracingDescHeap.Alloc();
			pDevice->CopyDescriptorsSimple(1, colorUAV.GetCpuHandle(), colorBuffer.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			m_OutColorUAV = colorUAV.GetGpuHandle();

			// Accumulation buffer
			// if (m_bEnablePathTracing)
			// Align to the gloal root signature
			{
				m_AccumulationBufferUAV = m_RaytracingDescHeap.Alloc();
				pDevice->CopyDescriptorsSimple(1, m_AccumulationBufferUAV.GetCpuHandle(), m_AccumulationBuffer.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			}

			// Reservoir buffer
			if (m_bEnableReSTIRDI)
			{
				m_ReservoirBufferUAV = m_RaytracingDescHeap.Alloc();
				// pDevice->CopyDescriptorsSimple(1, m_ReservoirBufferUAV.GetCpuHandle(), m_ReservoirBuffer[0].GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			}

			// ReSTIR GI
			if (m_bEnableReSTIRGI)
				m_ReSTIRGI->InitUAVs(pDevice, m_RaytracingDescHeap);
			
		}

		DescriptorHandle depthSRV = m_RaytracingDescHeap.Alloc();
		pDevice->CopyDescriptorsSimple(1, depthSRV.GetCpuHandle(), depthBuffer.GetDepthSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_DepthAndNormalsTable = depthSRV.GetGpuHandle();

		D3D12_CPU_DESCRIPTOR_HANDLE normalSRV = m_RaytracingDescHeap.Alloc();
		pDevice->CopyDescriptorsSimple(1, normalSRV, normalBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		D3D12_CPU_DESCRIPTOR_HANDLE velocitySRV = m_RaytracingDescHeap.Alloc();
		pDevice->CopyDescriptorsSimple(1, velocitySRV, velocityBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		m_AccumulationBufferSRV = m_RaytracingDescHeap.Alloc();
		// It can change, don't copy here
		// pDevice->CopyDescriptorsSimple(1, m_AccumulationBufferSRV.GetCpuHandle(), m_AccumulationBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		// ReSTIR GI
		if (m_bEnableReSTIRGI)
			m_ReSTIRGI->InitSRVs(pDevice, m_RaytracingDescHeap);
	}
	// Scene buffers
	{
		// Mesh data
		DescriptorHandle meshInfoSRV = m_RaytracingDescHeap.Alloc();
		pDevice->CopyDescriptorsSimple(1, meshInfoSRV.GetCpuHandle(), m_Model->m_MeshInfoSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_SceneSrvs = meshInfoSRV.GetGpuHandle();

		DescriptorHandle indexBufferSRV = m_RaytracingDescHeap.Alloc();
		pDevice->CopyDescriptorsSimple(1, indexBufferSRV.GetCpuHandle(), m_Model->GetIndexBufferSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		DescriptorHandle vertexBufferSRV = m_RaytracingDescHeap.Alloc();
		CreateVertexBufferByteAddressSRV(pDevice, m_Model->m_VertexBuffer, vertexBufferSRV);
		// pDevice->CopyDescriptorsSimple(1, vertexBufferSRV.GetCpuHandle(), m_Model->GetVertexBufferSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		// ERROR::SRV须和shader声明一致

		// Others
		{
			// Light buffer
			DescriptorHandle lightBufferSRV = m_RaytracingDescHeap.Alloc();
			pDevice->CopyDescriptorsSimple(1, lightBufferSRV.GetCpuHandle(), Effects::s_ForwardPlusLighting.m_LightBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			m_LightBufferSrv = lightBufferSRV.GetGpuHandle();

			// Reservoir buffer
			m_ReservoirBufferSRV = m_RaytracingDescHeap.Alloc();
			// pDevice->CopyDescriptorsSimple(1, m_ReservoirBufferSRV.GetCpuHandle(), m_ReservoirBuffer[0].GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			// TODO ...

			// Shadow buffer
			DescriptorHandle shadowBufferSRV = m_RaytracingDescHeap.Alloc();
			pDevice->CopyDescriptorsSimple(1, shadowBufferSRV.GetCpuHandle(), Graphics::s_TextureManager.GetBlackTex2D().GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			// SSAO
			DescriptorHandle AOTextureSRV = m_RaytracingDescHeap.Alloc();
			pDevice->CopyDescriptorsSimple(1, AOTextureSRV.GetCpuHandle(), Graphics::s_TextureManager.GetWhiteTex2D().GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
	}
	
	uint32_t numMeshes = m_Model->m_MeshCount;
	uint32_t numMaterials = m_Model->m_MaterialCount;

	uint32_t descOffset = m_RaytracingDescHeap.GetAllocatedCount();

	// Texture descriptor heap
	// Copy descriptors
	uint32_t materialSrvOffset = descOffset;
	const uint32_t numTexturesPerMat = 2;
	m_RaytracingDescHeap.Alloc(numMaterials * numTexturesPerMat);
	UINT srvDescriptorStepSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	// material textures
	for (uint32_t matId = 0; matId < numMaterials; ++matId)
	{
		const auto& curMat = m_Model->m_Materials[matId];
		const auto& srcTexHandles = m_Model->GetSRVs(matId);

		auto dstHandle = m_RaytracingDescHeap.GetHandleAtOffset(descOffset);

		// Diffuse
		pDevice->CopyDescriptorsSimple(1, dstHandle.GetCpuHandle(), srcTexHandles[0], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		dstHandle += srvDescriptorStepSize;

		// Normal
		pDevice->CopyDescriptorsSimple(1, dstHandle.GetCpuHandle(), srcTexHandles[2], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		dstHandle += srvDescriptorStepSize;

		m_GpuSceneMaterialSrvs[matId] = m_RaytracingDescHeap.GetHandleAtOffset(descOffset).GetGpuHandle();
		descOffset += 2;
	}
}

void ModelViewer::InitRaytracingAS(ID3D12Device* pDevice)
{
	uint32_t numMeshes = m_Model->m_MeshCount;
	uint32_t numMaterials = m_Model->m_MaterialCount;

	const UINT numBottomLevels = 1;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO TLPreBuildInfo;
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC TLASDesc = {};
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& TLASInputs = TLASDesc.Inputs;
	TLASInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	TLASInputs.NumDescs = numBottomLevels;
	TLASInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	TLASInputs.pGeometryDescs = nullptr;
	TLASInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	m_RaytracingDevice->GetRaytracingAccelerationStructurePrebuildInfo(&TLASInputs, &TLPreBuildInfo);

	/**
	 * How to Compact AS in D3D12?
	 * https://alextardif.com/Compaction.html
	 * 
	 * D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION
	 */
	const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = 
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

	std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs(numMeshes);
	UINT64 scratchBufferSizeNeeded = TLPreBuildInfo.ScratchDataSizeInBytes;
	for (uint32_t i = 0; i < numMeshes; ++i)
	{
		const auto& mesh = m_Model->m_Meshes[i];

		D3D12_RAYTRACING_GEOMETRY_DESC& desc = geometryDescs[i];
		desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
#if 1
		bool bCutoutMat = m_Model->m_MaterialIsCutout[mesh.materialIndex];
		if (bCutoutMat) desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
#endif
		// NOTE::The any-hit shader will be ignored for acceleration structures created with the D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE flag.

		D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC& trianglesDesc = desc.Triangles; // union { Triangles, AABBs }
		trianglesDesc.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		trianglesDesc.VertexCount = mesh.vertexCount;
		trianglesDesc.VertexBuffer.StartAddress = m_Model->GetVertexBuffer().BufferLocation +
			(mesh.vertexDataByteOffset + mesh.attrib[(uint32_t)Model::Attrib::attrib_position].offset);
		trianglesDesc.VertexBuffer.StrideInBytes = mesh.vertexStride;
		trianglesDesc.IndexFormat = DXGI_FORMAT_R16_UINT;
		trianglesDesc.IndexCount = mesh.indexCount;
		trianglesDesc.IndexBuffer = m_Model->GetIndexBuffer().BufferLocation + mesh.indexDataByteOffset;
		trianglesDesc.Transform3x4 = 0;
	}

	std::vector<UINT64> BLASSize(numBottomLevels);
	std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC> BLASDescs(numBottomLevels);
	for (uint32_t i = 0; i < numBottomLevels; ++i)
	{
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& BLASDesc = BLASDescs[i];
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& BLASInputs = BLASDesc.Inputs;
		BLASInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		BLASInputs.NumDescs = numMeshes;
		BLASInputs.pGeometryDescs = &geometryDescs[i];
		BLASInputs.Flags = buildFlags;
		BLASInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO BLPreBuildInfo;
		m_RaytracingDevice->GetRaytracingAccelerationStructurePrebuildInfo(&BLASInputs, &BLPreBuildInfo);

		BLASSize[i] = BLPreBuildInfo.ResultDataMaxSizeInBytes;
		scratchBufferSizeNeeded = std::max(BLPreBuildInfo.ScratchDataSizeInBytes, scratchBufferSizeNeeded);
	}

	ByteAddressBuffer scratchBuffer;
	scratchBuffer.Create(m_RaytracingDevice.Get(), L"AS Scratch Buffer", (uint32_t)scratchBufferSizeNeeded, 1);

	D3D12_HEAP_PROPERTIES defaultHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto topLevelDesc = CD3DX12_RESOURCE_DESC::Buffer(TLPreBuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	pDevice->CreateCommittedResource(
		&defaultHeapDesc,
		D3D12_HEAP_FLAG_NONE,
		&topLevelDesc,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		nullptr,
		IID_PPV_ARGS(&m_TLAS)
	);
	m_TLAS->SetName(L"TLAS");

	TLASDesc.DestAccelerationStructureData = m_TLAS->GetGPUVirtualAddress();
	TLASDesc.ScratchAccelerationStructureData = scratchBuffer->GetGPUVirtualAddress();

	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs(numBottomLevels);
	m_BLAS.resize(numBottomLevels);
	for (uint32_t i = 0; i < numBottomLevels; ++i)
	{
		auto& blas = m_BLAS[i];

		auto bottomLevelDesc = CD3DX12_RESOURCE_DESC::Buffer(BLASSize[i], D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		pDevice->CreateCommittedResource(
			&defaultHeapDesc,
			D3D12_HEAP_FLAG_NONE,
			&bottomLevelDesc,
			D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
			nullptr,
			IID_PPV_ARGS(&blas)
		);

		BLASDescs[i].DestAccelerationStructureData = blas->GetGPUVirtualAddress();
		BLASDescs[i].ScratchAccelerationStructureData = scratchBuffer.GetGpuVirtualAddress();

		D3D12_RAYTRACING_INSTANCE_DESC& instanceDesc = instanceDescs[i];
		AllocateBufferUav(pDevice, *blas.Get(), m_RaytracingDescHeap);

		// Identity matrix
		ZeroMemory(instanceDesc.Transform, sizeof(instanceDesc.Transform));
		instanceDesc.Transform[0][0] = 1.0f;
		instanceDesc.Transform[1][1] = 1.0f;
		instanceDesc.Transform[2][2] = 1.0f;

		instanceDesc.AccelerationStructure = m_BLAS[i]->GetGPUVirtualAddress();
		instanceDesc.Flags = 0;
		instanceDesc.InstanceID = 0;
		instanceDesc.InstanceMask = 1;
		instanceDesc.InstanceContributionToHitGroupIndex = i;
	}

	ByteAddressBuffer instanceDataBuffer;
	instanceDataBuffer.Create(pDevice, L"Instance Data Buffer", numBottomLevels, sizeof(D3D12_RAYTRACING_INSTANCE_DESC), instanceDescs.data());

	TLASInputs.InstanceDescs = instanceDataBuffer.GetGpuVirtualAddress();
	TLASInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Build Acceleration Structures");
	ID3D12GraphicsCommandList* pCmdList = gfxContext.GetCommandList();

	ComPtr<ID3D12GraphicsCommandList4> pRaytracingCmdList;
	pCmdList->QueryInterface(IID_PPV_ARGS(&pRaytracingCmdList));

	ID3D12DescriptorHeap* descHeaps[] = { m_RaytracingDescHeap.GetHeapPointer() };
	pRaytracingCmdList->SetDescriptorHeaps(ARRAYSIZE(descHeaps), descHeaps);

	auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
	for (uint32_t i = 0, imax = (uint32_t)BLASDescs.size(); i < imax; ++i)
	{
		pRaytracingCmdList->BuildRaytracingAccelerationStructure(&BLASDescs[i], 0, nullptr);

		// If each BLAS build reuses the scratch buffer, you would need a UAV barrier between each. But without
		// barriers, the driver may be able to batch these BLAS builds together. This maximizes GPU utilization and
		// should execute more quickly.
	}

	pCmdList->ResourceBarrier(1, &uavBarrier);
	pRaytracingCmdList->BuildRaytracingAccelerationStructure(&TLASDesc, 0, nullptr);

	gfxContext.Finish(true);
}

template <size_t byteCodeLength>
void SetDxilLibrary(CD3DX12_STATE_OBJECT_DESC &stateObjectDesc, const unsigned char (&pShaderByteCode)[byteCodeLength], LPCWSTR entrypoint)
{
	auto dxilLibSubObject = stateObjectDesc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();

	D3D12_SHADER_BYTECODE shader = CD3DX12_SHADER_BYTECODE(pShaderByteCode, byteCodeLength);
	dxilLibSubObject->SetDXILLibrary(&shader);
	dxilLibSubObject->DefineExport(entrypoint);
}

template <size_t bytecodeLength>
void ReplaceDxilLibrary(D3D12_STATE_OBJECT_DESC *pStateObjectDesc, const unsigned char(&pShaderBytecode)[bytecodeLength], LPCWSTR entryPoint)
{
	for (UINT i = 0; i < pStateObjectDesc->NumSubobjects; ++i)
	{
		const D3D12_STATE_SUBOBJECT* pSubObject = &pStateObjectDesc->pSubobjects[i];

		if (pSubObject->Type == D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY)
		{
			D3D12_DXIL_LIBRARY_DESC* pLibDesc = (D3D12_DXIL_LIBRARY_DESC*)pSubObject->pDesc;

			for (UINT j = 0; j < pLibDesc->NumExports; ++j)
			{
				if (wcscmp(entryPoint, pLibDesc->pExports[j].Name) == 0)
				{
					pLibDesc->DXILLibrary = CD3DX12_SHADER_BYTECODE(pShaderBytecode, bytecodeLength);
				}
			}
		}
	}
}

#define USE_CD3DX12 1
void ModelViewer::InitRaytracingStateObjects(ID3D12Device *pDevice)
{
	uint32_t numMeshes = m_Model->m_MeshCount;
	uint32_t numMaterials = m_Model->m_MaterialCount;

	uint32_t descOffset = m_RaytracingDescHeap.GetAllocatedCount();

	SamplerDesc DefaultSamplerDesc;
	DefaultSamplerDesc.MaxAnisotropy = 8;

	/// Root signature
	// Global Raytracing RS
	m_GlobalRaytracingRS.Reset((UINT)RTGlobalRSId::Num, 4);
	m_GlobalRaytracingRS[(UINT)RTGlobalRSId::ViewUniforms].InitAsConstantBuffer(g_ViewUniformBufferParamsRegister, g_UniformBufferParamsSpace);
	m_GlobalRaytracingRS[(UINT)RTGlobalRSId::SceneBuffers].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,  1,  8); // SceneBuffers: MeshInfo+IB+VB+LightBuffer+...
	m_GlobalRaytracingRS[(UINT)RTGlobalRSId::HitConstants].InitAsConstantBuffer(0); // HitConstants
	m_GlobalRaytracingRS[(UINT)RTGlobalRSId::DynamicCB].InitAsConstantBuffer(1); // DynamicCB
	m_GlobalRaytracingRS[(UINT)RTGlobalRSId::InputTextures].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 12, 16); // DepthTexture, NormalTexture, ...
	m_GlobalRaytracingRS[(UINT)RTGlobalRSId::Outputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV,  2, 10); // u2->ColorOutput
	m_GlobalRaytracingRS[(UINT)RTGlobalRSId::U0].InitAsBufferUAV(0);
	m_GlobalRaytracingRS[(UINT)RTGlobalRSId::U1].InitAsBufferUAV(1);
	m_GlobalRaytracingRS[(UINT)RTGlobalRSId::AccelerationStructure].InitAsBufferSRV(0); // TLAS
	m_GlobalRaytracingRS[(UINT)RTGlobalRSId::ReSTIRGINewSamples].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 15, 3, 1);
	m_GlobalRaytracingRS[(UINT)RTGlobalRSId::ReSTIRGISPSamples].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 18, 5, 1);
	m_GlobalRaytracingRS[(UINT)RTGlobalRSId::ReSTIRGIOutputs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 5, 1);
	m_GlobalRaytracingRS.InitStaticSampler(0, DefaultSamplerDesc);
	m_GlobalRaytracingRS.InitStaticSampler(1, Graphics::s_CommonStates.SamplerLinearClampDesc);
	m_GlobalRaytracingRS.InitStaticSampler(2, Graphics::s_CommonStates.SamplerPointClampDesc);
	m_GlobalRaytracingRS.InitStaticSampler(3, Graphics::s_CommonStates.SamplerShadowDesc);
	m_GlobalRaytracingRS.Finalize(Graphics::s_Device, L"Global RaytracingRS");

	// Local Raytracing RS
	m_LocalRaytracingRS.Reset(2, 0);
	UINT sizeOfRootConstantInDwords = (sizeof(MaterialRootConstant) - 1) / sizeof(DWORD) + 1;
	m_LocalRaytracingRS[0].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6, 2, 1); // space = 1
	m_LocalRaytracingRS[1].InitAsConstants(3, sizeOfRootConstantInDwords, 1); // space = 1
	m_LocalRaytracingRS.Finalize(Graphics::s_Device, L"Local RaytracingRS", D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

	m_BufferCopyPSO.SetComputeShader(BufferCopyCS, sizeof(BufferCopyCS));
	m_BufferCopyPSO.SetRootSignature(m_GlobalRaytracingRS);
	m_BufferCopyPSO.Finalize(Graphics::s_Device);
	// ERROR::Root Signature doesn't match Compute Shader: SRV or UAV root descriptors can only be Raw or Structured buffers.
	// Textures should be put in DescriptorHeaps.

	m_ClearReservoirPSO.SetComputeShader(ClearReservoirs, sizeof(ClearReservoirs));
	m_ClearReservoirPSO.SetRootSignature(m_GlobalRaytracingRS);
	m_ClearReservoirPSO.Finalize(Graphics::s_Device);

	/// StateObjects

#if USE_CD3DX12
	CD3DX12_STATE_OBJECT_DESC stateObjectDesc(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
#endif

#if !USE_CD3DX12
	std::vector<D3D12_STATE_SUBOBJECT> subObjects;
#endif

	// Node mask
#if !USE_CD3DX12
	D3D12_STATE_SUBOBJECT nodeMaskSubObject;
	UINT nodeMask = 1;
	nodeMaskSubObject.pDesc = &nodeMask;
	nodeMaskSubObject.Type = D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK;
	subObjects.emplace_back(std::move(nodeMaskSubObject));
#else
	auto nodeMaskSubObject = stateObjectDesc.CreateSubobject<CD3DX12_NODE_MASK_SUBOBJECT>();
	nodeMaskSubObject->SetNodeMask(1);
#endif

	// Global RS
#if !USE_CD3DX12
	D3D12_STATE_SUBOBJECT rsSubObject;
	rsSubObject.pDesc = &m_GlobalRaytracingRS.GetSignatureRef();
	rsSubObject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
	subObjects.emplace_back(std::move(rsSubObject));
#else
	 auto rsSubObject = stateObjectDesc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
	 rsSubObject->SetRootSignature(m_GlobalRaytracingRS.GetSignature());
#endif

	// Pipeline Config
#if !USE_CD3DX12
	D3D12_STATE_SUBOBJECT configureSubobject;
	D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig;
	pipelineConfig.MaxTraceRecursionDepth = c_MaxRayRecursion;
	configureSubobject.pDesc = &pipelineConfig;
	configureSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
	subObjects.emplace_back(std::move(configureSubobject));
#else
	 auto configSubObject = stateObjectDesc.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
	 configSubObject->Config(c_MaxRayRecursion);
#endif

	std::vector<LPCWSTR> shadersToAssociate;
	// Ray Gen shader stuff
	// --------------------------------------------------------
	LPCWSTR rayGenExportName = L"RayGen";
#if!USE_CD3DX12
	subObjects.emplace_back(D3D12_STATE_SUBOBJECT{});
	auto& rayGenDxilLibSubobject = subObjects.back();
	D3D12_EXPORT_DESC rayGenExportDesc;
	D3D12_DXIL_LIBRARY_DESC rayGenDxilLibDesc = {};
	rayGenDxilLibSubobject = CreateDxilLibrary(rayGenExportName, RayGenShaderLib, sizeof(RayGenShaderLib), rayGenDxilLibDesc, rayGenExportDesc);
	shadersToAssociate.push_back(rayGenShaderExportName);
#else
	SetDxilLibrary(stateObjectDesc, RayGenShaderLib, rayGenExportName);
#endif

	// Hit Group shader stuff
	// --------------------------------------------------------
	LPCWSTR hitExportName = L"Hit";
#if !USE_CD3DX12
	D3D12_EXPORT_DESC hitGroupExportDesc = {};
	D3D12_DXIL_LIBRARY_DESC hitGroupDxilLibDesc = {};
	D3D12_STATE_SUBOBJECT hitGroupLibSubobject = CreateDxilLibrary(hitExportName, HitShaderLib, sizeof(HitShaderLib), hitGroupDxilLibDesc, hitGroupExportDesc);
	subObjects.emplace_back(std::move(hitGroupLibSubobject));
	shadersToAssociate.push_back(hitExportName);
#else
	SetDxilLibrary(stateObjectDesc, HitShaderLib, hitExportName);
#endif

	LPCWSTR anyHitExportName = L"AnyHit";
	SetDxilLibrary(stateObjectDesc, HitShaderLib, anyHitExportName);

	// Miss shader stuff
	// --------------------------------------------------------
	LPCWSTR missExportName = L"Miss";
#if !USE_CD3DX12
	D3D12_EXPORT_DESC missExportDesc;
	D3D12_DXIL_LIBRARY_DESC missDxilLibDesc = {};
	D3D12_STATE_SUBOBJECT missLibSubobject = CreateDxilLibrary(missExportName, MissShaderLib, sizeof(MissShaderLib), missDxilLibDesc, missExportDesc);
	subObjects.emplace_back(std::move(missLibSubobject));

	D3D12_STATE_SUBOBJECT& missDxilLibSubobject = subObjects.back();
	shadersToAssociate.push_back(missExportName);
#else
	SetDxilLibrary(stateObjectDesc, MissShaderLib, missExportName);
#endif

	// Shader Config
#if !USE_CD3DX12
	D3D12_STATE_SUBOBJECT shaderConfigStateObject;
	D3D12_RAYTRACING_SHADER_CONFIG shaderConfig;
	shaderConfig.MaxAttributeSizeInBytes = 8;
	shaderConfig.MaxPayloadSizeInBytes = 8;
	shaderConfigStateObject.pDesc = &shaderConfig;
	shaderConfigStateObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
	subObjects.emplace_back(std::move(shaderConfigStateObject));
	D3D12_STATE_SUBOBJECT& shaderConfigSubobject = subObjects.back();
#else
	auto shaderConfigStateObject = stateObjectDesc.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
	shaderConfigStateObject->Config(c_MaxPayloadSize, c_MaxAttributeSize);
#endif

	// Hit Group (Intersection + AnyHit + ClosestHit)
	LPCWSTR hitGroupExportName = L"HitGroup";
#if !USE_CD3DX12
	D3D12_HIT_GROUP_DESC hitGroupDesc = {};
	hitGroupDesc.ClosestHitShaderImport = hitExportName;
	// hitGroupDesc.IntersectionShaderImport = nullptr;
	// hitGroupDesc.AnyHitShaderImport = nullptr;
	hitGroupDesc.HitGroupExport = hitGroupExportName;
	D3D12_STATE_SUBOBJECT hitGroupSubobject = {};
	hitGroupSubobject.pDesc = &hitGroupDesc;
	hitGroupSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
	subObjects.emplace_back(std::move(hitGroupSubobject));
#else
	auto hitGroupSubObject = stateObjectDesc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
	hitGroupSubObject->SetHitGroupExport(hitGroupExportName);
	hitGroupSubObject->SetAnyHitShaderImport(anyHitExportName);
	hitGroupSubObject->SetClosestHitShaderImport(hitExportName);
#endif

	// Local RS
#if !USE_CD3DX12
	D3D12_STATE_SUBOBJECT localRSSubobject;
	localRSSubobject.pDesc = &m_LocalRaytracingRS.GetSignatureRef();
	localRSSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
	subObjects.emplace_back(std::move(localRSSubobject));
	D3D12_STATE_SUBOBJECT& loalRSSubobject = subObjects.back();
#else
	auto localRSSubObject = stateObjectDesc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
	localRSSubObject->SetRootSignature(m_LocalRaytracingRS.GetSignature());
#endif

#if !USE_CD3DX12
	D3D12_STATE_OBJECT_DESC stateObjectDesc;
	stateObject.NumSubobjects = (UINT)subObjects.size();
	stateObject.pSubobjects = subObjects.data();
	stateObject.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
#endif

	constexpr UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
#define ALIGN(alignment, num) ((((num) + alignment - 1) / alignment) * alignment)
// If num is POT	
// #define ALIGN(alignment, num) ( alignment + num-1 ) & (~(num-1))
	constexpr UINT offsetToDescriptorHandle = ALIGN(sizeof(D3D12_GPU_DESCRIPTOR_HANDLE), shaderIdentifierSize);
	constexpr UINT offsetToMaterialConstants= ALIGN(sizeof(UINT32), offsetToDescriptorHandle + sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
	constexpr UINT shaderRecordSizeInBytes  = ALIGN(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, offsetToMaterialConstants + sizeof(MaterialRootConstant));

#undef ALIGN

	std::vector<byte> pHitShaderTable(shaderRecordSizeInBytes * numMeshes);
	auto GetShaderTable = [&](const Model& model, ID3D12StateObject* pPSO, byte* pShaderTable)
	{
		ID3D12StateObjectProperties* stateObjectProperties = nullptr;
		ThrowIfFailed(pPSO->QueryInterface(IID_PPV_ARGS(&stateObjectProperties)));
		void* pHitGroupIdentifierData = stateObjectProperties->GetShaderIdentifier(hitGroupExportName);
		for (uint32_t i = 0; i < numMeshes; ++i)
		{
			byte* pShaderRecord = i * shaderRecordSizeInBytes + pShaderTable;
			memcpy(pShaderRecord, pHitGroupIdentifierData, shaderIdentifierSize);

			uint32_t materialIdx = model.m_Meshes[i].materialIndex;
			memcpy(pShaderRecord + offsetToDescriptorHandle, &m_GpuSceneMaterialSrvs[materialIdx].ptr, sizeof(m_GpuSceneMaterialSrvs[materialIdx].ptr));

			MaterialRootConstant material;
			material.MaterialID = i;
			memcpy(pShaderRecord + offsetToMaterialConstants, &material, sizeof(material));
		}
	};

	/**
	 * This causes the CD3DX12_STATE_OBJECT_DESC to generate the final D3D12_STATE_OBJECT_DESC, after which we aren't supposed to make any more changes.
	 * But we *are* going to make changes to override the shader library bytecode to make various permutations of the state object. For this reason,
	 * we must const_cast the state object description. ReplaceDxilLibrary() will not accept a const D3D12_STATE_OBJECT_DESC.
	 */
#if USE_CD3DX12
	D3D12_STATE_OBJECT_DESC* pStateObjectDesc = const_cast<D3D12_STATE_OBJECT_DESC*>((const D3D12_STATE_OBJECT_DESC*)stateObjectDesc);
#else
	D3D12_STATE_OBJECT_DESC* pStateObjectDesc = &stateObjectDesc;
#endif

	{
		ComPtr<ID3D12StateObject> pBarycentricPSO;
		m_RaytracingDevice->CreateStateObject(pStateObjectDesc, IID_PPV_ARGS(&pBarycentricPSO));

		GetShaderTable(*m_Model, pBarycentricPSO.Get(), pHitShaderTable.data());
		m_RaytracingInputs[(uint32_t)RaytracingType::Primarybarycentric] = RaytracingDispatchRayInputs(m_RaytracingDevice.Get(), pBarycentricPSO.Get(),
			pHitShaderTable.data(), shaderRecordSizeInBytes, (UINT)pHitShaderTable.size(), rayGenExportName, missExportName);
	}

	{
#if !USE_CD3DX12
		rayGenDxilLibSubobject = CreateDxilLibrary(rayGenShaderExportName, nullptr, 0, rayGenDxilLibDesc, rayGenExportDesc);
#else
		ReplaceDxilLibrary(pStateObjectDesc, RayGenShaderSSRLib, rayGenExportName);
#endif
		ComPtr<ID3D12StateObject> pReflectionbarycentricPSO;
		m_RaytracingDevice->CreateStateObject(pStateObjectDesc, IID_PPV_ARGS(&pReflectionbarycentricPSO));

		GetShaderTable(*m_Model, pReflectionbarycentricPSO.Get(), pHitShaderTable.data());
		m_RaytracingInputs[(uint32_t)RaytracingType::Reflectionbarycentric] = RaytracingDispatchRayInputs(m_RaytracingDevice.Get(), pReflectionbarycentricPSO.Get(),
			pHitShaderTable.data(), shaderRecordSizeInBytes, (UINT)pHitShaderTable.size(), rayGenExportName, missExportName);
	}

	{
#if !USE_CD3DX12
		rayGenDxilLibSubobject = CreateDxilLibrary(rayGenShaderExportName, nullptr, 0, rayGenDxilLibDesc, rayGenExportDesc);
#else
		ReplaceDxilLibrary(pStateObjectDesc, RayGenShaderShadowsLib, rayGenExportName);
		ReplaceDxilLibrary(pStateObjectDesc, MissShadowsLib, missExportName);
#endif
		ComPtr<ID3D12StateObject> pShadowPSO;
		m_RaytracingDevice->CreateStateObject(pStateObjectDesc, IID_PPV_ARGS(&pShadowPSO));

		GetShaderTable(*m_Model, pShadowPSO.Get(), pHitShaderTable.data());
		m_RaytracingInputs[(int32_t)RaytracingType::Shadows] = RaytracingDispatchRayInputs(m_RaytracingDevice.Get(), pShadowPSO.Get(),
			pHitShaderTable.data(), shaderRecordSizeInBytes, (UINT)pHitShaderTable.size(), rayGenExportName, missExportName);
	}

	{
#if !USE_CD3DX12
		rayGenDxilLibSubobject = CreateDxilLibrary(rayGenShaderExportName, nullptr, 0, rayGenDxilLibDesc, rayGenExportDesc);
		hitGroupLibSubobject = CreateDxilLibrary(hitExportName, nullptr, 0, hitGroupDxilLibDesc, hitGroupExportDesc);
		missDxilLibSubobject = CreateDxilLibrary(missExportName, nullptr, 0, missDxilLibDesc, missExportDesc);
#else
		ReplaceDxilLibrary(pStateObjectDesc, RayGenShaderLib, rayGenExportName);
		ReplaceDxilLibrary(pStateObjectDesc, DiffuseHitShaderLib, hitExportName);
		ReplaceDxilLibrary(pStateObjectDesc, DiffuseHitShaderLib, anyHitExportName);
		ReplaceDxilLibrary(pStateObjectDesc, MissShaderLib, missExportName);
#endif
		ComPtr<ID3D12StateObject> pDiffusePSO;
		m_RaytracingDevice->CreateStateObject(pStateObjectDesc, IID_PPV_ARGS(&pDiffusePSO));

		GetShaderTable(*m_Model, pDiffusePSO.Get(), pHitShaderTable.data());
		m_RaytracingInputs[(uint32_t)RaytracingType::DiffuseHitShader] = RaytracingDispatchRayInputs(m_RaytracingDevice.Get(), pDiffusePSO.Get(),
			pHitShaderTable.data(), shaderRecordSizeInBytes, (UINT)pHitShaderTable.size(), rayGenExportName, missExportName);
	}

	{
#if !USE_CD3DX12
		rayGenDxilLibSubobject = CreateDxilLibrary(rayGenShaderExportName, nullptr, 0, rayGenDxilLibDesc, rayGenExportDesc);
		hitGroupLibSubobject = CreateDxilLibrary(hitGroupExportName, nullptr, 0, hitGroupDxilLibDesc, hitGroupExportDesc);
		missDxilLibSubobject = CreateDxilLibrary(missExportName, nullptr, 0, missDxilLibDesc, missExportDesc);
#else
		ReplaceDxilLibrary(pStateObjectDesc, RayGenShaderSSRLib, rayGenExportName);
		ReplaceDxilLibrary(pStateObjectDesc, DiffuseHitShaderLib, hitExportName);
		ReplaceDxilLibrary(pStateObjectDesc, MissShaderLib, missExportName);
#endif

		ComPtr<ID3D12StateObject> pReflectionPSO;
		m_RaytracingDevice->CreateStateObject(pStateObjectDesc, IID_PPV_ARGS(&pReflectionPSO));

		GetShaderTable(*m_Model, pReflectionPSO.Get(), pHitShaderTable.data());
		m_RaytracingInputs[(uint32_t)RaytracingType::Reflection] = RaytracingDispatchRayInputs(m_RaytracingDevice.Get(), pReflectionPSO.Get(),
			pHitShaderTable.data(), shaderRecordSizeInBytes, (UINT)pHitShaderTable.size(), rayGenExportName, missExportName);
	}

	// Reference path tracing
	{
		ReplaceDxilLibrary(pStateObjectDesc, SimplePathTracing, rayGenExportName);
		ReplaceDxilLibrary(pStateObjectDesc, SimplePathTracing, hitExportName);
		ReplaceDxilLibrary(pStateObjectDesc, SimplePathTracing, missExportName);
		ReplaceDxilLibrary(pStateObjectDesc, SimplePathTracing, anyHitExportName);

		ComPtr<ID3D12StateObject> pSimplePathTracingPSO;
		m_RaytracingDevice->CreateStateObject(pStateObjectDesc, IID_PPV_ARGS(&pSimplePathTracingPSO));

		GetShaderTable(*m_Model, pSimplePathTracingPSO.Get(), pHitShaderTable.data());
		m_RaytracingInputs[(uint32_t)RaytracingType::ReferencePathTracing] = RaytracingDispatchRayInputs(m_RaytracingDevice.Get(), pSimplePathTracingPSO.Get(),
			pHitShaderTable.data(), shaderRecordSizeInBytes, (UINT)pHitShaderTable.size(), rayGenExportName, missExportName);
	}

	// ReSTIR with direct lights
	{
		ReplaceDxilLibrary(pStateObjectDesc, ReSTIRWithDirectLighting, rayGenExportName);
		ReplaceDxilLibrary(pStateObjectDesc, ReSTIRWithDirectLighting, hitExportName);
		ReplaceDxilLibrary(pStateObjectDesc, ReSTIRWithDirectLighting, missExportName);

		ComPtr<ID3D12StateObject> pReSTIRWithDirectLightingPSO;
		m_RaytracingDevice->CreateStateObject(pStateObjectDesc, IID_PPV_ARGS(&pReSTIRWithDirectLightingPSO));

		GetShaderTable(*m_Model, pReSTIRWithDirectLightingPSO.Get(), pHitShaderTable.data());
		m_RaytracingInputs[(uint32_t)RaytracingType::ReSTIRWithDirectLights] = RaytracingDispatchRayInputs(m_RaytracingDevice.Get(), pReSTIRWithDirectLightingPSO.Get(),
			pHitShaderTable.data(), shaderRecordSizeInBytes, (UINT)pHitShaderTable.size(), rayGenExportName, missExportName);
	}

	// ReSTIR GI
	{
		ReplaceDxilLibrary(pStateObjectDesc, ReSTIR_TraceDiffuse, rayGenExportName);
		ReplaceDxilLibrary(pStateObjectDesc, ReSTIR_TraceDiffuse, hitExportName);
		ReplaceDxilLibrary(pStateObjectDesc, ReSTIR_TraceDiffuse, missExportName);

		ComPtr<ID3D12StateObject> pReSTIRGIPSO;
		m_RaytracingDevice->CreateStateObject(pStateObjectDesc, IID_PPV_ARGS(&pReSTIRGIPSO));

		GetShaderTable(*m_Model, pReSTIRGIPSO.Get(), pHitShaderTable.data());
		m_ReSTIRGI->m_ReSTIRGIInputs = RaytracingDispatchRayInputs(m_RaytracingDevice.Get(), pReSTIRGIPSO.Get(),
			pHitShaderTable.data(), shaderRecordSizeInBytes, (UINT)pHitShaderTable.size(), rayGenExportName, missExportName);
	}

	for (auto& raytracingPipelineState : m_RaytracingInputs)
	{
		WCHAR hitGroupExportNameClosestHitType[64];
		swprintf_s(hitGroupExportNameClosestHitType, L"%s::closesthit", hitGroupExportName);
		SetPipelineStateStackSize(rayGenExportName, hitGroupExportNameClosestHitType, missExportName, c_MaxRayRecursion, raytracingPipelineState.GetPSO());
	}

}

void ModelViewer::CleanRaytracing()
{
	m_RaytracingDescHeap.Destroy();

	m_HitConstantBuffer.Destroy();
	m_DynamicConstantBuffer.Destroy();

	if (m_bEnablePathTracing)
		m_AccumulationBuffer.Destroy();

	if (m_bEnableReSTIRDI)
	{
		m_ReservoirBuffer[0].Destroy();
		m_ReservoirBuffer[1].Destroy();
	}

	for (uint32_t i = 0; i < (uint32_t)RaytracingType::Num; ++i)
	{
		m_RaytracingInputs[i].Cleanup();
	}

	// m_GpuSceneMaterialSrvs.

	if (m_bEnableReSTIRGI)
		m_ReSTIRGI->Shutdown();
}


/**
	Pipeline: (Forward+ / TiledBasedForward)
	RenderLightShadows => ZPrePass (DepthOnly ->Depth) => FillLightGrid (CullLights) =>
	ModelViewer
*/
