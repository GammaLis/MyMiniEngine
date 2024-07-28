#include "Skybox.h"
#include "Graphics.h"
#include "MyBasicGeometry.h"
#include "Camera.h"
#include "TextureManager.h"
#include "CommandContext.h"
#include "ProfilingScope.h"

// compiled shader bytecode
#include "SkyboxVS.h"
#include "SkyboxPS.h"

using namespace MyDirectX;

struct MyVertex
{
	XMFLOAT3 position;
};

struct CBPerCamera
{
	XMMATRIX _ViewMat;
	XMFLOAT3 _CamPos;
};

struct CBProjection
{
	XMMATRIX _ProjMat;
};

void Skybox::Init(ID3D12Device* pDevice, const std::wstring& fileName)
{
	// root signatures
	{
		m_SkyboxRS.Reset(3, 1);
		m_SkyboxRS[0].InitAsConstantBuffer(0);
		m_SkyboxRS[1].InitAsConstantBuffer(1);
		m_SkyboxRS[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
		m_SkyboxRS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearWrapDesc);
		m_SkyboxRS.Finalize(pDevice, L"SkyboxRS", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	}

	// PSOs
	{
		m_SkyboxPSO.SetRootSignature(m_SkyboxRS);
		m_SkyboxPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		D3D12_INPUT_ELEMENT_DESC inputElements[] =
		{
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA}
		};
		m_SkyboxPSO.SetInputLayout(_countof(inputElements), inputElements);
		m_SkyboxPSO.SetVertexShader(SkyboxVS, sizeof(SkyboxVS));
		m_SkyboxPSO.SetPixelShader(SkyboxPS, sizeof(SkyboxPS));
		m_SkyboxPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerDefault);
		m_SkyboxPSO.SetBlendState(Graphics::s_CommonStates.BlendDisable);
		m_SkyboxPSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateReadWrite);

		auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
		auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
		m_SkyboxPSO.SetRenderTargetFormat(colorBuffer.GetFormat(), depthBuffer.GetFormat());
		m_SkyboxPSO.Finalize(pDevice);
	}

	// geometries
	{
		using namespace Geometry;

		MyVertex vertices[] = 
		{
			{XMFLOAT3(-1.0f,  1.0f, -1.0f)},
			{XMFLOAT3(-1.0f, -1.0f, -1.0f)},
			{XMFLOAT3( 1.0f, -1.0f, -1.0f)},
			{XMFLOAT3( 1.0f,  1.0f, -1.0f)},

			{XMFLOAT3(-1.0f,  1.0f, 1.0f)},
			{XMFLOAT3(-1.0f, -1.0f, 1.0f)},
			{XMFLOAT3( 1.0f, -1.0f, 1.0f)},
			{XMFLOAT3( 1.0f,  1.0f, 1.0f)},
		};
		m_VertexCount = _countof(vertices);
		m_VertexOffset = 0;
		m_VertexStride = sizeof(MyVertex);

		uint16_t indices[] =
		{
			0, 1, 2,
			0, 2, 3,

			3, 2, 6,
			3, 6, 7,

			7, 6, 5,
			7, 5, 4,

			4, 5, 1, 
			4, 1, 0,

			4, 0, 3,
			4, 3, 7,

			1, 5, 6,
			1, 6, 2
		};
		m_IndexCount = _countof(indices);
		m_IndexOffset = 0;

#if 0
		Geometry::Mesh box;
		MyBasicGeometry::BasicBox(2, 2, 2, box);

		// vertices
		m_VertexCount = box.vertices.size();
		m_VertexOffset = 0;
		m_VertexStride = sizeof(MyVertex);
		std::vector<MyVertex> vertices(m_VertexCount);
		for (size_t i = 0; i < m_VertexCount; ++i)
		{
			vertices[i].position = box.vertices[i].position;
		}
#endif
		m_SkyVB.Create(pDevice, L"SkyboxVB", m_VertexCount, m_VertexStride, vertices);

		// indices
#if 0
		m_IndexCount = box.indices.size();
		m_IndexOffset = 0;
		std::vector<uint16_t> indices(m_IndexCount);
		for (size_t i = 0; i < m_IndexCount; ++i)
		{
			indices[i] = static_cast<uint16_t>(box.indices[i]);
		}
#endif
		m_SkyIB.Create(pDevice, L"SkyboxIB", m_IndexCount, sizeof(uint16_t), indices);
	}

	// textures
	{
		const auto &texture = Graphics::s_TextureManager.LoadFromFile(pDevice, fileName);
		srv = texture->GetSRV();
	}
}

void Skybox::Render(GraphicsContext& gfx, const Math::Camera& camera)
{
	ProfilingScope profilingScope(L"Render Skybox", gfx);

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;

	gfx.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
	gfx.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);

	gfx.SetRenderTarget(colorBuffer.GetRTV(), depthBuffer.GetDSV());
	gfx.SetViewportAndScissor(0, 0, colorBuffer.GetWidth(), colorBuffer.GetHeight());
	
	gfx.SetRootSignature(m_SkyboxRS);
	gfx.SetPipelineState(m_SkyboxPSO);
	gfx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	gfx.SetVertexBuffer(0, m_SkyVB.VertexBufferView());
	gfx.SetIndexBuffer(m_SkyIB.IndexBufferView());
	
	CBPerCamera cbPerCamera;
	cbPerCamera._ViewMat = XMMatrixTranspose(camera.GetViewMatrix());
	const auto& camPos = camera.GetPosition();
	cbPerCamera._CamPos = XMFLOAT3(camPos.GetX(), camPos.GetY(), camPos.GetZ());
	gfx.SetDynamicConstantBufferView(0, sizeof(cbPerCamera), &cbPerCamera);

	CBProjection cbProjection;
	cbProjection._ProjMat = XMMatrixTranspose(camera.GetProjMatrix());
	gfx.SetDynamicConstantBufferView(1, sizeof(cbProjection), &cbProjection);

	gfx.SetDynamicDescriptor(2, 0, srv);

	gfx.DrawIndexed(m_IndexCount, m_IndexOffset, static_cast<INT>(m_VertexOffset));
}

void Skybox::Shutdown()
{
	m_SkyVB.Destroy();
	m_SkyIB.Destroy();
}
