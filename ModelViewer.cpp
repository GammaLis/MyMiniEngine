#include "ModelViewer.h"
#include "Graphics.h"
#include "CommandContext.h"
#include "TextureManager.h"
#include "Model.h"
#include "Effect.h"

// shaders
#include "DepthViewerVS.h"
#include "DepthViewerPS.h"
#include "ModelViewerVS.h"
#include "ModelViewerPS.h"

using namespace MyDirectX;

struct VSConstants
{
	Math::Matrix4 _ModelToProjection;
	XMFLOAT3 _CamPos;
};

struct alignas(16) PSConstants
{
	Math::Vector3 _SunDirection;
	Math::Vector3 _SunLight;
	Math::Vector3 _AmbientLight;

	float _InvTileDim[4];
	uint32_t _TileCount[4];	// x,y有效，后面字节对齐
	uint32_t _FirstLightIndex[4];
};

ModelViewer::ModelViewer(HINSTANCE hInstance, const wchar_t* title, UINT width, UINT height)
	: IGameApp(hInstance, title, width, height)
{
}

void ModelViewer::Update(float deltaTime)
{
	m_ViewProjMatrix = m_Camera.GetViewProjMatrix();

	float cosTheta = cosf(m_CommonStates.SunOrientation);
	float sinTheta = sinf(m_CommonStates.SunOrientation);
	float cosPhi = cosf(m_CommonStates.SunInclination * Math::Pi * 0.5f);
	float sinPhi = sinf(m_CommonStates.SunInclination * Math::Pi * 0.5f);
	m_SunDirection = Math::Normalize(Math::Vector3(cosTheta * cosPhi, sinPhi, sinTheta * cosPhi));

	//
	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto bufferWidth = colorBuffer.GetWidth(), bufferHeight = colorBuffer.GetHeight();
	m_MainViewport.TopLeftX = m_MainViewport.TopLeftY = 0.0f;
	m_MainViewport.Width = (float)bufferWidth;
	m_MainViewport.Height = (float)bufferHeight;
	m_MainViewport.MinDepth = 0.0f;
	m_MainViewport.MaxDepth = 1.0f;

	m_MainScissor.left = 0;
	m_MainScissor.top = 0;
	m_MainScissor.right = (LONG)bufferWidth;
	m_MainScissor.bottom = (LONG)bufferHeight;
}

void ModelViewer::Render()
{
	auto curFrameIndex = m_Gfx->GetCurrentFrameIndex();
	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;

	PSConstants psConstants;
	psConstants._SunDirection = m_SunDirection;
	psConstants._SunLight = Math::Vector3(1.0f) * m_CommonStates.SunLightIntensity;
	psConstants._AmbientLight = Math::Vector3(1.0f) * m_CommonStates.AmbientIntensity;

	const auto& forwardPlusLighting = Effect::s_ForwardPlusLighting;
	psConstants._InvTileDim[0] = 1.0f / forwardPlusLighting.m_LightGridDim;
	psConstants._InvTileDim[1] = 1.0f / forwardPlusLighting.m_LightGridDim;

	psConstants._TileCount[0] = Math::DivideByMultiple(colorBuffer.GetWidth(), forwardPlusLighting.m_LightGridDim);
	psConstants._TileCount[1] = Math::DivideByMultiple(colorBuffer.GetHeight(), forwardPlusLighting.m_LightGridDim);
	psConstants._FirstLightIndex[0] = forwardPlusLighting.m_FirstConeLight;
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

	// z prepass
	{
		// opaque 
		gfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);

		gfxContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
		gfxContext.ClearDepth(depthBuffer);

		gfxContext.SetDepthStencilTarget(depthBuffer.GetDSV());
		gfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);

		gfxContext.SetPipelineState(m_DepthPSO);
		RenderObjects(gfxContext, m_ViewProjMatrix, ObjectFilter::kOpaque);

		// 暂时不用 -20-2-9
		//// cutout
		//gfxContext.SetPipelineState(m_CutoutDepthPSO);
		//RenderObjects(gfxContext, m_ViewProjMatrix, ObjectFilter::kCutout);
	}

	// 
	{
		// CS
		Effect::s_ForwardPlusLighting.FillLightGrid(gfxContext, m_Camera, curFrameIndex);
	}

	// main render
	{
		gfxContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfxContext.ClearColor(colorBuffer);

		pfnSetupGraphicsState();

		// render shadow map

		// render color
		gfxContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
		
		gfxContext.SetRenderTarget(colorBuffer.GetRTV(), depthBuffer.GetDSV_DepthReadOnly());
		gfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);

		gfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);
		// gfxContext.SetDynamicDescriptors(4, 0, _countof(m_ExtraTextures), m_ExtraTextures);
		gfxContext.SetDynamicDescriptors(4, 2, 3, m_ExtraTextures + 2);

		// ->opauqe
		gfxContext.SetPipelineState(m_ModelPSO);
		RenderObjects(gfxContext, m_ViewProjMatrix, ObjectFilter::kOpaque);

		// ->cutout
		// ...
	}
	
	//// 普通渲染顺序，不经z prepass
	//{
	//	gfxContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
	//	gfxContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
	//	gfxContext.ClearColor(colorBuffer);
	//	gfxContext.ClearDepth(depthBuffer);
	//	gfxContext.SetRenderTarget(colorBuffer.GetRTV(), depthBuffer.GetDSV());	// GetDSV_DepthReadOnly
	//	gfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);

	//	gfxContext.SetPipelineState(m_ModelPSO);

	//	gfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);

	//	RenderObjects(gfxContext, m_ViewProjMatrix, ObjectFilter::kOpaque);
	//}

	gfxContext.Finish();
}

void ModelViewer::InitPipelineStates()
{
	SamplerDesc DefaultSamplerDesc;
	DefaultSamplerDesc.MaxAnisotropy = 8;

	// root signature
	m_RootSig.Reset(5, 2);
	m_RootSig[0].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig[1].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[2].InitAsConstants(1, 2, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 6, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[4].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 64, 6, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig.InitStaticSampler(0, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig.InitStaticSampler(1, Graphics::s_CommonStates.SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig.Finalize(Graphics::s_Device, L"ModelViewer", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	DXGI_FORMAT colorFormat = Graphics::s_BufferManager.m_SceneColorBuffer.GetFormat();
	DXGI_FORMAT depthFormat = Graphics::s_BufferManager.m_SceneDepthBuffer.GetFormat();

	// input elements
	D3D12_INPUT_ELEMENT_DESC inputElements[] =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA},
		{"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA},
		{"BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA},
	};

	// PSOs
	// depth pso
	// 只渲染深度，允许深度读写，不需PixelShader，不需颜色绘制
	m_DepthPSO.SetRootSignature(m_RootSig);
	m_DepthPSO.SetInputLayout(_countof(inputElements), inputElements);
	m_DepthPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	m_DepthPSO.SetVertexShader(CD3DX12_SHADER_BYTECODE(DepthViewerVS, sizeof(DepthViewerVS)));
	m_DepthPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerDefault);	// RasterizerDefault RasterizerDefaultWireframe
	m_DepthPSO.SetBlendState(Graphics::s_CommonStates.BlendNoColorWrite);
	m_DepthPSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateReadWrite);
	m_DepthPSO.SetRenderTargetFormats(0, nullptr, depthFormat);
	m_DepthPSO.Finalize(Graphics::s_Device);

	// depth-only shading but with alpha-testing
	m_CutoutDepthPSO = m_DepthPSO;
	m_CutoutDepthPSO.SetPixelShader(CD3DX12_SHADER_BYTECODE(DepthViewerPS, sizeof(DepthViewerPS)));
	m_CutoutDepthPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerTwoSided);
	m_CutoutDepthPSO.Finalize(Graphics::s_Device);

	// model viewer pso
	m_ModelPSO = m_DepthPSO;
	m_ModelPSO.SetVertexShader(CD3DX12_SHADER_BYTECODE(ModelViewerVS, sizeof(ModelViewerVS)));
	m_ModelPSO.SetPixelShader(CD3DX12_SHADER_BYTECODE(ModelViewerPS, sizeof(ModelViewerPS)));
	m_ModelPSO.SetBlendState(Graphics::s_CommonStates.BlendDisable);
	m_ModelPSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateTestEqual);
	m_ModelPSO.SetRenderTargetFormats(1, &colorFormat, depthFormat);
	m_ModelPSO.Finalize(Graphics::s_Device);
}

void ModelViewer::InitGeometryBuffers()
{
	Graphics::s_TextureManager.Init(L"Textures/");
	m_Model->CreateFromAssimp(Graphics::s_Device, "Models/sponza.obj");
	ASSERT(m_Model->m_MeshCount > 0, "Model contains no meshes");
}

void ModelViewer::InitCustom()
{
	// camera
	float modelRadius = Math::Length(m_Model->m_BoundingBox.max - m_Model->m_BoundingBox.min) * 0.5f;
	const Math::Vector3 eye = (m_Model->m_BoundingBox.min + m_Model->m_BoundingBox.max) * 0.5f + Math::Vector3(modelRadius * 0.5f, 0.0f, 0.0f);
	m_Camera.SetEyeAtUp(eye, Math::Vector3(Math::kZero), Math::Vector3(Math::kYUnitVector));
	m_Camera.SetZRange(1.0f, 10000.0f);
	m_Camera.Update();

	// effects
	// ...
	m_ExtraTextures[0] = CD3DX12_CPU_DESCRIPTOR_HANDLE();	// 暂时为空
	m_ExtraTextures[1] = CD3DX12_CPU_DESCRIPTOR_HANDLE();	// 暂时为空

	// forward+ lighting
	const auto &boundingBox = m_Model->GetBoundingBox();
	auto& forwardPlusLighting = Effect::s_ForwardPlusLighting;
	forwardPlusLighting.CreateRandomLights(Graphics::s_Device, boundingBox.min, boundingBox.max);

	m_ExtraTextures[2] = forwardPlusLighting.m_LightBuffer.GetSRV();
	m_ExtraTextures[3] = forwardPlusLighting.m_LightGrid.GetSRV();
	m_ExtraTextures[4] = forwardPlusLighting.m_LightGridBitMask.GetSRV();
	// m_ExtraTextures[5] = ;

}

void ModelViewer::RenderObjects(GraphicsContext& gfxContext, const Math::Matrix4 viewProjMat, ObjectFilter filter)
{
	VSConstants vsConstants;
	vsConstants._ModelToProjection = Math::Transpose(viewProjMat);	// HLSL - 对应 mul(float4(pos), mat)
	// vsConstants._ModelToProjection = (viewProjMat);	// HLSL - 对应 mul(mat, float4(pos))
	XMStoreFloat3(&vsConstants._CamPos, m_Camera.GetPosition());

	gfxContext.SetDynamicConstantBufferView(0, sizeof(vsConstants), &vsConstants);

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
			// ...

			curMatIdx = mesh.materialIndex;
			// 暂时不用纹理
			gfxContext.SetDynamicDescriptors(3, 0, 6, m_Model->GetSRVs(curMatIdx));
		}

		gfxContext.SetConstants(2, vertexOffset, curMatIdx);

		gfxContext.DrawIndexed(indexCount, indexOffset, vertexOffset);
	}

}
