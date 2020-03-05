#include "glTFViewer.h"
#include "Graphics.h"
#include "GfxCommon.h"
#include "CommandContext.h"

// compiled shade bytecode
#include "CommonVS.h"
#include "CommonPS.h"

using namespace MyDirectX;

struct alignas(16) CBPerObject
{
	glTF::Matrix4x4 _WorldMat;
	glTF::Matrix4x4 _InvWorldMat;
};

struct alignas(16) CBPerCamera
{
	Math::Matrix4 _ViewProjMat;
};

glTFViewer::glTFViewer(HINSTANCE hInstance, const std::string& glTFFileName, const wchar_t* title, UINT width, UINT height)
	: IGameApp(hInstance, title, width, height)
{
	m_Importer.Load(glTFFileName);
}

void glTFViewer::Update(float deltaTime)
{
	IGameApp::Update(deltaTime);

	m_CameraController->Update(deltaTime);
	m_ViewProjMatrix = m_Camera.GetViewProjMatrix();
}

void glTFViewer::Render()
{
	GraphicsContext& gfx = GraphicsContext::Begin(L"Scene Render");

	gfx.SetRootSignature(m_CommonRS);
	gfx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	gfx.SetVertexBuffer(0, m_Importer.m_VertexBuffer.VertexBufferView());
	gfx.SetIndexBuffer(m_Importer.m_IndexBuffer.IndexBufferView());

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;

	gfx.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
	gfx.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
	gfx.ClearColor(colorBuffer);
	gfx.ClearDepth(depthBuffer);
	gfx.SetRenderTarget(colorBuffer.GetRTV(), depthBuffer.GetDSV());
	gfx.SetViewportAndScissor(m_MainViewport, m_MainScissor);
	
	RenderObjects(gfx, m_ViewProjMatrix);	

	gfx.Finish();
}

void glTFViewer::InitAssets()
{
	using glTF::Attrib;

	// 创建模型
	ASSERT(m_Importer.Create(Graphics::s_Device));

	// root signature & pso
	{
		// root signature
		m_CommonRS.Reset(5, 2);
		m_CommonRS[0].InitAsConstants(0, 4);
		m_CommonRS[1].InitAsConstantBuffer(1);
		m_CommonRS[2].InitAsConstantBuffer(2);
		// m_CommonRS[3].InitAsConstantBuffer(3, D3D12_SHADER_VISIBILITY_PIXEL);
		m_CommonRS[3].InitAsConstants(3, 8, D3D12_SHADER_VISIBILITY_PIXEL);
		m_CommonRS[4].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 2, D3D12_SHADER_VISIBILITY_PIXEL);
		m_CommonRS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearWrapDesc);
		m_CommonRS.InitStaticSampler(1, Graphics::s_CommonStates.SamplerPointClampDesc);
		m_CommonRS.Finalize(Graphics::s_Device, L"CommonRS", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		// input elements
		const auto& vAttribs = m_Importer.m_VertexAttributes;
		D3D12_INPUT_ELEMENT_DESC inputElements[] =
		{
			{"POSITION", 0, vAttribs[Attrib::attrib_position].format, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA },
			{"TEXCOORD", 0, vAttribs[Attrib::attrib_texcoord0].format, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA },
			{"TEXCOORD", 1, vAttribs[Attrib::attrib_texcoord1].format, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA },
			{"NORMAL", 0, vAttribs[Attrib::attrib_normal].format, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA },
			{"TANGENT", 0, vAttribs[Attrib::attrib_tangent].format, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA },
			{"COLOR", 0, vAttribs[Attrib::attrib_color0].format, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA }
		};

		const auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
		const auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;

		m_ModelViewerPSO.SetRootSignature(m_CommonRS);
		m_ModelViewerPSO.SetInputLayout(_countof(inputElements), inputElements);
		m_ModelViewerPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		m_ModelViewerPSO.SetVertexShader(CommonVS, sizeof(CommonVS));
		m_ModelViewerPSO.SetPixelShader(CommonPS, sizeof(CommonPS));
		m_ModelViewerPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerDefaultWireframe);
		m_ModelViewerPSO.SetBlendState(Graphics::s_CommonStates.BlendDisable);
		m_ModelViewerPSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateReadWrite);
		m_ModelViewerPSO.SetSampleMask(0xFFFFFFFF);
		m_ModelViewerPSO.SetRenderTargetFormats(1, &colorBuffer.GetFormat(), depthBuffer.GetFormat());
		m_ModelViewerPSO.Finalize(Graphics::s_Device);

		// viewport & scissor
		uint32_t bufferWidth = colorBuffer.GetWidth();
		uint32_t bufferHeight = colorBuffer.GetHeight();
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

	// camera
	{
		glTF::BoundingBox boundingBox = m_Importer.GetBoundingBox();
		glTF::Vector3 center = (boundingBox.max + boundingBox.min) / 2.0f;
		glTF::Vector3 extent = (boundingBox.max - boundingBox.min);
		Math::Vector3 eye(center.x, center.y, center.z + extent.z);
		m_Camera.SetEyeAtUp(eye, Math::Vector3(Math::kZero), Math::Vector3(Math::kYUnitVector));
		m_Camera.SetZRange(1.0f, 1000.0f);
		// m_Camera.Update();	// 若无CameraController，需要手动更新
		m_CameraController.reset(new CameraController(m_Camera, Math::Vector3(Math::kYUnitVector), *m_Input));
	}
}

void glTFViewer::CleanCustom()
{
	m_Importer.Clear();
}

void glTFViewer::RenderObjects(GraphicsContext& gfx, const Math::Matrix4 viewProjMat, ObjectFilter filter)
{
	CBPerCamera cbPerCamera;
	cbPerCamera._ViewProjMat = Math::Transpose(viewProjMat);
	gfx.SetDynamicConstantBufferView(2, sizeof(CBPerCamera), &cbPerCamera);
	
	gfx.SetPipelineState(m_ModelViewerPSO);

	CBPerObject cbPerObject;

	const auto& rMeshes = m_Importer.m_oMeshes;
	for (int i = 0, imax = rMeshes.size(); i < imax; ++i)
	{
		const auto& curMesh = rMeshes[i];
		
		glTF::Matrix4x4 trans(std::move(m_Importer.GetMeshTransform(curMesh)));
		cbPerObject._WorldMat = glm::transpose(trans);
		cbPerObject._InvWorldMat = glm::transpose(glm::inverse(trans));
		gfx.SetDynamicConstantBufferView(1, sizeof(CBPerObject), &cbPerObject);

		Math::Vector3 camPos = m_Camera.GetPosition();
		gfx.SetConstants(0, (float)camPos.GetX(), (float)camPos.GetY(), (float)camPos.GetZ(), curMesh.enabledAttribs);

		if (curMesh.indexAccessor >= 0)
		{
			gfx.DrawIndexed(curMesh.indexCount, curMesh.indexDataByteOffset / sizeof(uint16_t), curMesh.vertexDataByteOffset / curMesh.vertexStride);
		}
		else
		{
			gfx.Draw(curMesh.vertexCount, curMesh.vertexDataByteOffset / curMesh.vertexStride);
		}
	}
}
