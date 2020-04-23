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

#include "CommonIndirectVS.h"
#include "CommonIndirectPS.h"

#include "IndirectGBufferVS.h"
#include "IndirectGBufferPS.h"
#include "DeferredCS.h"

namespace MFalcor
{
	// deferred render targets
	static const DXGI_FORMAT rtFormats[] =
	{
		DXGI_FORMAT_R10G10B10A2_UNORM,
		DXGI_FORMAT_R16G16B16A16_SNORM,
		DXGI_FORMAT_R16G16B16A16_SNORM,
		DXGI_FORMAT_R8_UINT
	};

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

	struct GlobalMatrix
	{
		Matrix4x4 worldMat;
		Matrix4x4 invWorldMat;
	};

	struct alignas(16) CSConstants
	{
		uint32_t _ViewportWidth, _ViewportHeight;
		float _NearClip, _FarClip;
		XMFLOAT4 _CamPos;
		Matrix4x4 _ViewProjMat;
		Matrix4x4 _InvViewProjMat;
		Matrix4x4 _ViewMat;
		Matrix4x4 _ProjMat;
	};

	// checks if the transform flips the coordinate system handedness (its determinant is negative)
	bool DoesTransformFlip(const Matrix4x4& mat)
	{
		return MMATH::determinant((Matrix3x3)mat) < 0.0f;
	}

	Scene::SharedPtr Scene::Create(ID3D12Device* pDevice, const std::string& filePath, GameInput* pInput, const InstanceMatrices& instances)
	{
		auto pAssimpImporter = AssimpImporter::Create(pDevice, filePath, instances);
		auto pScene = pAssimpImporter ? pAssimpImporter->GetScene(pDevice) : nullptr;
		return pScene;
	}

	bool Scene::Init(ID3D12Device* pDevice, const std::string& filePath, GameInput* pInput, const InstanceMatrices& instances)
	{
		bool ret = false;
		auto pAssimpImporter = AssimpImporter::Create();
		if (pAssimpImporter && pAssimpImporter->Load(pDevice, filePath, instances))
			ret = pAssimpImporter->Init(pDevice, this, pInput);

		if (ret)
		{
			// root signatures
			{
				// common RS
				m_CommonRS.Reset(7, 2);
				m_CommonRS[0].InitAsConstants(0, 4);
				m_CommonRS[1].InitAsConstantBuffer(1);	// cbPerObject
				m_CommonRS[2].InitAsConstantBuffer(2);	// cbPerCamera
				m_CommonRS[3].InitAsConstantBuffer(3);	// cbPerMaterial
				m_CommonRS[4].InitAsConstantBuffer(4);	// cbLights
				m_CommonRS[5].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 8);
				m_CommonRS[6].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 2);
				m_CommonRS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearWrapDesc);
				m_CommonRS.InitStaticSampler(1, Graphics::s_CommonStates.SamplerPointClampDesc);
				m_CommonRS.Finalize(pDevice, L"AiCommonRS", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

				// common indirect RS
				m_CommonIndirectRS.Reset(10, 2);
				m_CommonIndirectRS[0].InitAsConstants(0, 4);
				m_CommonIndirectRS[1].InitAsConstantBuffer(1);
				m_CommonIndirectRS[2].InitAsConstantBuffer(2);
				m_CommonIndirectRS[3].InitAsBufferSRV(0, 1);
				m_CommonIndirectRS[4].InitAsBufferSRV(1, 1);
				m_CommonIndirectRS[5].InitAsBufferSRV(2, 1);
				m_CommonIndirectRS[6].InitAsBufferSRV(3, 1);
				m_CommonIndirectRS[7].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, -1, 1, D3D12_SHADER_VISIBILITY_PIXEL);
				m_CommonIndirectRS[8].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 8, 0, D3D12_SHADER_VISIBILITY_PIXEL);
				m_CommonIndirectRS[9].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 2);
				m_CommonIndirectRS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearWrapDesc);
				m_CommonIndirectRS.InitStaticSampler(1, Graphics::s_CommonStates.SamplerPointClampDesc);
				m_CommonIndirectRS.Finalize(pDevice, L"CommonIndirectRS", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

				// gbuffer
				m_GBufferRS.Reset(6);
				m_GBufferRS[0].InitAsConstants(0, 4);
				m_GBufferRS[1].InitAsConstantBuffer(1);
				m_GBufferRS[2].InitAsBufferSRV(0, 1);
				m_GBufferRS[3].InitAsBufferSRV(1, 1);
				m_GBufferRS[4].InitAsBufferSRV(2, 1);
				m_GBufferRS[5].InitAsBufferSRV(3, 1);
				m_GBufferRS.Finalize(pDevice, L"GBufferRS", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

				// deferred cs
				m_DeferredCSRS.Reset(7, 2);
				m_DeferredCSRS[0].InitAsConstantBuffer(0);
				m_DeferredCSRS[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 5);
				// 单独设置 MaterialIDTarget	-目前会有问题 -2020-4-21
				// m_DeferredCSRS[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 65, 1);
				m_DeferredCSRS[2].InitAsBufferSRV(1, 1);
				m_DeferredCSRS[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, -1, 1);
				m_DeferredCSRS[4].InitAsBufferSRV(0, 2);	// decal buffer
				m_DeferredCSRS[5].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 10, 2);	// decal textures
				m_DeferredCSRS[6].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
				m_DeferredCSRS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearWrapDesc);
				m_DeferredCSRS.InitStaticSampler(1, Graphics::s_CommonStates.SamplerPointClampDesc);
				m_DeferredCSRS.Finalize(pDevice, L"DeferredCSRS");
			}
			// command signatures
			{
				m_CommandSignature.Reset(1);
				m_CommandSignature[0].DrawIndexed();
				m_CommandSignature.Finalize(pDevice);
			}
			// PSOs
			{
				auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
				auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;

				// debug wireframe PSO
				m_DebugWireframePSO.SetRootSignature(m_CommonRS);
				// per vertex data
				auto pVertexInputs = m_VertexLayout;
				const uint32_t numVertexElements = pVertexInputs->GetElementCount();
				// per instance data
				auto pInstanceInputs = m_InstanceLayout;
				const uint32_t numInstanceElements = pInstanceInputs ? pInstanceInputs->GetElementCount() : 0;

				uint32_t numElements = numVertexElements + numInstanceElements;
				std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements(numElements);
				uint32_t inputElementIdx = 0;
				for (uint32_t i = 0, imax = numVertexElements; i < imax; ++i, ++inputElementIdx)
				{
					const auto& layout = pVertexInputs->GetBufferLayout(i);
					inputElements[inputElementIdx] = 
					{ layout.semanticName.c_str(), layout.semanticIndex, layout.format, layout.inputSlot, layout.alignedByteOffset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
				}
				for (uint32_t i = 0, imax = numInstanceElements; i < imax; ++i, ++inputElementIdx)
				{
					const auto& layout = pInstanceInputs->GetBufferLayout(i);
					inputElements[inputElementIdx] = 
					{ layout.semanticName.c_str(), layout.semanticIndex, layout.format, layout.inputSlot, layout.alignedByteOffset, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, pInstanceInputs->GetInstanceStepRate() };
				}

				m_DebugWireframePSO.SetInputLayout(numElements, inputElements.data());
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

				// mask model
				m_MaskModelPSO = m_OpaqueModelPSO;
				m_MaskModelPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerTwoSided);
				m_MaskModelPSO.Finalize(pDevice);

				// transparent model
				m_TransparentModelPSO = m_OpaqueModelPSO;
				m_TransparentModelPSO.SetBlendState(Graphics::s_CommonStates.BlendTraditional);
				m_TransparentModelPSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateReadOnly);
				m_TransparentModelPSO.Finalize(pDevice);

				// indirect drawing
				// opaque indirect model
				m_OpaqueIndirectPSO = m_OpaqueModelPSO;
				m_OpaqueIndirectPSO.SetRootSignature(m_CommonIndirectRS);
				m_OpaqueIndirectPSO.SetVertexShader(CommonIndirectVS, sizeof(CommonIndirectVS));
				m_OpaqueIndirectPSO.SetPixelShader(CommonIndirectPS, sizeof(CommonIndirectPS));
				m_OpaqueIndirectPSO.Finalize(pDevice);

				// mask indirect model
				m_MaskIndirectPSO = m_OpaqueIndirectPSO;
				m_MaskIndirectPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerTwoSided);
				m_MaskIndirectPSO.Finalize(pDevice);

				// transparent indirect model
				m_TransparentIndirectPSO = m_OpaqueIndirectPSO;
				m_TransparentIndirectPSO.SetBlendState(Graphics::s_CommonStates.BlendTraditional);
				m_TransparentIndirectPSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateReadOnly);
				m_TransparentIndirectPSO.Finalize(pDevice);

				// gbuffer
				m_GBufferPSO = m_OpaqueIndirectPSO;
				m_GBufferPSO.SetRootSignature(m_GBufferRS);
				m_GBufferPSO.SetVertexShader(IndirectGBufferVS, sizeof(IndirectGBufferVS));
				m_GBufferPSO.SetPixelShader(IndirectGBufferPS, sizeof(IndirectGBufferPS));
				m_GBufferPSO.SetRenderTargetFormats(_countof(rtFormats), rtFormats, depthBuffer.GetFormat());
				m_GBufferPSO.Finalize(pDevice);

				// deferred cs
				m_DeferredCSPSO.SetRootSignature(m_DeferredCSRS);
				m_DeferredCSPSO.SetComputeShader(DeferredCS, sizeof(DeferredCS));
				m_DeferredCSPSO.Finalize(pDevice);
			}

			// bindless deferred
			m_BindlessDeferred.Init(pDevice, this);
		}

		return ret;
	}

	void Scene::Finalize(ID3D12Device* pDevice)
	{
		SortMeshInstances();

		CreateInstanceBuffer(pDevice);
		
		// create mapping of meshes to their instances
		m_MeshIdToInstanceIds.clear();
		m_MeshIdToInstanceIds.resize(m_MeshDescs.size());
		for (uint32_t i = 0, imax = (uint32_t)m_MeshInstanceData.size(); i < imax; ++i)
		{
			m_MeshIdToInstanceIds[m_MeshInstanceData[i].meshID].push_back(i);
		}

		UpdateMatrices();

		UpdateBounds();

		InitResources(pDevice);

		CreateDrawList(pDevice);

		if (m_Camera == nullptr)
		{
			m_Camera = std::make_shared<Math::Camera>();
		}
		ResetCamera(true);
		m_Camera->Update();
		m_CameraController.reset(new CameraController(*m_Camera, Math::Vector3(Math::kYUnitVector), *m_pInput));

		SaveNewViewport();
		UpdateGeometryStats();
	}

	Scene::UpdateFlags Scene::Update(float deltaTime)
	{
		m_CameraController->Update(deltaTime);

		UpdateSunLight();

		return UpdateFlags();
	}

	void Scene::Render(GraphicsContext& gfx, AlphaMode alphaMode)
	{
		gfx.SetConstants((UINT)CommonRSId::CBConstants, 0, 1, 2, 3);
		
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

	void Scene::BeginRendering(GraphicsContext& gfx, bool bIndirectRendering)
	{
		gfx.SetVertexBuffer(0, m_VertexBuffer->VertexBufferView());
		gfx.SetIndexBuffer(m_IndexBuffer->IndexBufferView());
		if (m_InstanceBuffer)
			gfx.SetVertexBuffer(1, m_InstanceBuffer->VertexBufferView());
		gfx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}

	void Scene::SetRenderCamera(GraphicsContext& gfx, const Matrix4x4& viewProjMat, const Vector3& camPos, UINT rootIdx)
	{
		CBPerCamera cbPerCamera;
		cbPerCamera._ViewProjMat = viewProjMat;
		cbPerCamera._CamPos = camPos;
		gfx.SetDynamicConstantBufferView(rootIdx, sizeof(CBPerCamera), &cbPerCamera);
	}

	void Scene::RenderByAlphaMode(GraphicsContext& gfx, GraphicsPSO& pso, AlphaMode alphaMode)
	{
		std::vector<uint32_t>* instanceIds = nullptr;
		switch (alphaMode)
		{
		default:
		case AlphaMode::kOPAQUE:
			instanceIds = &m_OpaqueInstances;
			break;
		case AlphaMode::kMASK:
			instanceIds = &m_MaskInstancs;
			break;
		case AlphaMode::kBLEND:
			instanceIds = &m_TransparentInstances;
			break;
		}
		if (instanceIds->empty())
			return;

		gfx.SetPipelineState(pso);
		CBPerObject cbPerObject;
		uint32_t curMatId = 0xFFFFFFFFul;

		for (auto instanceId : (*instanceIds))
		{
			const auto& instanceData = m_MeshInstanceData[instanceId];
			const auto& meshData = m_MeshDescs[instanceData.meshID];
			const auto& material = m_Materials[instanceData.materialID];
			const auto& worldMat = m_GlobalMatrices[instanceData.globalMatrixID];
			
			cbPerObject._WorldMat = MMATH::transpose( worldMat);
			cbPerObject._InvWorldMat = MMATH::inverse(worldMat);
			gfx.SetDynamicConstantBufferView((UINT)CommonRSId::CBPerObject, sizeof(CBPerObject), &cbPerObject);

			gfx.SetConstantBuffer((UINT)CommonRSId::CBPerMaterial, m_MaterialsDynamicBuffer.GetInstanceGpuPointer(instanceData.materialID));

			if (curMatId != instanceData.materialID)
			{
				curMatId = instanceData.materialID;

				const auto& srvs = material->GetDescriptors();

				gfx.SetDynamicDescriptors((UINT)CommonRSId::SRVTable, 0, (UINT)srvs.size(), srvs.data());
			}

			gfx.DrawIndexed(meshData.indexCount, meshData.indexByteOffset / meshData.indexStrideSize, meshData.vertexOffset);
		}
	}

	// indirect rendering
	void Scene::IndirectRender(GraphicsContext& gfx, GraphicsPSO& pso, AlphaMode alphaMode)
	{
		gfx.TransitionResource(m_CommandsBuffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

		gfx.SetConstants((UINT)CommonIndirectRSId::CBConstants, 0, 1, 2, 3);

		gfx.SetShaderResourceView((UINT)CommonIndirectRSId::MatrixTable, m_MatricesDynamicBuffer.GetGpuPointer());
		gfx.SetBufferSRV((UINT)CommonIndirectRSId::MaterialTable, m_MaterialsBuffer);
		gfx.SetBufferSRV((UINT)CommonIndirectRSId::MeshTable, m_MeshesBuffer);
		gfx.SetBufferSRV((UINT)CommonIndirectRSId::MeshInstanceTable, m_MeshInstancesBuffer);
		
		gfx.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_TextureDescriptorHeap.GetHeapPointer());
		gfx.SetDescriptorTable((UINT)CommonIndirectRSId::TextureTable, m_TextureDescriptorHeap.GetHandleAtOffset(0).GetGpuHandle());

		uint32_t opaqueCommandsCount = (uint32_t)m_OpaqueInstances.size();
		uint32_t maskCommandsCount = (uint32_t)m_MaskInstancs.size();
		uint32_t transparentCommandsCount = (uint32_t)m_TransparentInstances.size();
		uint32_t opaqueCommandsOffset = 0;
		uint32_t maskCommandsOffset = opaqueCommandsOffset + opaqueCommandsCount;
		uint32_t transparentOffset = maskCommandsOffset + maskCommandsCount;
		if (alphaMode == AlphaMode::UNKNOWN)
		{
			// 暂时不用 CounterBuffer,不进行Culling	-2020-4-12
			gfx.SetPipelineState(m_OpaqueIndirectPSO);
			gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, opaqueCommandsOffset * sizeof(IndirectCommand), opaqueCommandsCount);

			gfx.SetPipelineState(m_MaskIndirectPSO);
			gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, maskCommandsOffset * sizeof(IndirectCommand), maskCommandsCount);

			gfx.SetPipelineState(m_TransparentIndirectPSO);
			gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, transparentOffset * sizeof(IndirectCommand), transparentCommandsCount);
		}
		else if (alphaMode == AlphaMode::kOPAQUE)
		{
			gfx.SetPipelineState(pso);
			gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, opaqueCommandsOffset * sizeof(IndirectCommand), opaqueCommandsCount);
		}
		else if (alphaMode == AlphaMode::kMASK)
		{
			gfx.SetPipelineState(pso);
			gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, maskCommandsOffset * sizeof(IndirectCommand), maskCommandsCount);
		}
		else if (alphaMode == AlphaMode::kBLEND)
		{
			gfx.SetPipelineState(pso);
			gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, transparentOffset * sizeof(IndirectCommand), transparentCommandsCount);
		}		

	}

	// deferred rendering
	void Scene::PrepareGBuffer(GraphicsContext& gfx, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor)
	{
		auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
		auto& tangentFrame = m_BindlessDeferred.m_TangentFrame;
		auto& uvTarget = m_BindlessDeferred.m_UVTarget;
		auto& uvGradientsTarget = m_BindlessDeferred.m_UVGradientsTarget;
		auto& materialIDTarget = m_BindlessDeferred.m_MaterialIDTarget;

		gfx.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
		gfx.TransitionResource(tangentFrame, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfx.TransitionResource(uvTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfx.TransitionResource(uvGradientsTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfx.TransitionResource(materialIDTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, true);

		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[] =
		{
			tangentFrame.GetRTV(), uvTarget.GetRTV(),
			uvGradientsTarget.GetRTV(), materialIDTarget.GetRTV()
		};

		gfx.ClearDepth(depthBuffer);

		gfx.ClearColor(tangentFrame);
		gfx.ClearColor(uvTarget);
		gfx.ClearColor(uvGradientsTarget);
		gfx.ClearColor(materialIDTarget);

		gfx.SetRenderTargets(_countof(rtvHandles), rtvHandles, depthBuffer.GetDSV());
		gfx.SetViewportAndScissor(viewport, scissor);
	}

	void Scene::RenderToGBuffer(GraphicsContext& gfx, GraphicsPSO& pso)
	{
		gfx.TransitionResource(m_CommandsBuffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

		gfx.SetConstants((UINT)GBufferRSId::CBConstants, 0, 1, 2, 3);

		gfx.SetShaderResourceView((UINT)GBufferRSId::MatrixTable, m_MatricesDynamicBuffer.GetGpuPointer());
		gfx.SetBufferSRV((UINT)GBufferRSId::MaterialTable, m_MaterialsBuffer);
		gfx.SetBufferSRV((UINT)GBufferRSId::MeshTable, m_MeshesBuffer);
		gfx.SetBufferSRV((UINT)GBufferRSId::MeshInstanceTable, m_MeshInstancesBuffer);

		uint32_t opaqueCommandsCount = (uint32_t)m_OpaqueInstances.size();
		uint32_t maskCommandsCount = (uint32_t)m_MaskInstancs.size();
		uint32_t transparentCommandsCount = (uint32_t)m_TransparentInstances.size();
		uint32_t opaqueCommandsOffset = 0;
		uint32_t maskCommandsOffset = opaqueCommandsOffset + opaqueCommandsCount;
		uint32_t transparentOffset = maskCommandsOffset + maskCommandsCount;
		
		// only draw opaque objects
		gfx.SetPipelineState(pso);
		gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, opaqueCommandsOffset * sizeof(IndirectCommand), opaqueCommandsCount);
		
	}

	void Scene::DeferredRender(ComputeContext& computeContext, ComputePSO& pso)
	{
		auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
		auto& tangentFrame = m_BindlessDeferred.m_TangentFrame;
		auto& uvTarget = m_BindlessDeferred.m_UVTarget;
		auto& uvGradientsTarget = m_BindlessDeferred.m_UVGradientsTarget;
		auto& materialIDTarget = m_BindlessDeferred.m_MaterialIDTarget;

		auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
		uint32_t width = colorBuffer.GetWidth(), height = colorBuffer.GetHeight();

		computeContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(tangentFrame, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(uvTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(uvGradientsTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(materialIDTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		computeContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		computeContext.SetPipelineState(pso);

		CSConstants csConstants;
		csConstants._ViewportWidth = width;
		csConstants._ViewportHeight = height;
		csConstants._NearClip = m_Camera->GetNearClip();
		csConstants._FarClip = m_Camera->GetFarClip();
		const auto& camPos = m_Camera->GetPosition();
		csConstants._CamPos = XMFLOAT4(camPos.GetX(), camPos.GetY(), camPos.GetZ(), 1.0f);
		csConstants._ViewProjMat = Cast(m_Camera->GetViewProjMatrix());
		csConstants._InvViewProjMat = MMATH::inverse(csConstants._ViewProjMat);
		csConstants._ViewMat = Cast(m_Camera->GetViewMatrix());
		csConstants._ProjMat = Cast(m_Camera->GetProjMatrix());
		computeContext.SetDynamicConstantBufferView((UINT)DeferredCSRSId::CBConstants, sizeof(CSConstants), &csConstants);

		D3D12_CPU_DESCRIPTOR_HANDLE gbufferHandles[] =
		{
			materialIDTarget.GetSRV(),
			tangentFrame.GetSRV(), uvTarget.GetSRV(),
			uvGradientsTarget.GetSRV(), depthBuffer.GetDepthSRV(),
		};
		computeContext.SetDynamicDescriptors((UINT)DeferredCSRSId::GBuffer, 0, _countof(gbufferHandles), gbufferHandles);
		// 单独设置MaterialIDTarget，放在GBuffer后面总是提示出错	-2020-4-21
		// computeContext.SetDynamicDescriptor((UINT)DeferredCSRSId::MaterialIDTarget, 0, materialIDTarget.GetSRV());
		computeContext.SetBufferSRV((UINT)DeferredCSRSId::MaterialTable, m_MaterialsBuffer);

		computeContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_TextureDescriptorHeap.GetHeapPointer());
		computeContext.SetDescriptorTable((UINT)DeferredCSRSId::TextureTable, m_TextureDescriptorHeap.GetHandleAtOffset(0).GetGpuHandle());

		// decal
		computeContext.SetBufferSRV((UINT)DeferredCSRSId::DecalTable, m_BindlessDeferred.m_DecalBuffer);
		computeContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_BindlessDeferred.m_DecalTextureHeap.GetHeapPointer());
		computeContext.SetDescriptorTable((UINT)DeferredCSRSId::DecalTextures, m_BindlessDeferred.m_DecalTextureHeap.GetHandleAtOffset(0).GetGpuHandle());

		computeContext.SetDynamicDescriptor((UINT)DeferredCSRSId::OutputTarget, 0, colorBuffer.GetUAV());

		uint32_t groupCountX = Math::DivideByMultiple(width, m_BindlessDeferred.s_DeferredTileSize);
		uint32_t groupCountY = Math::DivideByMultiple(height, m_BindlessDeferred.s_DeferredTileSize);
		computeContext.Dispatch(groupCountX, groupCountY);

		computeContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
	}

	void Scene::Clean()
	{
		m_BindlessDeferred.Clean();

		m_MaterialsDynamicBuffer.Destroy();
		m_MatricesDynamicBuffer.Destroy();

		m_MaterialsBuffer.Destroy();
		m_MeshesBuffer.Destroy();
		m_MeshInstancesBuffer.Destroy();
		m_LightsBuffer.Destroy();
		m_CommandsBuffer.Destroy();

		m_VertexBuffer->Destroy();
		m_IndexBuffer->Destroy();
		m_InstanceBuffer->Destroy();

		m_CommandSignature.Destroy();
	}

	void Scene::InitResources(ID3D12Device* pDevice)
	{
		// material dynamic buffer
		uint32_t numMaterials = (uint32_t)m_Materials.size();
		m_MaterialsDynamicBuffer.Create(pDevice, L"MaterialsDynamicBuffer", numMaterials, sizeof(MaterialData), true);
		for (uint32_t matId = 0; matId < numMaterials; ++matId)
		{
			const auto& curMat = m_Materials[matId];
			const auto& matData = curMat->GetMaterialData();
			m_MaterialsDynamicBuffer.CopyToGpu((void*)&matData, sizeof(MaterialData), matId);
		}

		// matrices dynamic buffer
		uint32_t numGlobalMatrices = (uint32_t)m_GlobalMatrices.size();
		m_MatricesDynamicBuffer.Create(pDevice, L"MatricesDynamicBuffer", numGlobalMatrices, sizeof(GlobalMatrix));
		GlobalMatrix gmat;
		for (uint32_t i = 0; i < numGlobalMatrices; ++i)
		{
			gmat.worldMat = MMATH::transpose( m_GlobalMatrices[i]);
			gmat.invWorldMat = MMATH::inverse(gmat.worldMat);
			m_MatricesDynamicBuffer.CopyToGpu((void*)&gmat, sizeof(GlobalMatrix), i);
		}

		// material buffer & texture descriptor heap
		// copy descriptors
		uint32_t numTexturesPerMat = (uint32_t)TextureType::Count;
		m_TextureDescriptorHeap.Create(pDevice, L"TextureDescriptorHeap");
		m_TextureDescriptorHeap.Alloc(numMaterials * numTexturesPerMat);
		UINT srvDescriptorStepSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		std::vector<MaterialData> materialData(numMaterials);
		for (uint32_t matId = 0; matId < numMaterials; ++matId)
		{
			// material data
			const auto& curMat = m_Materials[matId];
			const auto& curData = curMat->GetMaterialData();
			materialData[matId] = curData;

			// material textures
			auto dstHandle = m_TextureDescriptorHeap.GetHandleAtOffset(matId * numTexturesPerMat);
			const auto& srcTexHandles = curMat->GetDescriptors();
			for (uint32_t texIdx = 0; texIdx < numTexturesPerMat; ++texIdx)
			{
				pDevice->CopyDescriptorsSimple(1, dstHandle.GetCpuHandle(), srcTexHandles[texIdx], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				dstHandle += srvDescriptorStepSize;
			}			
		}
		m_MaterialsBuffer.Create(pDevice, L"MaterialsBuffer", numMaterials, sizeof(MaterialData), materialData.data());

		// mesh buffer
		uint32_t numMeshes = (uint32_t)m_MeshDescs.size();
		m_MeshesBuffer.Create(pDevice, L"MeshesBuffer", numMeshes, sizeof(MeshDesc), m_MeshDescs.data());

		// mesh instance buffer
		uint32_t numInstances = (uint32_t)m_MeshInstanceData.size();
		m_MeshInstancesBuffer.Create(pDevice, L"MeshInstancesBuffer", numInstances, sizeof(MeshInstanceData), m_MeshInstanceData.data());

	}

	void Scene::SortMeshInstances()
	{
		// instance data sort by AlphaMode
		std::stable_sort(m_MeshInstanceData.begin(), m_MeshInstanceData.end(), [this](const MeshInstanceData& lhs, const MeshInstanceData& rhs) {
			const auto& lmat = m_Materials[lhs.materialID];
			const auto& rmat = m_Materials[rhs.materialID];
			return lmat->eAlphaMode < rmat->eAlphaMode;
			});

		size_t numInstance = m_MeshInstanceData.size();
		m_OpaqueInstances.reserve(numInstance);
		m_MaskInstancs.reserve(numInstance);
		m_TransparentInstances.reserve(numInstance);

		auto curAlphaMode = AlphaMode::UNKNOWN;
		std::vector<uint32_t>* curVec = nullptr;
		for (uint32_t instanceIdx = 0, maxIdx = (uint32_t)numInstance; instanceIdx < maxIdx; ++instanceIdx)
		{
			const auto& curInstance = m_MeshInstanceData[instanceIdx];
			const auto& curMat = m_Materials[curInstance.materialID];
			if (curMat->eAlphaMode == AlphaMode::UNKNOWN)
				curVec = &m_OpaqueInstances;
			else if (curMat->eAlphaMode != curAlphaMode)
			{
				switch (curMat->eAlphaMode)
				{
				default:
				case AlphaMode::kOPAQUE:
					curVec = &m_OpaqueInstances;
					break;
				case AlphaMode::kMASK:
					curVec = &m_MaskInstancs;
					break;
				case AlphaMode::kBLEND:
					curVec = &m_TransparentInstances;
					break;
				}
			}
			curVec->push_back(instanceIdx);
		}
	}

	std::shared_ptr<StructuredBuffer> Scene::CreateInstanceBuffer(ID3D12Device *pDevice)
	{
		uint32_t drawCount = (uint32_t)m_MeshInstanceData.size();
		std::vector<uint16_t> drawIds;
		drawIds.resize(drawCount);
		for (uint16_t i = 0; i < drawCount; ++i)
			drawIds[i] = i;

		std::shared_ptr<StructuredBuffer> pInstanceBuffer = std::make_shared<StructuredBuffer>();
		std::wstring ibName = std::wstring(m_Name.begin(), m_Name.end());
		pInstanceBuffer->Create(pDevice, ibName + L"_InstanceBuffer", drawCount, sizeof(uint16_t), drawIds.data());

		// layout
		VertexBufferLayout::SharedPtr pLayout = VertexBufferLayout::Create();
		pLayout->AddElement("DRAWID", DXGI_FORMAT_R16_UINT, 0, 0, 1);
		pLayout->SetInputClass(InputType::PerInstanceData, 1);
		m_InstanceLayout = pLayout;

		m_InstanceBuffer = pInstanceBuffer;

		return pInstanceBuffer;
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
				// globalMat = globalMat * m_GlobalMatrices[parentIdx];	// m_GlobalMatrices[i]
				globalMat = m_GlobalMatrices[parentIdx] * globalMat;
				// 注：按理应是 m_GlobalMatrices[parentIdx] * globalMat，但是结果不对，不懂 ？？？ -2020-4-8
				// 解：AssimpImporter-> MFalcor::Matrix4x4 aiCast(const aiMatrix&) 错误，结果自然不对！！！
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

	void Scene::CreateDrawList(ID3D12Device *pDevice)
	{
		uint32_t drawCount = (uint32_t)m_MeshInstanceData.size();
		std::vector<IndirectCommand> drawCommands(drawCount);
	
		for (uint32_t instanceId = 0; instanceId < drawCount; ++instanceId)
		{
			const auto& curInstance = m_MeshInstanceData[instanceId];
			const auto& curMesh = m_MeshDescs[curInstance.meshID];
			const auto& curMaterial = m_Materials[curInstance.materialID];

			IndirectCommand newCommand;
			auto& drawArgs = newCommand.drawArgs;
			drawArgs.IndexCountPerInstance = curMesh.indexCount;
			drawArgs.InstanceCount = 1;
			drawArgs.StartIndexLocation = curMesh.indexByteOffset / curMesh.indexStrideSize;
			drawArgs.BaseVertexLocation = curMesh.vertexOffset;
			drawArgs.StartInstanceLocation = instanceId;

			drawCommands[instanceId] = std::move(newCommand);
		}

		m_CommandsBuffer.Create(pDevice, L"CommandBuffer", drawCount, sizeof(IndirectCommand), drawCommands.data());
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
		Math::Vector3 meye{ center.x, center.y, center.z - radius / 10 };
		Math::Vector3 mtarget = meye + Math::Vector3(0, 0, 1);
		Math::Vector3 up{ 0, 1, 0 };
		m_Camera->SetEyeAtUp(meye, mtarget, up);

		if (bResetDepthRange)
		{
			float nearZ = std::max(0.1f, radius / 750.0f);
			float farZ = std::min(10000.f, radius * 50);
			m_Camera->SetZRange(nearZ, farZ);
		}
	}

	void Scene::SaveNewViewport()
	{
		auto cam = m_Camera;
		auto position = m_Camera->GetPosition();
		auto target = position + m_Camera->GetForwardVec();
		auto up = m_Camera->GetUpVec();

		Viewport newViewport;
		newViewport.position = Cast(position);
		newViewport.target = Cast(target);
		newViewport.up = Cast(up);
		m_Viewports.emplace_back(std::move(newViewport));
	}

	void Scene::RemoveViewport()
	{
		if (m_CurViewport == 0)
		{
			Utility::Print("Cannot remove default viewport");
			return;
		}

		m_Viewports.erase(m_Viewports.begin() + m_CurViewport);
		m_CurViewport = 0;
	}

	void Scene::GotoViewport(uint32_t index)
	{
		auto camera = m_Camera;
		const auto& dstViewport = m_Viewports[index];
		camera->SetEyeAtUp(Cast(dstViewport.position), Cast(dstViewport.target), Cast(dstViewport.up));
		m_CurViewport = index;
	}

	Material::SharedPtr Scene::GetMaterialByName(const std::string& name) const
	{
		for (const auto& mat : m_Materials)
		{
			if (mat->GetName() == name) return mat;
		}
		return nullptr;
	}

	void Scene::SetCommonLights(GraphicsContext& gfx, UINT rootIndex)
	{
		gfx.SetDynamicConstantBufferView(rootIndex, sizeof(m_CommonLights), &m_CommonLights);
	}
}
