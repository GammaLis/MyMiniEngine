#include "ModelViewer.h"
#include "Graphics.h"
#include "CommandContext.h"
#include "TextureManager.h"
#include "Model.h"
#include "Effects.h"

#include <atlbase.h>

// shaders
#include "DepthViewerVS.h"
#include "DepthViewerPS.h"
#include "ModelViewerVS.h"
#include "ModelViewerPS.h"

// 临时
#include "LinearizeDepthCS.h"

using namespace MyDirectX;

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

ModelViewer::ModelViewer(HINSTANCE hInstance, const char *modelName, const wchar_t* title, UINT width, UINT height)
	: IGameApp(hInstance, title, width, height)
{
	m_ModelName = modelName;
}

void ModelViewer::Update(float deltaTime)
{
	IGameApp::Update(deltaTime);

	m_CameraController->Update(deltaTime);
	m_ViewProjMatrix = m_Camera.GetViewProjMatrix();

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
	
	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto bufferWidth = colorBuffer.GetWidth(), bufferHeight = colorBuffer.GetHeight();
	
	// >>> test (改变Viewport位置，尺寸，Scissor位置，尺寸)
	// m_MainViewport.TopLeftX = m_MainViewport.TopLeftY = 0.0f;
	// m_MainViewport.TopLeftX = 10;
	// m_MainViewport.TopLeftY = 10;
	// <<< test end
	m_MainViewport.Width = (float)bufferWidth;
	m_MainViewport.Height = (float)bufferHeight;
	m_MainViewport.MinDepth = 0.0f;
	m_MainViewport.MaxDepth = 1.0f;

	m_MainScissor.left = 0;
	m_MainScissor.top = 0;
	m_MainScissor.right = (LONG)bufferWidth;
	m_MainScissor.bottom = (LONG)bufferHeight;

	// update particles
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

	PSConstants psConstants;
	psConstants._SunDirection = m_SunDirection;
	psConstants._SunLight = Math::Vector3(1.0f) * m_CommonStates.SunLightIntensity;
	psConstants._AmbientLight = Math::Vector3(1.0f) * m_CommonStates.AmbientIntensity;
	
	psConstants._ShadowTexelSize[0] = 1.0f / shadowBuffer.GetWidth();
	psConstants._ShadowTexelSize[1] = 1.0f / shadowBuffer.GetHeight();

	const auto& forwardPlusLighting = Effects::s_ForwardPlusLighting;
	psConstants._InvTileDim[0] = 1.0f / forwardPlusLighting.m_LightGridDim;
	psConstants._InvTileDim[1] = 1.0f / forwardPlusLighting.m_LightGridDim;

	psConstants._TileCount[0] = Math::DivideByMultiple(colorBuffer.GetWidth(), forwardPlusLighting.m_LightGridDim);
	psConstants._TileCount[1] = Math::DivideByMultiple(colorBuffer.GetHeight(), forwardPlusLighting.m_LightGridDim);
	psConstants._FirstLightIndex[0] = forwardPlusLighting.m_FirstConeLight;
	psConstants._FirstLightIndex[1] = forwardPlusLighting.m_FirstConeShadowedLight;
	psConstants._FrameIndexMod2 = frameIndex & 1;
	// ...

	GraphicsContext &gfxContext = GraphicsContext::Begin(L"Scene Render");

	// set the default state for command lists
	auto pfnSetupGraphicsState = [&]()
	{
		gfxContext.SetRootSignature(m_RootSig);
		gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		gfxContext.SetVertexBuffer(0, m_Model->m_VertexBuffer.VertexBufferView());
		gfxContext.SetIndexBuffer(m_Model->m_IndexBuffer.IndexBufferView());
	};
	pfnSetupGraphicsState();

	// MaxLights 个 shadow pass，分帧进行，一帧一个
	RenderLightShadows(gfxContext);

	// z prepass
	{
		// opaque 
		{
			gfxContext.SetDynamicConstantBufferView((uint32_t)RSId::kMaterialConstants, sizeof(psConstants), &psConstants);

			gfxContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
			gfxContext.ClearDepth(depthBuffer);

			gfxContext.SetDepthStencilTarget(depthBuffer.GetDSV());
			gfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);

			gfxContext.SetPipelineState(m_DepthPSO);
			RenderObjects(gfxContext, m_ViewProjMatrix, ObjectFilter::kOpaque);
		}

		// cutout
		{
			gfxContext.SetPipelineState(m_CutoutDepthPSO);
			RenderObjects(gfxContext, m_ViewProjMatrix, ObjectFilter::kCutout);
		}
	}

	// 
	{
		// 暂时先在这里 计算线性深度 （实际应在SSAO中计算，目前没有实现）
		// linearize depth
		{
			auto& computeContext = gfxContext.GetComputeContext();
			computeContext.SetRootSignature(m_LinearDepthRS);
			computeContext.SetPipelineState(m_LinearDepthCS);

			auto& linearDepth = Graphics::s_BufferManager.m_LinearDepth[frameIndex % 2];
			computeContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			computeContext.TransitionResource(linearDepth, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			
			float farClip = m_Camera.GetFarClip(), nearClip = m_Camera.GetNearClip();
			const float zMagic = (farClip - nearClip) / nearClip;
			computeContext.SetConstant(0, zMagic);
			computeContext.SetDynamicDescriptor(1, 0, depthBuffer.GetDepthSRV());
			computeContext.SetDynamicDescriptor(2, 0, linearDepth.GetUAV());
			computeContext.Dispatch2D(linearDepth.GetWidth(), linearDepth.GetHeight(), 16, 16);

		}

		// CS - fill light grid
		Effects::s_ForwardPlusLighting.FillLightGrid(gfxContext, m_Camera, frameIndex);
	}

	// main render
	{
		gfxContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfxContext.TransitionResource(normalBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfxContext.ClearColor(colorBuffer);

		pfnSetupGraphicsState();

		auto& shadowBuffer = Graphics::s_BufferManager.m_ShadowBuffer;
		// render shadow map
		{
			m_SunShadow.UpdateMatrix(-m_SunDirection, Math::Vector3(0, -500.0f, 0),
				Math::Vector3(m_CommonStates.ShadowDimX, m_CommonStates.ShadowDimY, m_CommonStates.ShadowDimZ),
				shadowBuffer.GetWidth(), shadowBuffer.GetHeight(), 16);

			shadowBuffer.BeginRendering(gfxContext);

			gfxContext.SetPipelineState(m_ShadowPSO);
			RenderObjects(gfxContext, m_SunShadow.GetViewProjMatrix(), ObjectFilter::kOpaque);
			gfxContext.SetPipelineState(m_CutoutShadowPSO);
			RenderObjects(gfxContext, m_SunShadow.GetViewProjMatrix(), ObjectFilter::kCutout);

			shadowBuffer.EndRendering(gfxContext);
		}

		// render color
		{
			gfxContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);

			D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = { colorBuffer.GetRTV(), normalBuffer.GetRTV() };
			gfxContext.SetRenderTargets(ARRAYSIZE(rtvs), rtvs, depthBuffer.GetDSV_DepthReadOnly());
			gfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);

			gfxContext.SetDynamicConstantBufferView((uint32_t)RSId::kMaterialConstants, sizeof(psConstants), &psConstants);
			gfxContext.SetDynamicDescriptors((uint32_t)RSId::kCommonSRVs, 0, _countof(m_ExtraTextures), m_ExtraTextures);

			// ->opauqe
			gfxContext.SetPipelineState(m_ModelPSO);
			RenderObjects(gfxContext, m_ViewProjMatrix, ObjectFilter::kOpaque);

			// ->cutout
			gfxContext.SetPipelineState(m_CutoutModelPSO);
			RenderObjects(gfxContext, m_ViewProjMatrix, ObjectFilter::kCutout);
		}
	}

	// skybox
	m_Skybox.Render(gfxContext, m_Camera);

	// effects
	{
		// some systems generate a per-pixel velocity buffer to better track dynamic and skinned meshes.
		// everything is static in our scene, so we generate velocity from camera motion and the depth buffer.
		// a velocity buffer is necessary for all temporal effects (and motion blur)
		Effects::s_MotionBlur.GenerateCameraVelocityBuffer(gfxContext, m_Camera, frameIndex, true);

		Effects::s_TemporalAA.ResolveImage(gfxContext);

		// particle effects
		auto& linearDepth = Graphics::s_BufferManager.m_LinearDepth[frameIndex % 2];
		Effects::s_ParticleEffectManager.Render(gfxContext, m_Camera, colorBuffer, depthBuffer, linearDepth);
	}

	
	// 普通渲染顺序，不经z prepass
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

	gfxContext.Finish();
}

void ModelViewer::Raytrace()
{

}

void ModelViewer::InitPipelineStates()
{
	SamplerDesc DefaultSamplerDesc;
	DefaultSamplerDesc.MaxAnisotropy = 8;

	SamplerDesc CubeMapSamplerDesc = DefaultSamplerDesc;
	// CubeMapSamplerDesc.MaxLOD = 6.0f;

	// root signature
	m_RootSig.Reset((uint32_t)RSId::kNum, 3);
	m_RootSig[(uint32_t)RSId::kMeshConstants].InitAsConstantBuffer(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig[(uint32_t)RSId::kMaterialConstants].InitAsConstantBuffer(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[(uint32_t)RSId::kMaterialSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 10, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[(uint32_t)RSId::kMaterialSamplers].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0, 10, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[(uint32_t)RSId::kCommonCBV].InitAsConstants(1, 2);
	m_RootSig[(uint32_t)RSId::kCommonSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 10, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[(uint32_t)RSId::kSkinMatrices].InitAsBufferSRV(20, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig.InitStaticSampler(10, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig.InitStaticSampler(11, Graphics::s_CommonStates.SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig.InitStaticSampler(12, CubeMapSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig.Finalize(Graphics::s_Device, L"ModelViewer", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	DXGI_FORMAT colorFormat = Graphics::s_BufferManager.m_SceneColorBuffer.GetFormat();
	DXGI_FORMAT normalFormat = Graphics::s_BufferManager.m_SceneNormalBuffer.GetFormat();
	DXGI_FORMAT depthFormat = Graphics::s_BufferManager.m_SceneDepthBuffer.GetFormat();
	DXGI_FORMAT shadowFormat = Graphics::s_BufferManager.m_ShadowBuffer.GetFormat();

	// input elements
	D3D12_INPUT_ELEMENT_DESC inputElements[] =
	{
		{ "POSITION",	0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",	0, DXGI_FORMAT_R32G32_FLOAT,	0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",		0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",	0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BITANGENT",	0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	// PSOs
	// depth pso
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

	// depth-only shading but with alpha-testing
	m_CutoutDepthPSO = m_DepthPSO;
	m_CutoutDepthPSO.SetPixelShader(DepthViewerPS, sizeof(DepthViewerPS));
	m_CutoutDepthPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerTwoSided);
	m_CutoutDepthPSO.Finalize(Graphics::s_Device);

	// depth-only but with a depth bias and/or render only backfaces
	m_ShadowPSO = m_DepthPSO;
	m_ShadowPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerShadow);
	m_ShadowPSO.SetRenderTargetFormats(0, nullptr, shadowFormat);
	m_ShadowPSO.Finalize(Graphics::s_Device);

	// shadows with alpha testing
	m_CutoutShadowPSO = m_ShadowPSO;
	m_CutoutShadowPSO.SetPixelShader(DepthViewerPS, sizeof(DepthViewerPS));
	m_CutoutShadowPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerShadowTwoSided);
	m_CutoutShadowPSO.Finalize(Graphics::s_Device);

	DXGI_FORMAT modelViewerFormats[2] = { colorFormat, normalFormat };

	// model viewer pso
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

	// ****************************************
	// 临时设置，后面需要放到别处 -20-2-21
	// linear depth
	m_LinearDepthRS.Reset(3, 0);
	m_LinearDepthRS[0].InitAsConstants(0, 1);
	m_LinearDepthRS[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
	m_LinearDepthRS[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
	m_LinearDepthRS.Finalize(Graphics::s_Device, L"Linear Depth");

	m_LinearDepthCS.SetRootSignature(m_LinearDepthRS);
	m_LinearDepthCS.SetComputeShader(LinearizeDepthCS, sizeof(LinearizeDepthCS));
	m_LinearDepthCS.Finalize(Graphics::s_Device);
	// ****************************************
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
	// camera
	float modelRadius = Math::Length(m_Model->m_BoundingBox.max - m_Model->m_BoundingBox.min) * 0.5f;
	const Math::Vector3 eye = (m_Model->m_BoundingBox.min + m_Model->m_BoundingBox.max) * 0.5f + Math::Vector3(modelRadius * 0.5f, 0.0f, 0.0f);
	m_Camera.SetEyeAtUp(eye, Math::Vector3(Math::kZero), Math::Vector3(Math::kYUnitVector));
	m_Camera.SetZRange(1.0f, 10000.0f);
	// m_Camera.Update();	// 若无CameraController，需要手动更新
	m_CameraController.reset(new CameraController(m_Camera, Math::Vector3(Math::kYUnitVector), *m_Input));

	// effects
	// ...
	m_ExtraTextures[0] = Graphics::s_TextureManager.GetWhiteTex2D().GetSRV();	// 暂时为空
	m_ExtraTextures[1] = Graphics::s_BufferManager.m_ShadowBuffer.GetSRV();

	// forward+ lighting
	const auto &boundingBox = m_Model->GetBoundingBox();
	auto& forwardPlusLighting = Effects::s_ForwardPlusLighting;
	forwardPlusLighting.CreateRandomLights(Graphics::s_Device, boundingBox.min, boundingBox.max);

	m_ExtraTextures[2] = forwardPlusLighting.m_LightBuffer.GetSRV();
	m_ExtraTextures[3] = forwardPlusLighting.m_LightGrid.GetSRV();
	m_ExtraTextures[4] = forwardPlusLighting.m_LightGridBitMask.GetSRV();
	m_ExtraTextures[5] = forwardPlusLighting.m_LightShadowArray.GetSRV();

	// skybox
	m_Skybox.Init(Graphics::s_Device, L"grasscube1024");

	// particle effects
	CreateParticleEffects();

	Effects::s_MotionBlur.m_Enabled = true;
	Effects::s_TemporalAA.m_Enabled = true;
	Effects::s_PostEffects.m_CommonStates.EnableHDR = true;
	Effects::s_PostEffects.m_CommonStates.EnableAdaption = true;
}

void ModelViewer::CleanCustom()
{
	m_Model->Cleanup();
	m_Skybox.Shutdown();
}

void ModelViewer::PostProcess()
{
	Effects::s_PostEffects.Render();
}

// render light shadows
// 先将shadow深度渲染到Light::m_LightShadowTempBuffer,然后将其CopySubResource到Light::m_LightShadowArray.
void ModelViewer::RenderLightShadows(GraphicsContext& gfxContext)
{
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

void ModelViewer::RenderObjects(GraphicsContext& gfxContext, const Math::Matrix4 viewProjMat, ObjectFilter filter)
{
	VSConstants vsConstants;
	vsConstants._ModelToProjection = Math::Transpose(viewProjMat);	// HLSL - 对应 mul(float4(pos), mat)
	// vsConstants._ModelToProjection = (viewProjMat);	// HLSL - 对应 mul(mat, float4(pos))
	vsConstants._ModelToShadow = Math::Transpose(m_SunShadow.GetShadowMatrix());
	XMStoreFloat3(&vsConstants._CamPos, m_Camera.GetPosition());

	gfxContext.SetDynamicConstantBufferView((uint32_t)RSId::kMeshConstants, sizeof(vsConstants), &vsConstants);

	uint32_t curMatIdx = 0xFFFFFFFFul;

	uint32_t vertexStride = m_Model->m_VertexStride;

	for (uint32_t meshIndex = 0; meshIndex < m_Model->m_MeshCount; ++meshIndex)
	{
		const Model::Mesh& mesh = m_Model->m_pMesh[meshIndex];

		uint32_t indexCount = mesh.indexCount;
		uint32_t indexOffset = mesh.indexDataByteOffset / sizeof(uint16_t);
		uint32_t vertexOffset = mesh.vertexDataByteOffset / vertexStride;

		if (mesh.materialIndex != curMatIdx)
		{
			// filter objects
			bool bCutoutMat = m_Model->m_MaterialIsCutout[mesh.materialIndex];
			if ( bCutoutMat && !((uint32_t)filter & (uint32_t)ObjectFilter::kCutout) ||
				!bCutoutMat && !((uint32_t)filter & (uint32_t)ObjectFilter::kOpaque))
				continue;

			curMatIdx = mesh.materialIndex;
			gfxContext.SetDynamicDescriptors((uint32_t)RSId::kMaterialSRVs, 0, 6, m_Model->GetSRVs(curMatIdx));
			
			gfxContext.SetConstants((uint32_t)RSId::kCommonCBV, vertexOffset, curMatIdx);
		}

		gfxContext.DrawIndexed(indexCount, indexOffset, vertexOffset);
	}

}

void ModelViewer::CreateParticleEffects()
{
	using namespace ParticleEffects;
	auto& particleEffectManager = Effects::s_ParticleEffectManager;
	Vector3 camPos = m_Camera.GetPosition();

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

struct RaytracingDispatchRayInputs
{
	RaytracingDispatchRayInputs() {  }
	RaytracingDispatchRayInputs(
		ID3D12Device *pDevice,
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
		void* pRayGenShaderData = stateObjectProperties->GetShaderIdentifier(rayGenExportName);
		void* pMissShaderData = stateObjectProperties->GetShaderIdentifier(missExportName);

		m_HitGroupStride = HitGroupStride;

		// MiniEngine requires that all initial data be aligned to 16 bytes
		UINT alignment = 16;
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

	D3D12_DISPATCH_RAYS_DESC GetDispatchRayDesc(UINT DispatchWidth, UINT DispatchHeight)
	{
		D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = {};
		dispatchRaysDesc.RayGenerationShaderRecord.StartAddress = m_RayGenShaderTable.GetGpuVirtualAddress();
		dispatchRaysDesc.RayGenerationShaderRecord.SizeInBytes = m_RayGenShaderTable.GetBufferSize();
		dispatchRaysDesc.RayGenerationShaderRecord.SizeInBytes = 0;
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

	UINT m_HitGroupStride;
	CComPtr<ID3D12StateObject> m_pPSO;
	ByteAddressBuffer m_RayGenShaderTable;
	ByteAddressBuffer m_MissShaderTable;
	ByteAddressBuffer m_HitShaderTable;

};

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

void ModelViewer::InitRaytracingStateObjects()
{
	
}


/**
	当前渲染管线 (Forward+ / TiledBasedForward)
	RenderLightShadows => ZPrePass (DepthOnly ->Depth) => FillLightGrid (CullLights) =>
	ModelViewer

*/
