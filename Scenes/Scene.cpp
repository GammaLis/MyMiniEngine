#include "Scene.h"
#include "AssimpImporter.h"
#include "GameInput.h"
#include "Graphics.h"
#include "CommandContext.h"

// compiled shader bytecode
#include "WireframeVS.h"
#include "WireframePS.h"
#include "DepthOnlyVS.h"
#include "DepthOnlyPS.h"
#include "DepthOnlyClipPS.h"
#include "AiCommonVS.h"
#include "AiCommonPS.h"

namespace MFalcor
{
	struct alignas(16) CBPerObject
	{
		Matrix4x4 _WorldMat;
		Matrix4x4 _InvWorldMat;
	};

	struct alignas(16) CBPerCamera
	{
		Matrix4x4 _ViewProjMat;
		Vector3 _CamPos;
	};

	// checks if the transform flips the coordinate system handedness (its determinant is negative)
	bool DoesTransformFlip(const Matrix4x4& mat)
	{
		return MMATH::determinant((Matrix3x3)mat) < 0.0f;
	}

	Scene::SharedPtr Scene::Create(ID3D12Device* pDevice, const std::string& filePath, GameInput* pInput)
	{
		auto pAssimpImporter = AssimpImporter::Create(pDevice, filePath);
		auto pScene = pAssimpImporter ? pAssimpImporter->GetScene(pDevice) : nullptr;
		return pScene;
	}

	bool Scene::Init(ID3D12Device* pDevice, const std::string& filePath, GameInput* pInput)
	{
		bool ret = false;
		auto pAssimpImporter = AssimpImporter::Create();
		if (pAssimpImporter && pAssimpImporter->Load(pDevice, filePath))
			ret = pAssimpImporter->Init(pDevice, this, pInput);

		if (ret)
		{
			// root signature
			{
				m_CommonRS.Reset(6, 2);
				m_CommonRS[0].InitAsConstants(0, 4);
				m_CommonRS[1].InitAsConstantBuffer(1);
				m_CommonRS[2].InitAsConstantBuffer(2);
				m_CommonRS[3].InitAsConstantBuffer(3);
				m_CommonRS[4].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 8);
				m_CommonRS[5].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 2);
				m_CommonRS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearWrapDesc);
				m_CommonRS.InitStaticSampler(1, Graphics::s_CommonStates.SamplerPointClampDesc);
				m_CommonRS.Finalize(pDevice, L"AiCommonRS", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
			}
			// PSO
			{
				auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
				auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;

				// debug wireframe PSO
				m_DebugWireframePSO.SetRootSignature(m_CommonRS);
				auto pVertexInputs = m_VertexLayout;
				const uint32_t numElements = pVertexInputs->GetElementCount();
				D3D12_INPUT_ELEMENT_DESC* inputElements = (D3D12_INPUT_ELEMENT_DESC*)_malloca(numElements * sizeof(D3D12_INPUT_ELEMENT_DESC));
				for (uint32_t i = 0, imax = numElements; i < imax; ++i)
				{
					const auto& layout = pVertexInputs->GetBufferLayout(i);
					inputElements[i] = { layout.semanticName.c_str(), layout.semanticIndex, layout.format, layout.inputSlot, layout.alignedByteOffset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA };
				}
				m_DebugWireframePSO.SetInputLayout(numElements, inputElements);
				m_DebugWireframePSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
				m_DebugWireframePSO.SetVertexShader(WireframeVS, sizeof(WireframeVS));
				m_DebugWireframePSO.SetPixelShader(WireframePS, sizeof(WireframePS));
				m_DebugWireframePSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerDefaultWireframe);
				m_DebugWireframePSO.SetBlendState(Graphics::s_CommonStates.BlendDisable);
				m_DebugWireframePSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateReadWrite);
				m_DebugWireframePSO.SetRenderTargetFormat(colorBuffer.GetFormat(), depthBuffer.GetFormat());
				m_DebugWireframePSO.Finalize(pDevice);	

				// depth PSO
				m_DepthPSO = m_DebugWireframePSO;
				m_DepthPSO.SetVertexShader(DepthOnlyVS, sizeof(DepthOnlyVS));
				m_DepthPSO.SetPixelShader(DepthOnlyPS, sizeof(DepthOnlyPS));
				m_DepthPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerDefault);
				m_DepthPSO.SetBlendState(Graphics::s_CommonStates.BlendNoColorWrite);
				m_DepthPSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateReadWrite);
				m_DepthPSO.SetRenderTargetFormats(0, nullptr, depthBuffer.GetFormat());
				m_DepthPSO.Finalize(pDevice);

				// depth clip PSO
				m_DepthClipPSO = m_DepthPSO;
				m_DepthClipPSO.SetPixelShader(DepthOnlyClipPS, sizeof(DepthOnlyClipPS));
				m_DepthClipPSO.Finalize(pDevice);

				// opaque model 
				m_OpaqueModelPSO = m_DepthPSO;
				m_OpaqueModelPSO.SetVertexShader(AiCommonVS, sizeof(AiCommonVS));
				m_OpaqueModelPSO.SetPixelShader(AiCommonPS, sizeof(AiCommonPS));
				m_OpaqueModelPSO.SetBlendState(Graphics::s_CommonStates.BlendDisable);
				m_OpaqueModelPSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateReadWrite);	// DepthStateTestEqual
				m_OpaqueModelPSO.SetRenderTargetFormat(colorBuffer.GetFormat(), depthBuffer.GetFormat());
				m_OpaqueModelPSO.Finalize(pDevice);

				m_MaskModelPSO = m_OpaqueModelPSO;
				m_MaskModelPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerTwoSided);
				m_MaskModelPSO.Finalize(pDevice);

				// transparent model
				m_TransparentModelPSO = m_OpaqueModelPSO;
				m_TransparentModelPSO.SetBlendState(Graphics::s_CommonStates.BlendTraditional);
				m_TransparentModelPSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateReadOnly);
				m_TransparentModelPSO.Finalize(pDevice);

				_freea(inputElements);
			}
		}

		return ret;
	}

	void Scene::Finalize()
	{
		// instance data sort by AlphaMode
		std::sort(m_MeshInstanceData.begin(), m_MeshInstanceData.end(), [this](const MeshInstanceData& lhs, const MeshInstanceData& rhs) {
			const auto& lmat = m_Materials[lhs.materialID];
			const auto& rmat = m_Materials[rhs.materialID];
			return lmat->eAlphaMode < rmat->eAlphaMode;
			});
		
		// create mapping of meshes to their instances
		m_MeshIdToInstanceIds.clear();
		m_MeshIdToInstanceIds.resize(m_MeshDescs.size());
		for (uint32_t i = 0, imax = (uint32_t)m_MeshInstanceData.size(); i < imax; ++i)
		{
			m_MeshIdToInstanceIds[m_MeshInstanceData[i].meshID].push_back(i);
		}

		InitResources();

		UpdateMatrices();

		UpdateBounds();

		if (m_Camera == nullptr)
		{
			m_Camera = std::make_shared<Math::Camera>();
		}
		ResetCamera();
		m_Camera->Update();
		m_CameraController.reset(new CameraController(*m_Camera, Math::Vector3(Math::kYUnitVector), *m_pInput));

		SaveNewViewport();
		UpdateGeometryStats();
	}

	Scene::UpdateFlags Scene::Update(float deltaTime)
	{
		m_CameraController->Update(deltaTime);

		return UpdateFlags();
	}

	void Scene::Render(GraphicsContext& gfx, AlphaMode alphaMode)
	{
		gfx.SetConstants(0, 0, 1, 2, 3);
		CBPerObject cbPerObject;
		if (alphaMode == AlphaMode::UNKNOWN)
		{
			RenderByAlphaMode(gfx, m_OpaqueModelPSO, AlphaMode::kOPAQUE);
			RenderByAlphaMode(gfx, m_MaskModelPSO, AlphaMode::kMASK);
			RenderByAlphaMode(gfx, m_TransparentModelPSO, AlphaMode::kBLEND);
		}
		else if (alphaMode == AlphaMode::kOPAQUE)
		{
			RenderByAlphaMode(gfx, m_OpaqueModelPSO, AlphaMode::kOPAQUE);
		}
		else if (alphaMode == AlphaMode::kMASK)
		{
			RenderByAlphaMode(gfx, m_MaskModelPSO, AlphaMode::kMASK);
		}
		else if (alphaMode == AlphaMode::kBLEND)
		{
			RenderByAlphaMode(gfx, m_TransparentModelPSO, AlphaMode::kBLEND);
		}
	}

	void Scene::BeginRendering(GraphicsContext& gfx)
	{
		gfx.SetRootSignature(m_CommonRS);
		gfx.SetVertexBuffer(0, m_VertexBuffer->VertexBufferView());
		gfx.SetIndexBuffer(m_IndexBuffer->IndexBufferView());
		gfx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}

	void Scene::SetRenderCamera(GraphicsContext& gfx, const Matrix4x4& viewProjMat, const Vector3& camPos)
	{
		CBPerCamera cbPerCamera;
		cbPerCamera._ViewProjMat = viewProjMat;
		cbPerCamera._CamPos = camPos;
		gfx.SetDynamicConstantBufferView(2, sizeof(CBPerCamera), &cbPerCamera);
	}

	void Scene::RenderByAlphaMode(GraphicsContext& gfx, GraphicsPSO& pso, AlphaMode alphaMode)
	{
		gfx.SetPipelineState(pso);
		CBPerObject cbPerObject;
		uint32_t curMatId = 0xFFFFFFFFul;
		for (uint32_t instanceId = 0, maxInstanceId = (uint32_t)m_MeshInstanceData.size(); instanceId < maxInstanceId; ++instanceId)
		{
			const auto& instanceData = m_MeshInstanceData[instanceId];
			const auto& meshData = m_MeshDescs[instanceData.meshID];
			const auto& material = m_Materials[instanceData.materialID];
			const auto& worldMat = m_GlobalMatrices[instanceData.globalMatrixID];
			if (material->eAlphaMode == alphaMode)
			{
				cbPerObject._WorldMat = worldMat;
				cbPerObject._InvWorldMat = MMATH::inverse(worldMat);
				gfx.SetDynamicConstantBufferView(1, sizeof(CBPerObject), &cbPerObject);

				gfx.SetConstantBuffer(3, m_MaterialsBuffer.GetInstanceGpuPointer(instanceData.materialID));

				if (curMatId != instanceData.materialID)
				{
					curMatId = instanceData.materialID;
					
					const auto& srvs = material->GetDescriptors();
					
					gfx.SetDynamicDescriptors(4, 0, (UINT)srvs.size(), srvs.data());
				}				

				gfx.DrawIndexed(meshData.indexCount, meshData.indexByteOffset / meshData.indexStrideSize, meshData.vertexOffset);
			}
		}
	}

	void Scene::Clean()
	{
		m_MaterialsBuffer.Destroy();

		m_VertexBuffer->Destroy();
		m_IndexBuffer->Destroy();
	}

	Material::SharedPtr Scene::GetMaterialByName(const std::string& name) const
	{
		for (const auto& mat : m_Materials)
		{
			if (mat->GetName() == name) return mat;
		}
		return nullptr;
	}

	void Scene::InitResources()
	{
		// material buffer
		m_MaterialsBuffer.Create(Graphics::s_Device, L"MaterialsBuffer", (uint32_t)m_Materials.size(), sizeof(MaterialData), true);
		for (uint32_t matId = 0, maxId = (uint32_t)m_Materials.size(); matId < maxId; ++matId)
		{
			const auto& curMat = m_Materials[matId];
			const auto& matData = curMat->GetMaterialData();
			m_MaterialsBuffer.CopyToGpu((void*)&matData, sizeof(MaterialData), matId);
		}
	}

	void Scene::UpdateMatrices()
	{
		// local matrices
		m_LocalMatrices.resize(m_SceneGraph.size());
		uint32_t i = 0;
		std::for_each(m_LocalMatrices.begin(), m_LocalMatrices.end(), [this, &i](Matrix4x4& localMat) {
			localMat = m_SceneGraph[i++].transform;
			});

		// global matrices
		i = 0;
		m_GlobalMatrices = m_LocalMatrices;
		m_InvTransposeGlobalMatrices.resize(m_SceneGraph.size());
		std::for_each(m_GlobalMatrices.begin(), m_GlobalMatrices.end(), [this, &i](Matrix4x4& globalMat) {
			if (m_SceneGraph[i].parentIndex != kInvalidNode)
			{
				auto parentIdx = m_SceneGraph[i].parentIndex;
				globalMat = globalMat * m_GlobalMatrices[parentIdx];	// m_GlobalMatrices[i]
				// 注：按理应是 m_GlobalMatrices[parentIdx] * globalMat，但是结果不对，不懂 ？？？ -2020-4-8
			}
			m_InvTransposeGlobalMatrices[i] = MMATH::transpose(MMATH::inverse(globalMat));
			++i;
			});
	}

	// 
	void Scene::UpdateBounds()
	{
		std::vector<BoundingBox> instanceBBs;
		instanceBBs.reserve(m_MeshInstanceData.size());

		for (const auto& inst : m_MeshInstanceData)
		{
			const BoundingBox& meshBB = m_MeshBBs[inst.meshID];
			const Matrix4x4& transform = m_GlobalMatrices[inst.globalMatrixID];
			instanceBBs.push_back(meshBB.Transform(transform));
		}

		m_SceneBB = instanceBBs.front();
		for (const auto& bb : instanceBBs)
		{
			m_SceneBB.Union(bb);
		}
	}

	Scene::UpdateFlags Scene::UpdateMaterials(bool forceUpdate)
	{
		return UpdateFlags::MaterialsChanged;
	}

	void Scene::UpdateGeometryStats()
	{
		m_GeometryStats = {};
		auto& s = m_GeometryStats;

		for (uint32_t meshId = 0, maxId = (uint32_t)m_MeshDescs.size(); meshId < maxId; ++meshId)
		{
			const auto& meshDesc = m_MeshDescs[meshId];
			s.uniqueVertexCount += meshDesc.vertexCount;
			s.uniqueTriangleCount += meshDesc.indexCount / 3;
		}
		for (uint32_t instanceId = 0, maxId = (uint32_t)m_MeshInstanceData.size(); instanceId < maxId; ++instanceId)
		{
			const auto& instance = m_MeshInstanceData[instanceId];
			const auto& meshDesc = m_MeshDescs[instance.meshID];
			s.instancedVertexCount += meshDesc.vertexCount;
			s.instancedTriangleCount += meshDesc.indexCount / 3;
		}
	}

	void Scene::ResetCamera(bool bResetDepthRange)
	{
		float radius = MMATH::length(m_SceneBB.GetExtent());
		Vector3 center = m_SceneBB.GetCenter();
		Math::Vector3 meye{ center.x, center.y, center.z };
		Math::Vector3 mtarget = meye + Math::Vector3(0, 0, 1);
		Math::Vector3 up{ 0, 1, 0 };
		m_Camera->SetEyeAtUp(meye, mtarget, up);

		if (bResetDepthRange)
		{
			float nearZ = std::max(0.1f, radius / 750.0f);
			float farZ = radius * 50;
			m_Camera->SetZRange(nearZ, farZ);
		}
	}

	void Scene::SaveNewViewport()
	{
		auto cam = m_Camera;
		auto position = m_Camera->GetPosition();
		auto target = position + m_Camera->GetForwardVec();
		auto up = m_Camera->GetUpVec();

		// TO DO 
	}

}
