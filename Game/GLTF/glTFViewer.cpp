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

#ifndef USE_DESCRIPTOR_HEAP_INDEX
#define USE_DESCRIPTOR_HEAP_INDEX 1
#endif

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
		uint32_t materialIndex;
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
		DirectX::XMUINT4 textureIndices[2];
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

	using BaseMaterial = glTF::BaseMaterial;

	static uint32_t GetTextureIndex(uint32_t index, uint32_t materialTextureStart, uint32_t defaultTextureStart)
	{
		bool bValid = BaseMaterial::IsTextureValid(index);
		return bValid ?
			index + materialTextureStart :
			BaseMaterial::DecodeDefaultTextureIndex(index) + defaultTextureStart;
	}

	void Cast(PSConstants& dst, const glTF::BaseMaterial &src, uint32_t materialTextureStart, uint32_t defaultTextureStart = 0) 
	{
		// TODO...
		dst.alphaCutout = src.alphaCutoff;
		
		// Textures
		dst.textureIndices[0].x = GetTextureIndex(src.textures[0], materialTextureStart, defaultTextureStart); 
		dst.textureIndices[0].y = GetTextureIndex(src.textures[1], materialTextureStart, defaultTextureStart); 
		dst.textureIndices[0].z = GetTextureIndex(src.textures[2], materialTextureStart, defaultTextureStart); 
		dst.textureIndices[0].w = GetTextureIndex(src.textures[3], materialTextureStart, defaultTextureStart); 
		dst.textureIndices[1].x = GetTextureIndex(src.textures[4], materialTextureStart, defaultTextureStart); 
	}
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

	// Update textures
	if (!m_TextureIndexMap.empty())
	{
		Graphics::s_TextureManager.DeferredUpdate();
	}

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
				// UpdateMeshDescriptors(fileName, m_DrawObjects[it->second]->materials);
				UpdateTextures(fileName, m_DrawObjects[it->second]->textures);
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

	m_FrameDescriptorHeap.EndFrame();
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
	m_DrawObjects.emplace_back(drawObject);
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
		auto elementSize = static_cast<uint32_t>(sizeof(srcMesh->vertices[0]));
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

void glTFViewer::UpdateMeshDescriptors(const std::string &objName, const glTF::DrawObject &drawObject)
{
	// Update materials
	const auto &materials = drawObject.materials;
	
	uint32_t start = 0;
	uint32_t numDescriptors = static_cast<uint32_t>(materials.size()) * glTF::BaseMaterial::kTextureNum;
	
	auto iter = m_NameAndDescStartMap.find(objName);
	if (iter != m_NameAndDescStartMap.end())
	{
		start = iter->second;
	}
	else
	{
		auto alloc = m_FrameDescriptorHeap.AllocPersistent(numDescriptors);
		start = alloc.index;
		// m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor()
		m_NameAndDescStartMap.insert({ objName, start });
	}

	for (uint32_t i = 0; i < static_cast<uint32_t>(materials.size()); i++)
	{
		const auto &mat = materials[i];
		std::vector<DescriptorHandle> descriptorHandles; 
		for (auto tex : mat.textures)
		{
			// FIXME:
			// descriptorHandles.emplace_back(tex->GetSRV());
		}
		m_FrameDescriptorHeap.UpdatePersistentDescriptors(Graphics::s_Device, start, descriptorHandles);
		start += static_cast<uint32_t>( descriptorHandles.size() );
	}
}

void glTFViewer::UpdateTextures(const std::string &name,  const std::vector<const ManagedTexture*>& textures)
{
	static constexpr uint32_t kMaxNumPerCopy = 8;

	uint32_t numTextures = static_cast<uint32_t>(textures.size());
	uint32_t N = DivideAndRoundUp(numTextures, kMaxNumPerCopy);

	uint32_t start = 0;
	if (auto iter = m_NameAndDescStartMap.find(name); iter != m_NameAndDescStartMap.end())
	{
		start = iter->second;
	}
	else
	{
		auto alloc = m_FrameDescriptorHeap.AllocPersistent(numTextures);
		start = alloc.index;
		m_NameAndDescStartMap.insert({ name, start });
	}
	
	for (uint32_t p = 0 ; p < N; p++)
	{
		std::vector<DescriptorHandle> descriptorHandles;
		uint32_t imin = p * kMaxNumPerCopy, imax = std::min(imin+kMaxNumPerCopy, numTextures);
		for (uint32_t i = imin ; i < imax; i++)
		{
			// FIXME: need lock ???
			if (textures[i]->IsLoading())
			{
				m_TextureIndexMap[textures[i]] = start + i;
			}
			descriptorHandles.emplace_back(textures[i]->GetSRV());
		}
		m_FrameDescriptorHeap.UpdatePersistentDescriptors(Graphics::s_Device, imin + start, descriptorHandles);
	}
}

bool glTFViewer::UpdateTexture(const ManagedTexture* texture)
{
	auto iter = m_TextureIndexMap.find(texture); 
	if (iter == m_TextureIndexMap.end())
		return false;

	uint32_t descIndex = iter->second;
	m_FrameDescriptorHeap.UpdatePersistentDescriptor(Graphics::s_Device, descIndex, texture->GetSRV());
	m_TextureIndexMap.erase(texture);
	return true;
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

	auto pDevice = Graphics::s_Device;

	// Init model
	// ASSERT(m_Importer->Create(Graphics::s_Device));
	// Need not init!  
	// Graphics::s_TextureManager.Init(L"Textures/");

	// Root signature & pso
	{
		// Root signature
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

#if 0
		m_ModelViewerPSO.SetRootSignature(m_CommonRS);
#else
		m_ModelViewerPSO.SetRootSignature(Graphics::s_CommonStates.GlobalBindlessRS);
#endif
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
		// Viewport & scissor
		UpdateViewportAndScissor(m_MainViewport, m_MainScissor, 0, 0, static_cast<float>(bufferWidth), static_cast<float>(bufferHeight));
	}

	// Descriptors
	m_FrameDescriptorHeap.Create(pDevice, L"FrameDescriptorHeap", kMaxDescriptorNum/2);

	// Init textures
	{
		InitDefaultTextures();
		Graphics::s_TextureManager.BindUpdateCallback([this](const ManagedTexture* texture)
		{
			return UpdateTexture(texture);
		});
	}

	// Camera
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

	// Lights
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

		m_LightBufferDescIndex = m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, m_LightBuffer.GetSRV());
	}

#pragma region SH
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

		InitCustom();
	}
#pragma endregion

	m_ViewBuffer.Create(pDevice, L"ViewUniformBuffer", 1, sizeof(CBPerCamera), true, true);
	m_MaterialBuffer.Create(pDevice, L"MaterialBuffer", kMaxMaterialNum, sizeof(PSConstants), false, true);
	m_InstanceBuffer.Create(pDevice, L"InstanceBuffer", kMaxInstanceNum, sizeof(CBPerObject), false, true);
	
	return true;
}

bool glTFViewer::InitCustom()
{
	auto pDevice = Graphics::s_Device;
	
	// SH
	struct SH9Color
	{
		XMFLOAT3 c[9];
	};
	
	constexpr UINT GroupSizeX = 32;
	constexpr UINT GroupSizeY = 32;
	UINT picWidth;
	UINT picHeight;
	{
		// Texture
		std::wstring filePath = L"Textures/grasscube1024.dds";
		filePath = Utility::RemoveExtension(filePath);
		const auto texture = Graphics::s_TextureManager.LoadFromFile(Graphics::s_Device, filePath);
		m_SHsrv = texture->GetSRV();
		auto desc = const_cast<ID3D12Resource*>(texture->GetResource())->GetDesc();
		picWidth = (UINT)desc.Width;
		picHeight = desc.Height;

		UINT numGroupX = Math::DivideByMultiple(picWidth, GroupSizeX);
		UINT numGroupY = Math::DivideByMultiple(picHeight, GroupSizeY);
		// Buffer
		m_SHOutput.Create(Graphics::s_Device, L"SHBuffer", numGroupX * numGroupY, sizeof(SH9Color));

		m_SHBufferDescIndex = m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, m_SHOutput.GetSRV());
	}
	
	// Precomputing SH coefficients
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

	Graphics::s_TextureManager.BindUpdateCallback(nullptr);
	
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

	m_ViewBuffer.Destroy();
	m_MaterialBuffer.Destroy();
	m_InstanceBuffer.Destroy();
	
	m_GlobalVertexBuffer.Destroy();
	m_GlobalIndexBuffer.Destroy();

	m_FrameDescriptorHeap.Destroy();

	// SH
	m_SHOutput.Destroy();
}

void glTFViewer::InitDefaultTextures()
{
	constexpr uint32_t numDefaultTextures = static_cast<uint32_t>(EDefaultTexture::kNumDefaultTextures);

	// Alloc
	auto alloc = m_FrameDescriptorHeap.AllocPersistent(numDefaultTextures);
	m_DefaultTextureDescIndex = alloc.index;
	// Copy
	std::vector<DescriptorHandle> defaultTextureSRVs;
	for (uint32_t i = 0; i < numDefaultTextures; i++)
	{
		defaultTextureSRVs.emplace_back( TextureManager::GetDefaultTextureDescriptor(static_cast<EDefaultTexture>(i)));
	}
	m_FrameDescriptorHeap.UpdatePersistentDescriptors(Graphics::s_Device, alloc.index, defaultTextureSRVs);
}

void glTFViewer::RenderObjects(GraphicsContext& gfx, const Math::Matrix4 &viewProjMat, ObjectFilter filter)
{
	const auto &drawObjects = m_DrawObjects;
	if (drawObjects.empty())
		return;

	auto pDevice = Graphics::s_Device;

	uint32_t frameFactor = m_Gfx->GetCurrentFrameIndex();
		
	// Note: 'SetDescriptorHeap' must be set before 'SetRootSignature' 
	gfx.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_FrameDescriptorHeap.CurrentHeap()->GetHeapPointer());
	gfx.SetRootSignature(Graphics::s_CommonStates.GlobalBindlessRS);

	// camera
	CBPerCamera cbPerCamera;
	cbPerCamera.viewProjMat = Math::Transpose(viewProjMat);
	cbPerCamera.camPos = m_Camera->GetPosition();
#if 0
    gfx.SetDynamicConstantBufferView(ERSId::PerCamera, sizeof(CBPerCamera), &cbPerCamera);
#else
	m_ViewBuffer.CopyToGpu(&cbPerCamera, sizeof(cbPerCamera), 0, frameFactor);
#endif
	m_FrameDescriptorHeap.AllocAndCopyTemporaryDescriptor(pDevice, m_ViewBuffer.GetCBV(frameFactor));
	
    // Constants
	// TODO: update some per frame data
	constexpr uint32_t numConstants = 8;
	float pushConstants[numConstants] = { };
	pushConstants[4] = static_cast<float>(m_LightBufferDescIndex);
	pushConstants[5] = static_cast<float>(m_SHBufferDescIndex);
    // gfx.SetConstants(ERSId::Constants, _countof(lightBufferAndSH), lightBufferAndSH, destOffset);

#if 0
    // lights
    gfx.SetBufferSRV(ERSId::LightBuffer, m_LightBuffer);
    // sh
    gfx.SetBufferSRV(ERSId::GIBuffer, m_SHOutput);
#endif
	
	gfx.SetPipelineState(m_ModelViewerPSO);

	CBPerObject cbPerObject;
	PSConstants psConstants;
	
	constexpr auto kTextureNum = glTF::Material::TextureNum;
	static D3D12_CPU_DESCRIPTOR_HANDLE kDefaultTextures[kTextureNum] = {
		TextureManager::GetDefaultTextureDescriptor(EDefaultTexture::kMagenta2D),
		TextureManager::GetDefaultTextureDescriptor(EDefaultTexture::kBlackOpaque2D),
		TextureManager::GetDefaultTextureDescriptor(EDefaultTexture::kDefaultNormalMap),
		TextureManager::GetDefaultTextureDescriptor(EDefaultTexture::kWhiteOpaque2D),
		TextureManager::GetDefaultTextureDescriptor(EDefaultTexture::kBlackOpaque2D),
	};
	
	ASSERT(drawObjects.size() >= m_VertexBuffers.size() && drawObjects.size() >= m_IndexBuffers.size());
	ASSERT(m_VertexBuffers.size() == m_IndexBuffers.size());

	uint32_t globalMaterialIndex = 0;
	uint32_t globalInstanceIndex = 0;
	uint32_t instanceMaterialOffset = 0;

	// Update instances & materials
	std::vector<PSConstants> materialData;
	std::vector<CBPerObject> instanceData;

	for (size_t i = 0, imax = drawObjects.size(); i < imax; ++i)
	{
		const auto pObject = drawObjects[i].get();
		if (pObject == nullptr)
			continue;

		const auto pVB = m_VertexBuffers[i].get();
		const auto pIB = m_IndexBuffers[i].get();
		if (pVB == nullptr || pIB == nullptr)
			continue;

		auto iter = m_NameAndDescStartMap.find(pObject->name);
		ASSERT(iter != m_NameAndDescStartMap.end());
		uint32_t materialTextureStart = iter->second;

		// Materials
		for (const auto& mat : pObject->materials)
		{
			Cast(psConstants, mat, materialTextureStart, m_DefaultTextureDescIndex);
			materialData.emplace_back(psConstants);

			// Or
			// m_MaterialBuffer.CopyToGpu(&psConstants, sizeof(psConstants), globalMaterialIndex, frameFactor);

			globalMaterialIndex++;
		}
		
		for (const auto& instance : pObject->instances)
		{
			const auto& batchElement = pObject->mesh.batchElements[instance.elementIndex];

			// Per instance
			const glTF::Matrix4x4& worldMatrix = instance.transform;
			// Not use 'InvWorldMat'
			// No transpose, use 'mul(mat, vec)' instead (matrix right mul)
			cbPerObject.worldMat = /*glm::transpose */(worldMatrix);
			cbPerObject.materialIndex = batchElement.materialIndex + instanceMaterialOffset;
			instanceData.emplace_back(cbPerObject);

			// Or
			// m_InstanceBuffer.CopyToGpu(&cbPerObject, sizeof(cbPerObject), globalInstanceIndex, frameFactor);

			globalInstanceIndex++;
		}

		instanceMaterialOffset += static_cast<uint32_t>(pObject->materials.size());
	}
	// Update materials
	m_MaterialBuffer.CopyToGpu(materialData.data(), static_cast<uint32_t>(materialData.size() * sizeof(PSConstants)), 0, frameFactor);
	m_InstanceBuffer.CopyToGpu(instanceData.data(), static_cast<uint32_t>(instanceData.size() * sizeof(CBPerObject)), 0, frameFactor);
	m_FrameDescriptorHeap.AllocAndCopyTemporaryDescriptor(pDevice, m_MaterialBuffer.GetSRV(frameFactor));
	m_FrameDescriptorHeap.AllocAndCopyTemporaryDescriptor(pDevice, m_InstanceBuffer.GetSRV(frameFactor));

	globalInstanceIndex = 0;
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

		for (const auto &instance : pObject->instances)
		{
			const auto &batchElement = pObject->mesh.batchElements[ instance.elementIndex ];
	
			// DrawId
			pushConstants[0] = (float)globalInstanceIndex;
			gfx.SetConstants(ERSId::Constants, numConstants, pushConstants);

			// Draw
			gfx.DrawIndexed(batchElement.indexCount, batchElement.indexOffset, batchElement.vertexOffset);

			globalInstanceIndex++;
		}
	}
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
