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
#include "IndirectDepthVS.h"
#include "IndirectDepthCutoutPS.h"

// deferred shading
#include "IndirectGBufferVS.h"
#include "IndirectGBufferPS.h"
#include "DeferredCS.h"

// culling
#include "FrustumCullingCS.h"
#include "GenerateHiZMipsCS.h"
#include "OcclusionCullArgsCS.h"
#include "OcclusionCullingCS.h"

// voxelization
#include "VoxelizationVS.h"
#include "VoxelizationGS.h"
#include "VoxelizationPS.h"

namespace MFalcor
{
	static const uint32_t s_HiZMips = 9;

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

	struct alignas(16) DeferredCSConstants
	{
		uint32_t _ViewportWidth, _ViewportHeight;
		float _NearClip, _FarClip;
		XMFLOAT4 _CamPos;
		XMFLOAT4 _CascadeSplits;
		Matrix4x4 _ViewProjMat;
		Matrix4x4 _InvViewProjMat;
		Matrix4x4 _ViewMat;
		Matrix4x4 _ProjMat;
	};

	struct alignas(16) CullingCSConstants
	{
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
				m_CommonIndirectRS.Reset(11, 2);
				m_CommonIndirectRS[0].InitAsConstants(0, 4);
				m_CommonIndirectRS[1].InitAsConstantBuffer(1);
				m_CommonIndirectRS[2].InitAsConstantBuffer(2);
				m_CommonIndirectRS[3].InitAsConstantBuffer(3);
				m_CommonIndirectRS[4].InitAsBufferSRV(0, 1);
				m_CommonIndirectRS[5].InitAsBufferSRV(1, 1);
				m_CommonIndirectRS[6].InitAsBufferSRV(2, 1);
				m_CommonIndirectRS[7].InitAsBufferSRV(3, 1);
				m_CommonIndirectRS[8].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, -1, 1, D3D12_SHADER_VISIBILITY_PIXEL);
				m_CommonIndirectRS[9].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 8, 0, D3D12_SHADER_VISIBILITY_PIXEL);
				m_CommonIndirectRS[10].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 2);
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
				m_DeferredCSRS.Reset(10, 3);
				m_DeferredCSRS[0].InitAsConstantBuffer(0);
				m_DeferredCSRS[1].InitAsConstantBuffer(1);	// CommonLights
				m_DeferredCSRS[2].InitAsConstantBuffer(2);	// CascadedShadowMap constants
				m_DeferredCSRS[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 5);
				// 单独设置 MaterialIDTarget	-目前会有问题 -2020-4-21
				// m_DeferredCSRS[4].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 65, 1);
				m_DeferredCSRS[4].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 1);
				m_DeferredCSRS[5].InitAsBufferSRV(1, 1);
				m_DeferredCSRS[6].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, -1, 1);
				m_DeferredCSRS[7].InitAsBufferSRV(0, 2);	// decal buffer
				m_DeferredCSRS[8].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 10, 2);	// decal textures
				m_DeferredCSRS[9].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
				m_DeferredCSRS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearWrapDesc);
				m_DeferredCSRS.InitStaticSampler(1, Graphics::s_CommonStates.SamplerPointClampDesc);

				SamplerDesc shadowSamplerDesc;
				shadowSamplerDesc.SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
				shadowSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
				m_DeferredCSRS.InitStaticSampler(2, shadowSamplerDesc); // Graphics::s_CommonStates.SamplerShadowDesc

				m_DeferredCSRS.Finalize(pDevice, L"DeferredCSRS");

				// Culling cs
				m_CullingRS.Reset(5, 1);
				m_CullingRS[0].InitAsConstants(0, 4);
				m_CullingRS[1].InitAsConstantBuffer(1);
				m_CullingRS[2].InitAsBufferSRV(0);
				m_CullingRS[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);
				m_CullingRS[4].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 4);
				m_CullingRS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerPointClampDesc);
				m_CullingRS.Finalize(pDevice, L"CullingCSRS");
			}
			// command signatures
			{
				m_CommandSignature.Reset(1);
				m_CommandSignature[0].DrawIndexed();
				m_CommandSignature.Finalize(pDevice);
			}

			auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
			auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
			// PSOs
			{
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
				// opaque depth
				m_DepthIndirectPSO = m_DepthPSO;
				m_DepthIndirectPSO.SetRootSignature(m_CommonIndirectRS);
				m_DepthIndirectPSO.SetVertexShader(IndirectDepthVS, sizeof(IndirectDepthVS));
				m_DepthIndirectPSO.Finalize(pDevice);

				// depth clip
				m_DepthClipIndirectPSO = m_DepthIndirectPSO;
				m_DepthClipIndirectPSO.SetPixelShader(IndirectDepthCutoutPS, sizeof(IndirectDepthCutoutPS));
				m_DepthClipIndirectPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerShadowTwoSided);
				m_DepthClipIndirectPSO.Finalize(pDevice);

				// opaque indirect model
				m_OpaqueIndirectPSO = m_OpaqueModelPSO;
				m_OpaqueIndirectPSO.SetRootSignature(m_CommonIndirectRS);
				m_OpaqueIndirectPSO.SetVertexShader(CommonIndirectVS, sizeof(CommonIndirectVS));
				m_OpaqueIndirectPSO.SetPixelShader(CommonIndirectPS, sizeof(CommonIndirectPS));
				m_OpaqueIndirectPSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateTestEqual);
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
			}

			// shadows
			{
				m_CascadedShadowMap.Init(pDevice, -Math::Vector3(m_CommonLights.sunDirection), *m_Camera);

				// opaque
				m_OpaqueShadowPSO = m_DepthPSO;
				m_OpaqueShadowPSO.SetRootSignature(m_CommonIndirectRS);
				m_OpaqueShadowPSO.SetVertexShader(IndirectDepthVS, sizeof(IndirectDepthVS));
				m_OpaqueShadowPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerShadow);
				m_OpaqueShadowPSO.SetRenderTargetFormats(0, nullptr, m_CascadedShadowMap.m_LightShadowTempBuffer.GetFormat());
				m_OpaqueShadowPSO.Finalize(pDevice);

				// mask
				m_MaskShadowPSO = m_OpaqueShadowPSO;
				m_MaskShadowPSO.SetPixelShader(IndirectDepthCutoutPS, sizeof(IndirectDepthCutoutPS));
				m_MaskShadowPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerShadowTwoSided);
				m_MaskShadowPSO.Finalize(pDevice);
			}

			// bindless deferred
			{
				m_BindlessDeferred.Init(pDevice, this);

				// opaque models
				m_OpaqueGBufferPSO = m_OpaqueIndirectPSO;
				m_OpaqueGBufferPSO.SetRootSignature(m_GBufferRS);
				m_OpaqueGBufferPSO.SetVertexShader(IndirectGBufferVS, sizeof(IndirectGBufferVS));
				m_OpaqueGBufferPSO.SetPixelShader(IndirectGBufferPS, sizeof(IndirectGBufferPS));
				m_OpaqueGBufferPSO.SetRenderTargetFormats(_countof(rtFormats), rtFormats, depthBuffer.GetFormat());
				m_OpaqueGBufferPSO.Finalize(pDevice);

				// mask models
				m_MaskGBufferPSO = m_OpaqueGBufferPSO;
				m_MaskGBufferPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerShadowTwoSided);
				m_MaskGBufferPSO.Finalize(pDevice);

				// deferred cs
				m_DeferredCSPSO.SetRootSignature(m_DeferredCSRS);
				m_DeferredCSPSO.SetComputeShader(DeferredCS, sizeof(DeferredCS));
				m_DeferredCSPSO.Finalize(pDevice);
			}

			// culling
			{
				// frustum culling
				m_FrustumCSPSO.SetRootSignature(m_CullingRS);
				m_FrustumCSPSO.SetComputeShader(FrustumCullingCS, sizeof(FrustumCullingCS));
				m_FrustumCSPSO.Finalize(pDevice);

				// occlusion culling
				DXGI_FORMAT depthFormat = depthBuffer.GetFormat();
				m_HiZBuffer.Create(pDevice, L"HiZBuffer", depthBuffer.GetWidth(), depthBuffer.GetHeight(), 0, DXGI_FORMAT_R32_FLOAT);	// 0 s_HiZMips

				// generate Hi-Z mips
				m_GenerateHiZMipsPSO.SetRootSignature(m_CullingRS);
				m_GenerateHiZMipsPSO.SetComputeShader(GenerateHiZMipsCS, sizeof(GenerateHiZMipsCS));
				m_GenerateHiZMipsPSO.Finalize(pDevice);

				// indirect dispatch args
				m_OcclusionCullArgsPSO.SetRootSignature(m_CullingRS);
				m_OcclusionCullArgsPSO.SetComputeShader(OcclusionCullArgsCS, sizeof(OcclusionCullArgsCS));
				m_OcclusionCullArgsPSO.Finalize(pDevice);

				m_OcclusionCullingPSO.SetRootSignature(m_CullingRS);
				m_OcclusionCullingPSO.SetComputeShader(OcclusionCullingCS, sizeof(OcclusionCullingCS));
				m_OcclusionCullingPSO.Finalize(pDevice);
			}

			// voxelization
			{
				m_VoxelizationPSO = m_DepthIndirectPSO;
				m_VoxelizationPSO.SetVertexShader(VoxelizationVS, sizeof(VoxelizationVS));
				m_VoxelizationPSO.SetGeometryShader(VoxelizationGS, sizeof(VoxelizationGS));
				m_VoxelizationPSO.SetPixelShader(VoxelizationPS, sizeof(VoxelizationPS));
				m_VoxelizationPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerTwoSided);
				m_VoxelizationPSO.SetBlendState(Graphics::s_CommonStates.BlendNoColorWrite);
				m_VoxelizationPSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateDisabled);	// zwrite off, ztest off
				m_VoxelizationPSO.SetRenderTargetFormats(0, nullptr, depthBuffer.GetFormat());
				m_VoxelizationPSO.Finalize(pDevice);
			}
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

		UpdateBounds(pDevice);

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

		// m_CommonLights.sunOrientation += 0.002f * Math::Pi * deltaTime;
		UpdateSunLight();

		m_CascadedShadowMap.PrepareCascades(-Math::Vector3(m_CommonLights.sunDirection), *m_Camera);

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
		cbPerCamera._ViewProjMat = viewProjMat;	// viewProjMat 不用转置，存储时已经转置
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
		gfx.TransitionResource(m_FrustumCulledBuffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		gfx.TransitionResource(m_FrustumCulledBuffer.GetCounterBuffer(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		
		gfx.TransitionResource(m_OcclusionCulledBuffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		gfx.TransitionResource(m_OcclusionCulledBuffer.GetCounterBuffer(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

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
			gfx.SetPipelineState(m_OpaqueIndirectPSO);
			
			if (m_EnableFrustumCulling)
			{
				if (m_EnableOcclusionCulling)
				{
					gfx.ExecuteIndirect(m_CommandSignature, m_OcclusionCulledBuffer, 0, opaqueCommandsCount,
						&m_OcclusionCulledBuffer.GetCounterBuffer());
				}
				else
				{
					gfx.ExecuteIndirect(m_CommandSignature, m_FrustumCulledBuffer, 0, opaqueCommandsCount,
						&m_FrustumCulledBuffer.GetCounterBuffer());
				}
			}
			else
			{
				gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, opaqueCommandsOffset * sizeof(IndirectCommand), opaqueCommandsCount);
			}

			gfx.SetPipelineState(m_MaskIndirectPSO);
			gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, maskCommandsOffset * sizeof(IndirectCommand), maskCommandsCount);

			gfx.SetPipelineState(m_TransparentIndirectPSO);
			gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, transparentOffset * sizeof(IndirectCommand), transparentCommandsCount);
		}
		else if (alphaMode == AlphaMode::kOPAQUE)
		{
			gfx.SetPipelineState(pso);
			// gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, opaqueCommandsOffset * sizeof(IndirectCommand), opaqueCommandsCount);
			gfx.ExecuteIndirect(m_CommandSignature, m_FrustumCulledBuffer, 0, opaqueCommandsCount,
				&m_FrustumCulledBuffer.GetCounterBuffer());
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

	void Scene::RenderToGBuffer(GraphicsContext& gfx, GraphicsPSO& pso, AlphaMode alphaMode)
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
		
		if (alphaMode == AlphaMode::UNKNOWN)
		{
			gfx.SetPipelineState(m_OpaqueGBufferPSO);
			gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, opaqueCommandsOffset * sizeof(IndirectCommand), opaqueCommandsCount);

			gfx.SetPipelineState(m_MaskGBufferPSO);
			gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, maskCommandsOffset * sizeof(IndirectCommand), maskCommandsCount);
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
	}

	void Scene::DeferredRender(ComputeContext& computeContext, ComputePSO& pso)
	{
		auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
		auto& tangentFrame = m_BindlessDeferred.m_TangentFrame;
		auto& uvTarget = m_BindlessDeferred.m_UVTarget;
		auto& uvGradientsTarget = m_BindlessDeferred.m_UVGradientsTarget;
		auto& materialIDTarget = m_BindlessDeferred.m_MaterialIDTarget;
		auto& shadowMap = m_CascadedShadowMap.m_LightShadowArray;

		auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
		uint32_t width = colorBuffer.GetWidth(), height = colorBuffer.GetHeight();

		computeContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(tangentFrame, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(uvTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(uvGradientsTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(materialIDTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(shadowMap, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		computeContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		computeContext.SetPipelineState(pso);

		DeferredCSConstants csConstants;
		csConstants._ViewportWidth = width;
		csConstants._ViewportHeight = height;
		csConstants._NearClip = m_Camera->GetNearClip();
		csConstants._FarClip = m_Camera->GetFarClip();
		const auto& camPos = m_Camera->GetPosition();
		csConstants._CamPos = XMFLOAT4(camPos.GetX(), camPos.GetY(), camPos.GetZ(), 1.0f);
		const auto& cascadeSplits = m_CascadedShadowMap.m_CascadeSplits;
		csConstants._CascadeSplits = XMFLOAT4(cascadeSplits.GetX(), cascadeSplits.GetY(), cascadeSplits.GetZ(), cascadeSplits.GetW());
		csConstants._ViewProjMat = Cast(m_Camera->GetViewProjMatrix());
		csConstants._InvViewProjMat = MMATH::inverse(csConstants._ViewProjMat);
		csConstants._ViewMat = Cast(m_Camera->GetViewMatrix());
		csConstants._ProjMat = Cast(m_Camera->GetProjMatrix());
		computeContext.SetDynamicConstantBufferView((UINT)DeferredCSRSId::CBConstants, sizeof(DeferredCSConstants), &csConstants);

		computeContext.SetDynamicConstantBufferView((UINT)DeferredCSRSId::CommonLights, sizeof(CommonLightSettings), &m_CommonLights);

		const uint32_t numCascades = CascadedShadowMap::s_NumCascades;
		Math::Matrix4 cascadedShadowMats[numCascades];
		for (uint32_t i = 0; i < numCascades; ++i)
		{
			cascadedShadowMats[i] = Math::Transpose(m_CascadedShadowMap.m_ViewProjMat[i]);
		}
		computeContext.SetDynamicConstantBufferView((UINT)DeferredCSRSId::CascadedSMConstants, sizeof(cascadedShadowMats), cascadedShadowMats);

		D3D12_CPU_DESCRIPTOR_HANDLE gbufferHandles[] =
		{
			materialIDTarget.GetSRV(),
			tangentFrame.GetSRV(), uvTarget.GetSRV(),
			uvGradientsTarget.GetSRV(), depthBuffer.GetDepthSRV(),
		};
		computeContext.SetDynamicDescriptors((UINT)DeferredCSRSId::GBuffer, 0, _countof(gbufferHandles), gbufferHandles);
		// 单独设置MaterialIDTarget，放在GBuffer后面总是提示出错	-2020-4-21
		// computeContext.SetDynamicDescriptor((UINT)DeferredCSRSId::MaterialIDTarget, 0, materialIDTarget.GetSRV());
		computeContext.SetDynamicDescriptor((UINT)DeferredCSRSId::ShadowMap, 0, shadowMap.GetSRV());
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

	void Scene::RenderSunShadows(GraphicsContext& gfx)
	{
		gfx.TransitionResource(m_CommandsBuffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

		gfx.SetRootSignature(m_CommonIndirectRS);		

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
		
		CBPerCamera cbPerCamera;
		uint32_t numCascades = m_CascadedShadowMap.s_NumCascades;
		// 目前只渲染一级阴影	-2020-4-29
		for (uint32_t i = 0; i < 1; ++i)
		{
			// gfx.TransitionResource(m_CascadedShadowMap.m_LightShadowTempBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
			// clear depth
			m_CascadedShadowMap.m_LightShadowTempBuffer.BeginRendering(gfx);

			cbPerCamera._ViewProjMat = Cast(m_CascadedShadowMap.m_ViewProjMat[i]);
			cbPerCamera._CamPos = Cast(m_CascadedShadowMap.m_CamPos[i]);
			gfx.SetDynamicConstantBufferView((UINT)CommonIndirectRSId::CBPerCamera, sizeof(CBPerCamera), &cbPerCamera);

			// opaque shadow
			gfx.SetPipelineState(m_OpaqueShadowPSO);
			gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, opaqueCommandsOffset * sizeof(IndirectCommand), opaqueCommandsCount);

			// mask shadow
			gfx.SetPipelineState(m_MaskShadowPSO);
			gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, maskCommandsOffset * sizeof(IndirectCommand), maskCommandsCount);

			gfx.TransitionResource(m_CascadedShadowMap.m_LightShadowTempBuffer, D3D12_RESOURCE_STATE_GENERIC_READ);
			gfx.TransitionResource(m_CascadedShadowMap.m_LightShadowArray, D3D12_RESOURCE_STATE_COPY_DEST);
			gfx.CopySubresource(m_CascadedShadowMap.m_LightShadowArray, i, m_CascadedShadowMap.m_LightShadowTempBuffer, 0);

		}

		// m_CascadedShadowMap.m_LightShadowTempBuffer.EndRendering(gfx);
	}

	void Scene::FrustumCulling(ComputeContext& computeContext, const Matrix4x4& viewMat, const Matrix4x4& projMat)
	{
		computeContext.TransitionResource(m_CommandsBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		// debug
		if (m_EnableDebugCulling)
		{
			computeContext.TransitionResource(m_DCullValues, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			computeContext.TransitionResource(m_DSummedIndex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}

		computeContext.ResetCounter(m_FrustumCulledBuffer);

		computeContext.SetRootSignature(m_CullingRS);
		computeContext.SetPipelineState(m_FrustumCSPSO);

		uint32_t opaqueCommandsCount = (uint32_t)m_OpaqueInstances.size();
		uint32_t maskCommandsCount = (uint32_t)m_MaskInstancs.size();
		uint32_t transparentCommandsCount = (uint32_t)m_TransparentInstances.size();
		uint32_t opaqueCommandsOffset = 0;
		uint32_t maskCommandsOffset = opaqueCommandsOffset + opaqueCommandsCount;
		uint32_t transparentOffset = maskCommandsOffset + maskCommandsCount;
		uint32_t instanceCount = opaqueCommandsCount + maskCommandsCount + transparentCommandsCount;
		uint32_t gridDimensions = Math::DivideByMultiple(instanceCount, 1024);
		computeContext.SetConstants((UINT)IndirectCullingCSRSId::CBConstants, opaqueCommandsOffset, opaqueCommandsCount, instanceCount, gridDimensions);

		CullingCSConstants csConstants;
		csConstants._ViewProjMat = viewMat * projMat;	
			// 注：MMath::Cast 其实是将Math::Matrix4 转置(0000 | 1111 | 2222 | 3333)->(0123 | 0123 | ...)，
			// 但是因为Math::Matrix4 将0000视作一行，MMath::Matrix4x4 将0123视作一列，所以其实矩阵没变，
			// 所以MMath 矩阵乘法 类似 (V^T * P^T) = (P * V)^T
		csConstants._InvViewProjMat = MMATH::inverse(csConstants._ViewProjMat);
		csConstants._ViewMat = viewMat;
		csConstants._ProjMat = projMat;
		computeContext.SetDynamicConstantBufferView((UINT)IndirectCullingCSRSId::CBCamera, sizeof(CullingCSConstants), &csConstants);

		computeContext.SetShaderResourceView((UINT)IndirectCullingCSRSId::WorldBounds, m_BoundsDynamicBuffer.GetGpuPointer());

		D3D12_CPU_DESCRIPTOR_HANDLE srvs[] =
		{
			m_CommandsBuffer.GetSRV()
		};
		computeContext.SetDynamicDescriptors((UINT)IndirectCullingCSRSId::ShaderResources, 0, _countof(srvs), srvs);

		D3D12_CPU_DESCRIPTOR_HANDLE uavs[] =
		{
			m_FrustumCulledBuffer.GetUAV(),
			m_DCullValues.GetUAV(),
			m_DSummedIndex.GetUAV(),
		};
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> uavVec;
		uavVec.push_back(m_FrustumCulledBuffer.GetUAV());
		if (m_EnableDebugCulling)
		{
			uavVec.push_back(m_DCullValues.GetUAV());
			uavVec.push_back(m_DSummedIndex.GetUAV());
		}
		// computeContext.SetDynamicDescriptors((UINT)IndirectCullingCSRSId::Output, 0, _countof(uavs), uavs);
		computeContext.SetDynamicDescriptors((UINT)IndirectCullingCSRSId::Output, 0, (uint32_t)uavVec.size(), uavVec.data());

		computeContext.Dispatch(gridDimensions, 1, 1);

		if (m_EnableDebugCulling)
		{
			computeContext.TransitionResource(m_DCullValues, D3D12_RESOURCE_STATE_GENERIC_READ);
			computeContext.TransitionResource(m_DSummedIndex, D3D12_RESOURCE_STATE_GENERIC_READ);
		}
		computeContext.InsertUAVBarrier(m_FrustumCulledBuffer, true);

	}

	void Scene::UpdateHiZBuffer(ComputeContext& computeContext, Graphics& gfxCore)
	{
		auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
		uint32_t width = depthBuffer.GetWidth(), height = depthBuffer.GetHeight();

		computeContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
		computeContext.TransitionResource(m_HiZBuffer, D3D12_RESOURCE_STATE_COPY_DEST);
		computeContext.CopySubresource(m_HiZBuffer, 0, depthBuffer, 0);

		computeContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
		
		// generate Hi-Z mips
		computeContext.TransitionResource(m_HiZBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		computeContext.SetRootSignature(m_CullingRS);
		computeContext.SetDynamicDescriptor((UINT)IndirectCullingCSRSId::ShaderResources, 0, m_HiZBuffer.GetSRV());

		uint32_t numMipmaps = m_HiZBuffer.GetMipNums(); // m_HiZBuffer.GetMipNums();	s_HiZMips - 1;
		for (uint32_t topMip = 0; topMip < numMipmaps; )
		{
			uint32_t srcWidth = width >> topMip;
			uint32_t srcHieght = height >> topMip;
			uint32_t dstWidth = srcWidth >> 1;
			uint32_t dstHeight = srcHieght >> 1;

			// determine if the first downsample is more than 2: 1. This happens whenever the source
			// width or height is odd.
			uint32_t NonPowerOfTwo = (srcWidth & 1) | (srcHieght & 1) << 1;
			computeContext.SetPipelineState(m_GenerateHiZMipsPSO);

			// we can downsample up to 4 times, but if the ratio between levels is not exactly 2:1, we have to
			// shift out blend weights, which gets complicated or expensive. Maybe we can update the code later
			// to compute sample weights for each successive downsample. We use _BitScanForward to count number
			// of zeros in the low bits. Zeros indicate we can divide by 2 without truncating.
			uint32_t AdditionalMips;
			_BitScanForward((unsigned long*)&AdditionalMips,
				(dstWidth == 1 ? dstHeight : dstWidth) | (dstHeight == 1 ? dstWidth : dstHeight));
			uint32_t numMips = 1 + (AdditionalMips > 3 ? 3 : AdditionalMips);
			if ((topMip + numMips) > numMipmaps)
				numMips = numMipmaps - topMip;

			// these are clamped to 1 after computing additional mips because clamped dimensions should 
			// not limit us from downsampling multiple times. (E.g. 16x1 -> 8x1 -> 4x1 -> 2x1 -> 1x1)
			if (dstWidth == 0)
				dstWidth = 1;
			if (dstHeight == 0)
				dstHeight = 1;

			computeContext.SetConstants((UINT)IndirectCullingCSRSId::CBConstants, topMip, numMips, 1.0f / dstWidth, 1.0f / dstHeight);
			computeContext.SetDynamicDescriptors((UINT)IndirectCullingCSRSId::Output, 0, numMips, m_HiZBuffer.GetMipUAVs() + topMip + 1);
			computeContext.Dispatch2D(dstWidth, dstHeight);

			computeContext.InsertUAVBarrier(m_HiZBuffer);

			topMip += numMips;
		}

		computeContext.TransitionResource(m_HiZBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	}

	void Scene::OcclusionCulling(ComputeContext& computeContext, const Matrix4x4& viewMat, const Matrix4x4& projMat)
	{
		computeContext.SetRootSignature(m_CullingRS);

		// indirect dispatch args
		{
			computeContext.TransitionResource(m_FrustumCulledBuffer, D3D12_RESOURCE_STATE_GENERIC_READ);
			computeContext.TransitionResource(m_OcclusionCullArgs, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			computeContext.SetPipelineState(m_OcclusionCullArgsPSO);
			computeContext.SetDynamicDescriptor((UINT)IndirectCullingCSRSId::ShaderResources, 0, m_FrustumCulledBuffer.GetCounterSRV(computeContext));
			computeContext.SetDynamicDescriptor((UINT)IndirectCullingCSRSId::Output, 0, m_OcclusionCullArgs.GetUAV());
			computeContext.Dispatch(1, 1, 1);
		}

		// occlusion culling
		{
			uint32_t width = m_HiZBuffer.GetWidth(), height = m_HiZBuffer.GetHeight();

			computeContext.TransitionResource(m_OcclusionCullArgs, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

			computeContext.ResetCounter(m_OcclusionCulledBuffer);

			computeContext.SetPipelineState(m_OcclusionCullingPSO);

			uint32_t numMips = m_HiZBuffer.GetMipNums();	// m_HiZBuffer.GetMipNums() s_HiZMips - 1
			computeContext.SetConstants((UINT)IndirectCullingCSRSId::CBConstants, width, height, numMips);

			CullingCSConstants csConstants;
			csConstants._ViewProjMat = viewMat * projMat;
			csConstants._InvViewProjMat = MMATH::inverse(csConstants._ViewProjMat);
			csConstants._ViewMat = viewMat;
			csConstants._ProjMat = projMat;
			computeContext.SetDynamicConstantBufferView((UINT)IndirectCullingCSRSId::CBCamera, sizeof(CullingCSConstants), &csConstants);

			computeContext.SetShaderResourceView((UINT)IndirectCullingCSRSId::WorldBounds, m_BoundsDynamicBuffer.GetGpuPointer());

			D3D12_CPU_DESCRIPTOR_HANDLE srvs[] =
			{
				m_FrustumCulledBuffer.GetSRV(),
				m_FrustumCulledBuffer.GetCounterSRV(computeContext),
				m_HiZBuffer.GetSRV()
			};
			computeContext.SetDynamicDescriptors((UINT)IndirectCullingCSRSId::ShaderResources, 0, _countof(srvs), srvs);

			D3D12_CPU_DESCRIPTOR_HANDLE uavs[] =
			{
				m_OcclusionCulledBuffer.GetUAV()
			};
			computeContext.SetDynamicDescriptors((UINT)IndirectCullingCSRSId::Output, 0, _countof(uavs), uavs);

			computeContext.DispatchIndirect(m_OcclusionCullArgs);
		}
	}

	void Scene::Clean()
	{
		// shadows
		m_CascadedShadowMap.Clean();
		// bindless deferred
		m_BindlessDeferred.Clean();

		// culling
		m_FrustumCulledBuffer.Destroy();
		m_HiZBuffer.Destroy();
		m_OcclusionCulledBuffer.Destroy();
		m_OcclusionCullArgs.Destroy();
		// debug
		m_DCullValues.Destroy();
		m_DSummedIndex.Destroy();

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
	void Scene::UpdateBounds(ID3D12Device* pDevice)
	{
		std::vector<BoundingBox> instanceBBs;
		uint32_t numInstance = (uint32_t)m_MeshInstanceData.size();
		instanceBBs.reserve(numInstance);

		m_BoundsDynamicBuffer.Create(pDevice, L"WorldBounds", numInstance, sizeof(BoundingBox));

		uint32_t instanceIndex = 0;
		for (const auto& inst : m_MeshInstanceData)
		{
			const BoundingBox& meshBB = m_MeshBBs[inst.meshID];
			const Matrix4x4& transform = m_GlobalMatrices[inst.globalMatrixID];

			const BoundingBox& worldBB = meshBB.Transform(transform);
			instanceBBs.push_back(worldBB);

			//单次拷贝
			//m_BoundsDynamicBuffer.CopyToGpu((void*)&worldBB, sizeof(BoundingBox), instanceIndex);
			//++instanceIndex;
		}
		// 整体拷贝
		m_BoundsDynamicBuffer.CopyToGpu(instanceBBs.data(), sizeof(BoundingBox) * numInstance);

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

		// frustum cull
		m_FrustumCulledBuffer.Create(pDevice, L"FrustumCulledBuffer", drawCount, sizeof(IndirectCommand));

		// debug
		if (m_EnableDebugCulling)
		{
			m_DCullValues.Create(pDevice, L"DebugCullValues", drawCount, 4);
			m_DSummedIndex.Create(pDevice, L"DebugSummedIndex", drawCount, 4);
		}

		// occlusion cull (Hi-Z buffer cull)
		m_OcclusionCulledBuffer.Create(pDevice, L"OcclusionCulledBuffer", drawCount, sizeof(IndirectCommand));
		alignas(16) UINT initDispatchIndirectArgs[] = { 1, 1, 1 };
		m_OcclusionCullArgs.Create(pDevice, L"OcclusionCullArgs", 1, sizeof(D3D12_DISPATCH_ARGUMENTS), initDispatchIndirectArgs);
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
