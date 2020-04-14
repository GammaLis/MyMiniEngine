#include "glTFViewer.h"
#include "Graphics.h"
#include "GfxCommon.h"
#include "CommandContext.h"
#include "TextureManager.h"

// compiled shade bytecode
#include "CommonVS.h"
#include "CommonPS.h"

#include "CubemapSH.h"

using namespace MyDirectX;
using namespace DirectX;

// StructuredBuffer - 不须对齐，但是SIMDMemcpy需要对齐，还是地址对齐，对应的，HLSL需要补齐
struct alignas(16) TLight
{
	XMFLOAT3 color = XMFLOAT3(1.0f, 1.0f, 1.0f);		// the color of emitted light, as a linear RGB color
	float intensity = 1.0f;	// the light's brighness. The unit depends on the type of light
	XMFLOAT3 positionOrDirection = XMFLOAT3(1.0f, 1.0f, 1.0f);
	float type = 0;			// 0 - directional lights, 1 - punctual lights
	XMFLOAT3 spotDirection = XMFLOAT3(0.0f, -1.0f, 0.0f);
	float falloffRadius = 50.0f;	// maximum distance of influence
	XMFLOAT2 spotAttenScaleOffset = XMFLOAT2(0.0f, 1.0f);	// Dot(...) * scaleOffset.x + scaleOffset.y
	// or float2 spotAngles;		// x - innerAngle, y - outerAngle
};

struct alignas(16) CBPerObject
{
	glTF::Matrix4x4 _WorldMat;
	glTF::Matrix4x4 _InvWorldMat;
};

struct alignas(16) CBPerCamera
{
	Math::Matrix4 _ViewProjMat;
	Math::Vector3 _CamPos;
};

struct alignas(16) PSConstants
{
	Math::Vector4 _BaseColorFactor;
	DirectX::XMFLOAT3 _EmissiveFactor;
	float _AlphaCutout;
	DirectX::XMUINT4 _Texcoords[2];
#if defined(SHADING_MODEL_METALLIC_ROUGHNESS)
	float _Metallic;
	float _Roughness;
	float _F0;
	float _Padding;
#elif defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
	DirectX::XMFLOAT3 _SpecularColor;
	float _Glossiness;
#endif
	float _NormalScale;
	float _OcclusionStrength;
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
	Graphics::s_TextureManager.Init(L"Textures/");
	ASSERT(m_Importer.Create(Graphics::s_Device));

	// root signature & pso
	{
		// root signature
		m_CommonRS.Reset(7, 2);
		m_CommonRS[0].InitAsConstants(0, 4);
		m_CommonRS[1].InitAsConstantBuffer(1);
		m_CommonRS[2].InitAsConstantBuffer(2);
		m_CommonRS[3].InitAsConstantBuffer(3, 0, D3D12_SHADER_VISIBILITY_PIXEL);
		// m_CommonRS[3].InitAsConstants(3, 8, 0, D3D12_SHADER_VISIBILITY_PIXEL);
		m_CommonRS[4].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 8, 0, D3D12_SHADER_VISIBILITY_PIXEL);
		m_CommonRS[5].InitAsBufferSRV(1, 1);	// light buffer
		m_CommonRS[6].InitAsBufferSRV(2, 1);	// sh buffer
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
		m_ModelViewerPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerDefault);
			// RasterizerDefaultWireframe
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

	// lights
	{
		std::vector<TLight> lights;
		{
			TLight newLight;
			newLight.color = XMFLOAT3(0.2f, 0.4f, 0.7f);
			newLight.intensity = 1.0f;
			newLight.positionOrDirection = XMFLOAT3(-1.0f, -1.0f, 1.0f);
			newLight.type = 0;
			newLight.falloffRadius = 50.0f;
			lights.emplace_back(newLight);
		}
		{
			TLight newLight;
			newLight.color = XMFLOAT3(0.4f, 0.8f, 0.6f);
			newLight.intensity = 2.0f;
			newLight.positionOrDirection = XMFLOAT3(1.0f, -1.0f, 1.0f);
			newLight.type = 0;
			newLight.falloffRadius = 50.0f;
			lights.emplace_back(newLight);
		}

		m_LightBuffer.Create(Graphics::s_Device, L"LightBuffer",
			lights.size(), sizeof(TLight), lights.data());
	}

#pragma region SH
	// SH
	struct SH9Color
	{
		XMFLOAT3 c[9];
	};
	{
		// root signature
		{
			D3D12_SAMPLER_DESC samplerDesc = {};
			samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			samplerDesc.MaxAnisotropy = 16;
			samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
			samplerDesc.MinLOD = 0.0f;
			samplerDesc.MipLODBias = 0.0f;
			samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

			m_SHRS.Reset(3, 1);
			m_SHRS[0].InitAsConstants(0, 4);
			m_SHRS[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
			m_SHRS[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
			m_SHRS.InitStaticSampler(0, samplerDesc);
			m_SHRS.Finalize(Graphics::s_Device, L"SHRS");
		}
		// PSO
		{
			m_SHPSO.SetRootSignature(m_SHRS);
			m_SHPSO.SetComputeShader(CubemapSH, sizeof(CubemapSH));
			m_SHPSO.Finalize(Graphics::s_Device);
		}

		// 		
		const UINT GroupSizeX = 32;
		const UINT GroupSizeY = 32;
		UINT picWidth;
		UINT picHeight;
		{
			// texture
			std::wstring filePath = L"grasscube1024.dds";
			auto pos = filePath.rfind('.');
			if (pos != std::wstring::npos)
				filePath = filePath.substr(0, pos);	// 去除扩展名
			const auto texture = Graphics::s_TextureManager.LoadFromFile(Graphics::s_Device, filePath);
			m_SHsrv = texture->GetSRV();
			auto desc = const_cast<ID3D12Resource*>(texture->GetResource())->GetDesc();
			picWidth = desc.Width;
			picHeight = desc.Height;

			UINT numGroupX = Math::DivideByMultiple(picWidth, GroupSizeX);
			UINT numGroupY = Math::DivideByMultiple(picHeight, GroupSizeY);
			// buffer
			m_SHOutput.Create(Graphics::s_Device, L"SHBuffer", numGroupX * numGroupY, sizeof(SH9Color));
		}

		// precomputing SH coefs
		{
			auto& computeContext = ComputeContext::Begin(L"PreSH");

			computeContext.TransitionResource(m_SHOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			computeContext.SetRootSignature(m_SHRS);
			computeContext.SetPipelineState(m_SHPSO);
			computeContext.SetConstants(0, picWidth, picHeight);
			computeContext.SetDynamicDescriptor(1, 0, m_SHsrv);
			computeContext.SetDynamicDescriptor(2, 0, m_SHOutput.GetUAV());

			computeContext.Dispatch2D(GroupSizeX, GroupSizeY, GroupSizeX, GroupSizeY);

			computeContext.TransitionResource(m_SHOutput, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

			computeContext.Finish(true);
		}
	}
#pragma endregion
}

void glTFViewer::CleanCustom()
{
	m_Importer.Clear();
	
	m_LightBuffer.Destroy();

	// SH
	m_SHOutput.Destroy();
}

void glTFViewer::RenderObjects(GraphicsContext& gfx, const Math::Matrix4 viewProjMat, ObjectFilter filter)
{
	// camera
	CBPerCamera cbPerCamera;
	cbPerCamera._ViewProjMat = Math::Transpose(viewProjMat);
	cbPerCamera._CamPos = m_Camera.GetPosition();
	gfx.SetDynamicConstantBufferView(2, sizeof(CBPerCamera), &cbPerCamera);
	// constants
	gfx.SetConstants(0, 2, 0, 0, 0);	// root0
	// lights
	gfx.SetBufferSRV(5, m_LightBuffer);
	// sh
	gfx.SetBufferSRV(6, m_SHOutput);
	
	gfx.SetPipelineState(m_ModelViewerPSO);

	CBPerObject cbPerObject;
	PSConstants psConstants;

	const auto& rMeshes = m_Importer.m_oMeshes;
	for (size_t i = 0, imax = rMeshes.size(); i < imax; ++i)
	{
		const auto& curMesh = rMeshes[i];

		int matIdx = curMesh.materialIndex;
		if (m_Importer.IsValidMaterial(matIdx))
		{
			int activeMatIdx = m_Importer.m_ActiveMaterials[matIdx];
			const auto& curMat = m_Importer.m_oMaterials[activeMatIdx];

			// CBPerObject
			glTF::Matrix4x4 trans(std::move(m_Importer.GetMeshTransform(curMesh)));
			cbPerObject._WorldMat = glm::transpose(trans);
			cbPerObject._InvWorldMat = glm::transpose(glm::inverse(trans));
			gfx.SetDynamicConstantBufferView(1, sizeof(CBPerObject), &cbPerObject);

			gfx.SetConstant(0, curMesh.enabledAttribs, 3);	// root0, 3 - enabledAttribs

			// PSConstants
			const auto& baseColorFactor = curMat.baseColorFactor;
			psConstants._BaseColorFactor = Math::Vector4(baseColorFactor[0], baseColorFactor[1], baseColorFactor[2], baseColorFactor[3]);
			const auto& emissiveFactor = curMat.emissiveFactor;
			psConstants._EmissiveFactor = DirectX::XMFLOAT3(emissiveFactor[0], emissiveFactor[1], emissiveFactor[2]);
			psConstants._AlphaCutout = curMat.alphaCutoff;
			memcpy_s(psConstants._Texcoords, sizeof(psConstants._Texcoords), curMat.texcoords, sizeof(curMat.texcoords));

			psConstants._NormalScale = curMat.normalScale;
			psConstants._OcclusionStrength = curMat.occlusionStrength;
#if defined(SHADING_MODEL_METALLIC_ROUGHNESS)
			psConstants._Metallic = curMat.metallic;
			psConstants._Roughness = curMat.roughness;
			psConstants._F0 = 0.04f;
#elif defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
			DirectX::XMFLOAT3 _SpecularColor;
			float _Glossiness;
#endif
			gfx.SetDynamicConstantBufferView(3, sizeof(PSConstants), &psConstants);

			// textures
			gfx.SetDynamicDescriptors(4, 0, glTF::Material::TextureNum, m_Importer.GetSRVs(activeMatIdx));

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
}
