#include "glTFViewer.h"
#include "Graphics.h"
#include "GfxCommon.h"
#include "CommandContext.h"
#include "TextureManager.h"
#include "Camera.h"
#include "Common/CameraController.h"
#include "Utilities/GameUtility.h"

#include "glTFImporter.h"
#include "Libraries/cgltf/cgltf.h"

// compiled shade bytecode
#include "glTFCommonVS.h"
#include "glTFCommonPS.h"

#include "CubemapSH.h"
#include "Graphics.h"
#include "GfxCommon.h"
#include "CommandContext.h"
#include "CommandListManager.h"
#include "Scenes/AssimpImporter.h"

using namespace MyDirectX;
using namespace DirectX;

namespace
{
	// SIMDMemcpy needs 16-byte aligned, but not in StructuredBuffer, so we need padding
	struct alignas(16) FLight
	{
		XMFLOAT3 color = XMFLOAT3(1.0f, 1.0f, 1.0f);		// the color of emitted light, as a linear RGB color
		float intensity = 1.0f;	// the light's brightness. The unit depends on the type of light
		XMFLOAT3 positionOrDirection = XMFLOAT3(1.0f, 1.0f, 1.0f);
		float type = 0;			// 0 - directional lights, 1 - punctual lights
		XMFLOAT3 spotDirection = XMFLOAT3(0.0f, -1.0f, 0.0f);
		float falloffRadius = 50.0f;	// maximum distance of influence
		XMFLOAT2 spotAttenScaleOffset = XMFLOAT2(0.0f, 1.0f);	// Dot(...) * scaleOffset.x + scaleOffset.y
		// or float2 spotAngles;		// x - innerAngle, y - outerAngle
	};

	struct alignas(16) CBPerObject
	{
		glTF::Matrix4x4 worldMat;
		glTF::Matrix4x4 invWorldMat;
	};

	struct alignas(16) CBPerCamera
	{
		Math::Matrix4 viewProjMat;
		Math::Vector3 camPos;
	};

	struct alignas(16) PSConstants
	{
		Math::Vector4 baseColorFactor { 1.0f, 1.0f, 1.0f, 1.0f };
		DirectX::XMFLOAT3 emissiveFactor { 1.0f, 1.0f, 1.0f };
		float alphaCutout { 0.5f };
		DirectX::XMUINT4 texcoords[2];
#if defined(SHADING_MODEL_METALLIC_ROUGHNESS)
		float metallic { 0.5f };
		float roughness { 0.5f };
		float f0 { 0.04f };
		float padding;
#elif defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
		DirectX::XMFLOAT3 _SpecularColor;
		float _Glossiness;
#endif
		float normalScale { 1.0f };
		float occlusionStrength { 1.0f };
	};
}

glTFViewer::glTFViewer(HINSTANCE hInstance, const std::string& glTFFileName, const wchar_t* title, UINT width, UINT height)
	: IGameApp(hInstance, title, width, height)
	, m_Importer(std::make_unique<glTF::glTFImporter>())
{
	m_FileNames.emplace_back(glTFFileName);
}

void glTFViewer::Update(float deltaTime)
{
	static std::vector<std::string> s_ReadyImporters;
	
	IGameApp::Update(deltaTime);

	// Update mesh buffers
	if (m_UpdateVertexBuffers.size())
	{
		auto updateInfo = m_UpdateVertexBuffers.front();
		if (updateInfo.fenceValue == 0 || Graphics::s_CommandManager.IsFenceComplete(updateInfo.fenceValue))
		{
			ASSERT(updateInfo.index < static_cast<uint32_t>(m_VertexBuffers.size()));
			m_VertexBuffers[updateInfo.index] = std::static_pointer_cast<StructuredBuffer>(updateInfo.buffer); 
			m_UpdateVertexBuffers.pop_front();
		}
	}
	if (m_UpdateIndexBuffers.size())
	{
		auto updateInfo = m_UpdateIndexBuffers.front();
		if (updateInfo.fenceValue == 0 ||  Graphics::s_CommandManager.IsFenceComplete(updateInfo.fenceValue))
		{
			ASSERT(updateInfo.index < static_cast<uint32_t>(m_IndexBuffers.size()));
			m_IndexBuffers[updateInfo.index] = std::static_pointer_cast<ByteAddressBuffer>(updateInfo.buffer); 
			m_UpdateIndexBuffers.pop_front();
		}		
	}

	// Update importers
	if (m_Importer->UpdateImporters(s_ReadyImporters))
	{
		for (const auto &fileName : s_ReadyImporters)
		{
			Utility::Printf("File loaded %s", fileName.c_str());

			// Update camera
			auto &sceneCamera = m_Importer->GetOptionalCamera();
			if (sceneCamera.has_value() && m_CameraName.empty())
			{
				ResetCamera(sceneCamera.value());
				m_CameraName = fileName;
				sceneCamera = std::nullopt;
			}
			
			if (auto&& drawObject = m_Importer->MoveDrawObject(fileName))
			{
				auto it = m_NameAndObjIndexMap.find(fileName);
				ASSERT(it != m_NameAndObjIndexMap.end());
				m_DrawObjects[it->second] = std::move(drawObject);

				UpdateMeshBuffers(std::pair{ fileName, &(m_DrawObjects[it->second]->mesh) }); 
			}
		}
		s_ReadyImporters.clear();
	}
	
	m_CameraController->Update(deltaTime);
	m_ViewProjMatrix = m_Camera->GetViewProjMatrix();
}

void glTFViewer::Render()
{
	GraphicsContext& gfx = GraphicsContext::Begin(L"Scene Render");

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
	
	// Set Render targets
	{
		gfx.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
		gfx.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		
		gfx.ClearColor(colorBuffer);
		gfx.ClearDepth(depthBuffer);
		gfx.SetRenderTarget(colorBuffer.GetRTV(), depthBuffer.GetDSV());
		gfx.SetViewportAndScissor(m_MainViewport, m_MainScissor);
	}

	// Draw meshes
	{
		gfx.SetRootSignature(m_CommonRS);
		gfx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		gfx.SetVertexBuffer(0, m_GlobalVertexBuffer.VertexBufferView());
		gfx.SetIndexBuffer(m_GlobalIndexBuffer.IndexBufferView());

		RenderObjects(gfx, m_ViewProjMatrix);
	}

	gfx.Finish();
}

bool glTFViewer::LoadFile(const std::string& fileName)
{
	auto it = m_NameAndObjIndexMap.find(fileName);
	if (it != m_NameAndObjIndexMap.end())
	{
		Utility::Printf("File %s already added", fileName.c_str());
		return true;
	}

	uint32_t index = static_cast<uint32_t>(m_DrawObjects.size());
	m_DrawObjects.emplace_back(nullptr);
	m_VertexBuffers.emplace_back(nullptr);
	m_IndexBuffers.emplace_back(nullptr);
	m_NameAndObjIndexMap.insert({ fileName, index });
	m_Importer->LoadAsync(fileName);
	
	return true;
}

void glTFViewer::AddDrawObject(const std::string &name, const std::shared_ptr<glTF::DrawObject>& drawObject)
{
	auto it = m_NameAndObjIndexMap.find(name);
	if (it != m_NameAndObjIndexMap.end())
	{
		Utility::Printf("DrawObject %s already added", name.c_str());
		return;
	}

	uint32_t index = static_cast<uint32_t>(m_DrawObjects.size());
	m_DrawObjects.emplace_back(std::move(drawObject));
	m_VertexBuffers.emplace_back(nullptr);
	m_IndexBuffers.emplace_back(nullptr);
	m_NameAndObjIndexMap.insert({ name, index });
	UpdateMeshBuffers({ name, &drawObject->mesh });
}

uint64_t UpdateBuffersAsync(const std::vector<std::tuple<GpuBuffer*, const uint8_t*, uint32_t>> &buffersToUpdate)
{
	CommandContext& updateContext = CommandContext::Begin(L"UpdateBufferAsync");

	// Find max bytes needed
	uint32_t maxBytes = 0;
	for (const auto &buffer : buffersToUpdate )
	{
		maxBytes = std::max(maxBytes, std::get<2>(buffer));
	}
	ASSERT(maxBytes > 0);

	// TODO: optimize
	for (const auto &bufferData : buffersToUpdate)
	{
		auto &buffer = *std::get<0>(bufferData);
		auto initialData = std::get<1>(bufferData);
		uint32_t numBytes = std::get<2>(bufferData);
	
		DynAlloc mem = updateContext.ReserveUploadMemory(numBytes);
		memcpy(mem.dataPtr, initialData, numBytes);

		// Copy data to the intermediate upload heap and then schedule a copy from the upload heap to the default buffer
		updateContext.TransitionResource(buffer, D3D12_RESOURCE_STATE_COPY_DEST, true);
		updateContext.GetCommandList()->CopyBufferRegion(buffer.GetResource(), 0, mem.buffer.GetResource(), mem.offset, numBytes);
		updateContext.TransitionResource(buffer, D3D12_RESOURCE_STATE_GENERIC_READ, true);	
	}

	// Execute the command list and wait for it to finish so we can release the upload buffer
	uint64_t fenceValue = updateContext.Finish();
	return fenceValue;
}

// [Tag] | [index (4 bits)]
uint32_t EncodeBufferIndex(uint32_t index, uint32_t tag)
{
	return (tag << 4) | index;
}

std::pair<uint32_t, uint32_t> DecodeBufferIndex(uint32_t value)
{
	return { value & 0x0F, value >> 4 };
}

void glTFViewer::UpdateMeshBuffers(const std::pair<std::string, glTF::MeshBatch*> &meshData)
{
	auto fileName = meshData.first;
	const auto &srcMesh = meshData.second;
	
	ASSERT(m_UpdateVertexBuffers.size() == m_UpdateIndexBuffers.size());
		
	auto meshName = Utility::RemoveBasePath(fileName);

	std::vector<std::tuple<GpuBuffer*, const uint8_t*, uint32_t>> buffersToUpdate;
	
	// New vertex buffer
	auto pVertexBuffer = std::make_shared<StructuredBuffer>();
	{
		auto numElements = static_cast<uint32_t>(srcMesh->vertices.size());
		auto elementSize = sizeof(srcMesh->vertices[0]);
		pVertexBuffer->Create(Graphics::s_Device, Utility::UTF8ToWideString(meshName+"_VB"), numElements, elementSize);
		buffersToUpdate.emplace_back(std::tuple{pVertexBuffer.get(), reinterpret_cast<const uint8_t*>(srcMesh->vertices.data()), numElements * elementSize});
	}
	// New index buffer
	auto pIndexBuffer = std::make_shared<ByteAddressBuffer>();
	{
		auto numElements = static_cast<uint32_t>(srcMesh->indices.size());
		uint32_t elementSize = sizeof(srcMesh->indices[0]);
		pIndexBuffer->Create(Graphics::s_Device, Utility::UTF8ToWideString(meshName+"_IB"), numElements, elementSize /*, srcMesh->indices.data() */ );
		buffersToUpdate.emplace_back(std::tuple{pIndexBuffer.get(), reinterpret_cast<const uint8_t*>(srcMesh->indices.data()), numElements * elementSize});
	}

	// Update fence value
	auto it = m_NameAndObjIndexMap.find(fileName);
	ASSERT(it != m_NameAndObjIndexMap.end());
	
	auto fenceValue = UpdateBuffersAsync(buffersToUpdate);
	m_UpdateVertexBuffers.push_back( { fenceValue, std::move(pVertexBuffer), it->second } );
	m_UpdateIndexBuffers.push_back( { fenceValue, std::move(pIndexBuffer), it->second } );
}

static std::vector<D3D12_INPUT_ELEMENT_DESC> s_InputElements =
{
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA },
	{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA },
	{"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA },
	{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA },
	{"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA },
	{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA }
};

bool glTFViewer::InitAssets()
{
	using glTF::Attrib;

	bool bAnyLoaded = false;
	for (const auto &fileName : m_FileNames)
	{
#if 0
		bAnyLoaded |= m_Importer->Load(fileName);
#else
		bAnyLoaded |= LoadFile(fileName); // m_Importer->LoadAsync(fileName);
#endif
	}
	if (!bAnyLoaded)
	{
		Utility::Printf("Load file failed! Cannot init viewers!\n");
		return false;
	}

	// Init model
	// ASSERT(m_Importer->Create(Graphics::s_Device));
	Graphics::s_TextureManager.Init(L"Textures/");

	// root signature & pso
	{
		// root signature
		m_CommonRS.Reset(7, 2);
		m_CommonRS[ERSId::Constants].InitAsConstants(0, 4);
		m_CommonRS[ERSId::PerObject].InitAsConstantBuffer(1);
		m_CommonRS[ERSId::PerCamera].InitAsConstantBuffer(2);
		m_CommonRS[ERSId::PerMaterial].InitAsConstantBuffer(3, 0, D3D12_SHADER_VISIBILITY_PIXEL);
		// m_CommonRS[3].InitAsConstants(3, 8, 0, D3D12_SHADER_VISIBILITY_PIXEL);
		m_CommonRS[ERSId::Textures].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 8, 0, D3D12_SHADER_VISIBILITY_PIXEL);
		m_CommonRS[ERSId::LightBuffer].InitAsBufferSRV(1, 1);	// light buffer
		m_CommonRS[ERSId::GIBuffer].InitAsBufferSRV(2, 1);	// sh buffer
		m_CommonRS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearWrapDesc);
		m_CommonRS.InitStaticSampler(1, Graphics::s_CommonStates.SamplerPointClampDesc);
		m_CommonRS.Finalize(Graphics::s_Device, L"CommonRS", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		// input elements
#if 0
		const auto& vAttribs = m_Importer->m_VertexAttributes;
		std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements=
		{
			{"POSITION", 0, vAttribs[Attrib::attrib_position].format, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA },
			{"TEXCOORD", 0, vAttribs[Attrib::attrib_texcoord0].format, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA },
			{"TEXCOORD", 1, vAttribs[Attrib::attrib_texcoord1].format, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA },
			{"NORMAL", 0, vAttribs[Attrib::attrib_normal].format, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA },
			{"TANGENT", 0, vAttribs[Attrib::attrib_tangent].format, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA },
			{"COLOR", 0, vAttribs[Attrib::attrib_color0].format, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA }
		};
#elseif 0
		const auto& inputElements = s_InputElements;
#else
		const auto &inputElements = glTF::kInputElements;
#endif

		const auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
		const auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;

		m_ModelViewerPSO.SetRootSignature(m_CommonRS);
		m_ModelViewerPSO.SetInputLayout(static_cast<uint32_t>(inputElements.size()), inputElements.data());
		m_ModelViewerPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		m_ModelViewerPSO.SetVertexShader(glTFCommonVS, sizeof(glTFCommonVS));
		m_ModelViewerPSO.SetPixelShader(glTFCommonPS, sizeof(glTFCommonPS));
		m_ModelViewerPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerDefault); // RasterizerDefaultCw
			// RasterizerDefaultWireframe
		m_ModelViewerPSO.SetBlendState(Graphics::s_CommonStates.BlendDisable);
		m_ModelViewerPSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateReadWrite);
		m_ModelViewerPSO.SetSampleMask(0xFFFFFFFF);
		m_ModelViewerPSO.SetRenderTargetFormats(1, &colorBuffer.GetFormat(), depthBuffer.GetFormat());
		m_ModelViewerPSO.Finalize(Graphics::s_Device);

		uint32_t bufferWidth = colorBuffer.GetWidth();
		uint32_t bufferHeight = colorBuffer.GetHeight();
		// viewport & scissor
#if 0
		m_MainViewport.TopLeftX = m_MainViewport.TopLeftY = 0.0f;
		m_MainViewport.Width = (float)bufferWidth;
		m_MainViewport.Height = (float)bufferHeight;
		m_MainViewport.MinDepth = 0.0f;
		m_MainViewport.MaxDepth = 1.0f;

		m_MainScissor.left = 0;
		m_MainScissor.top = 0;
		m_MainScissor.right = (LONG)bufferWidth;
		m_MainScissor.bottom = (LONG)bufferHeight;
#else
		UpdateViewportAndScissor(m_MainViewport, m_MainScissor, 0, 0, static_cast<float>(bufferWidth), static_cast<float>(bufferHeight));
#endif
	}

	// camera
	{
		glTF::BoundingBox boundingBox = m_SceneBoundingBox; // m_Importer->GetBoundingBox();
		glTF::Vector3 center = (boundingBox.max + boundingBox.min) / 2.0f;
		glTF::Vector3 extent = (boundingBox.max - boundingBox.min);
		Math::Vector3 eye(center.x, center.y, center.z + extent.z);
		m_Camera.reset(new Math::Camera());
		m_Camera->SetEyeAtUp(eye, Math::Vector3(Math::kZero), Math::Vector3(Math::kYUnitVector));
		m_Camera->SetZRange(1.0f, 1000.0f);
		// m_Camera.Update();	// if no CameraController, need manual update
		m_CameraController.reset(new CameraController(*m_Camera, Math::Vector3(Math::kYUnitVector), *m_Input));
		m_CameraController->SetMoveSpeed(200.0f);
		m_CameraController->SetStrafeSpeed(200.0f);
	}

	// lights
	{
		std::vector<FLight> lights;
		{
			FLight newLight;
			newLight.color = XMFLOAT3(0.2f, 0.4f, 0.7f);
			newLight.intensity = 1.0f;
			newLight.positionOrDirection = XMFLOAT3(-1.0f, -1.0f, 1.0f);
			newLight.type = 0;
			newLight.falloffRadius = 50.0f;
			lights.emplace_back(newLight);
		}
		{
			FLight newLight;
			newLight.color = XMFLOAT3(0.4f, 0.8f, 0.6f);
			newLight.intensity = 2.0f;
			newLight.positionOrDirection = XMFLOAT3(1.0f, -1.0f, 1.0f);
			newLight.type = 0;
			newLight.falloffRadius = 50.0f;
			lights.emplace_back(newLight);
		}

		m_LightBuffer.Create(Graphics::s_Device, L"LightBuffer",
			static_cast<uint32_t>(lights.size()), sizeof(FLight), lights.data());
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
		constexpr UINT GroupSizeX = 32;
		constexpr UINT GroupSizeY = 32;
		UINT picWidth;
		UINT picHeight;
		{
			// texture
			std::wstring filePath = L"grasscube1024.dds";
			auto pos = filePath.rfind('.');
			if (pos != std::wstring::npos)
				filePath = filePath.substr(0, pos);	// ȥ����չ��
			const auto texture = Graphics::s_TextureManager.LoadFromFile(Graphics::s_Device, filePath);
			m_SHsrv = texture->GetSRV();
			auto desc = const_cast<ID3D12Resource*>(texture->GetResource())->GetDesc();
			picWidth = (UINT)desc.Width;
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

	return true;
}

bool glTFViewer::InitCustom()
{
	return true;
}

void glTFViewer::CleanCustom()
{
	// No need to wait for fence
#if 0
	if (!m_UpdateVertexBuffers.empty())
	{
		const auto &updateInfo = m_UpdateVertexBuffers.back();
		Graphics::s_CommandManager.WaitForFence(updateInfo.fenceValue);
	}
#endif
	
	if (m_Importer)
	{
		m_Importer->Clear();
	}
	
	m_LightBuffer.Destroy();

	// Mesh buffers
	ASSERT(m_VertexBuffers.size() == m_IndexBuffers.size());
	for (uint32_t i = 0, imax = static_cast<uint32_t>(m_IndexBuffers.size()); i < imax; i++)
	{
		if (m_VertexBuffers[i])
		{
			m_VertexBuffers[i]->Destroy();
			m_VertexBuffers[i] = nullptr; 
		}
		if (m_IndexBuffers[i])
		{
			m_IndexBuffers[i]->Destroy();
			m_IndexBuffers[i] = nullptr; 
		}
	}

	ASSERT(m_UpdateVertexBuffers.size() == m_UpdateIndexBuffers.size());
	for (auto &updateInfo : m_UpdateVertexBuffers)
	{
		if (auto &buffer = updateInfo.buffer)
		{
			buffer->Destroy();
			buffer = nullptr;
		}
	}
	for (auto &updateInfo : m_UpdateIndexBuffers)
	{
		if (auto &buffer = updateInfo.buffer)
		{
			buffer->Destroy();
			buffer = nullptr;
		}
	}
	
	m_GlobalVertexBuffer.Destroy();
	m_GlobalIndexBuffer.Destroy();

	// SH
	m_SHOutput.Destroy();
}

void glTFViewer::RenderObjects(GraphicsContext& gfx, const Math::Matrix4 &viewProjMat, ObjectFilter filter)
{
	const auto &drawObjects = m_DrawObjects;
	if (drawObjects.empty())
		return;
		
	// camera
	CBPerCamera cbPerCamera;
	cbPerCamera.viewProjMat = Math::Transpose(viewProjMat);
	cbPerCamera.camPos = m_Camera->GetPosition();
	gfx.SetDynamicConstantBufferView(ERSId::PerCamera, sizeof(CBPerCamera), &cbPerCamera);
	// constants
	gfx.SetConstants(ERSId::Constants, 2, 0, 0, 0);	// root0
	// lights
	gfx.SetBufferSRV(ERSId::LightBuffer, m_LightBuffer);
	// sh
	gfx.SetBufferSRV(ERSId::GIBuffer, m_SHOutput);
	
	gfx.SetPipelineState(m_ModelViewerPSO);

	CBPerObject cbPerObject;
	PSConstants psConstants;

#if 0
	const auto& rMeshes = m_Importer->m_oMeshes;
	for (size_t i = 0, imax = rMeshes.size(); i < imax; ++i)
	{
		const auto& curMesh = rMeshes[i];

		int matIdx = curMesh.materialIndex;
		if (m_Importer->IsValidMaterial(matIdx))
		{
			int activeMatIdx = m_Importer->m_ActiveMaterials[matIdx];
			const auto& curMat = m_Importer->m_oMaterials[activeMatIdx];

			// CBPerObject
			glTF::Matrix4x4 trans(std::move(m_Importer->GetMeshTransform(curMesh)));
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
			gfx.SetDynamicDescriptors(4, 0, glTF::Material::TextureNum, m_Importer->GetSRVs(activeMatIdx));

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
#else

	constexpr auto kTextureNum = glTF::Material::TextureNum;
	static D3D12_CPU_DESCRIPTOR_HANDLE kDefaultTextures[kTextureNum] = {
		TextureManager::GetDefaultTexture(EDefaultTexture::kMagenta2D),
		TextureManager::GetDefaultTexture(EDefaultTexture::kBlackOpaque2D),
		TextureManager::GetDefaultTexture(EDefaultTexture::kDefaultNormalMap),
		TextureManager::GetDefaultTexture(EDefaultTexture::kWhiteOpaque2D),
		TextureManager::GetDefaultTexture(EDefaultTexture::kBlackOpaque2D),
	};
	
	ASSERT(drawObjects.size() >= m_VertexBuffers.size() && drawObjects.size() >= m_IndexBuffers.size());
	ASSERT(m_VertexBuffers.size() == m_IndexBuffers.size());
	for (size_t i = 0, imax = drawObjects.size(); i < imax; ++i)
	{
		const auto pObject = drawObjects[i].get();
		if (pObject == nullptr)
			continue;
		
		const auto pVB = m_VertexBuffers[i].get();
		const auto pIB = m_IndexBuffers[i].get();
		if (pVB == nullptr || pIB == nullptr)
			continue;

		gfx.SetVertexBuffer(0, pVB->VertexBufferView());
		gfx.SetIndexBuffer(pIB->IndexBufferView());

		const auto &instances = pObject->instances;
		for (const auto &instance : instances)
		{
			const auto &batchElement = pObject->mesh.batchElements[ instance.elementIndex ];
			
			// Per instance
			const glTF::Matrix4x4 &worldMatrix = instance.transform;
			// Not use 'InvWorldMat'
			// No transpose, use 'mul(mat, vec)' instead (matrix right mul)
			cbPerObject.worldMat = /*glm::transpose */(worldMatrix);
			gfx.SetDynamicConstantBufferView(ERSId::PerObject, sizeof(cbPerObject), &cbPerObject);

			gfx.SetConstant(ERSId::Constants, 0, 3); // roo0, 3 - enabledAttribs (Note: not used yet)

			// PS constants
			gfx.SetDynamicConstantBufferView(ERSId::PerMaterial, sizeof(psConstants), &psConstants);

			// Textures
			gfx.SetDynamicDescriptors(ERSId::Textures, 0, kTextureNum, kDefaultTextures);

			// Draw
			gfx.DrawIndexed(batchElement.indexCount, batchElement.indexOffset, batchElement.vertexOffset);
		}

		// Draw materials
	}
#endif
}

void glTFViewer::ResetCamera(const std::optional<Math::Vector3> &position, const std::optional<Math::AffineTransform> &transform)
{
	if (position.has_value())
	{
		m_Camera->SetPosition(position.value());
	}
	if (transform.has_value())
	{
		m_Camera->SetTransform(transform.value());
	}
}

void glTFViewer::ResetCamera(const Math::Camera& camera)
{
	*m_Camera = camera;
}













