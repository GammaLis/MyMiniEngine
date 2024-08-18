#include "Scene.h"
#include "AssimpImporter.h"
#include "GameInput.h"
#include "SceneViewer.h"
#include "Graphics.h"
#include "CommandContext.h"

#include "Camera.h"
#include "CameraController.h"
#include "BindlessDeferred.h"
#include "ClusteredLighting.h"
#include "Utilities/ShadowUtility.h"

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
#include "IndirectGBufferCutoutPS.h"
#include "DeferredCS.h"

// culling
#include "FrustumCullingCS.h"
#include "GenerateHiZMipsCS.h"
#include "OcclusionCullArgsCS.h"
#include "OcclusionCullingCS.h"

// visibility buffer
#include "VisibilityBufferVS.h"
#include "VisibilityBufferPS.h"
#include "VisibilityComputeCS.h"

// voxelization
#include "VoxelizationVS.h"
#include "VoxelizationGS.h"
#include "VoxelizationPS.h"

#define USE_VIEW_UNIFORMS 1

namespace MFalcor
{
	static const uint32_t s_HiZMips = 9;
	static const uint32_t c_MaxFrameIndex = 1023;
	static const auto c_BackgroundColor = DirectX::Colors::White;

	namespace DescriptorParams
	{
		enum
		{
			MaterialTextures = 0,
			DecalTextures,
			RenderTextureSRVs,
			RenderTextureUAVs,
			BufferSRVs,
			BufferUAVs,

			Count
		};
	}
	static DescriptorRange s_DescriptorRanges[DescriptorParams::Count];

	namespace RenderTextureSRVs
	{
		enum 
		{
			ColorBuffer = 0,
			DepthBuffer,

			tangentFrame,
			uvTarget,
			uvGradientsTarget,
			materialIDTarget,
			ShadowMap,

			VisibilityBuffer,

			Count
		};
	}
	namespace RenderTextureUAVs
	{
		enum 
		{
			ColorBuffer = 0,

			Count
		};
	}

	namespace BufferSRVs
	{
		enum
		{
			MatrixBuffer = 0,
			MaterialBuffer,
			MeshBuffer,
			MeshInstanceBuffer,
			VertexBuffer,
			IndexBuffer,

			Count
		};
	}

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

	Scene::SharedPtr Scene::Create(ID3D12Device* pDevice, const std::string& filePath, SceneViewer *sceneViewer, const InstanceMatrices& instances)
	{
		auto pAssimpImporter = AssimpImporter::Create(pDevice, filePath, instances);
		auto pScene = pAssimpImporter ? pAssimpImporter->GetScene(pDevice) : nullptr;
		pScene->m_Graphics = sceneViewer->GetGraphics();
		pScene->InitCamera(sceneViewer->GetInput());
		pScene->InitPipelines(pDevice);
		pScene->InitResources(pDevice);
		pScene->InitDescriptors(pDevice);
		return pScene;
	}

	Scene::SharedPtr Scene::Create()
	{
		return std::make_shared<Scene>();
	}

	Scene::Scene() = default;
	Scene::~Scene() = default;

	bool Scene::Init(ID3D12Device* pDevice, const std::string& filePath, SceneViewer *sceneViewer, const InstanceMatrices& instances)
	{
		bool ret = false;
		auto pAssimpImporter = AssimpImporter::Create();
		if (pAssimpImporter && pAssimpImporter->Load(pDevice, filePath, instances))
			ret = pAssimpImporter->AddToScene(pDevice, this);

		if (!ret) return false;

		m_Graphics = sceneViewer->GetGraphics();
		InitCamera(sceneViewer->GetInput());
		InitPipelines(pDevice);
		InitResources(pDevice);
		InitDescriptors(pDevice);
		
		return true;
	}

	void Scene::InitPipelines(ID3D12Device* pDevice)
	{
		// root signatures
		{
			// common RS
			m_CommonRS.Reset((UINT)CommonRSId::Count, 2);
			m_CommonRS[(UINT)CommonRSId::CBConstants].InitAsConstants(0, 4);
			m_CommonRS[(UINT)CommonRSId::CBPerObject].InitAsConstantBuffer(1);	// cbPerObject
			m_CommonRS[(UINT)CommonRSId::CBPerCamera].InitAsConstantBuffer(2);	// cbPerCamera
			m_CommonRS[(UINT)CommonRSId::CBPerMaterial].InitAsConstantBuffer(3);	// cbPerMaterial
			m_CommonRS[(UINT)CommonRSId::CBLights].InitAsConstantBuffer(4);	// cbLights
			m_CommonRS[(UINT)CommonRSId::SRVTable].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 8);
			m_CommonRS[(UINT)CommonRSId::UAVTable].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 2);
			m_CommonRS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearWrapDesc);
			m_CommonRS.InitStaticSampler(1, Graphics::s_CommonStates.SamplerPointClampDesc);
			m_CommonRS.Finalize(pDevice, L"AiCommonRS", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

			// common indirect RS
			m_CommonIndirectRS.Reset((UINT)CommonIndirectRSId::Count, 2);
			m_CommonIndirectRS[(UINT)CommonIndirectRSId::CBConstants].InitAsConstants(0, 10);
			m_CommonIndirectRS[(UINT)CommonIndirectRSId::CBPerCamera].InitAsConstantBuffer(1);
			m_CommonIndirectRS[(UINT)CommonIndirectRSId::CBLights].InitAsConstantBuffer(2);
			m_CommonIndirectRS[(UINT)CommonIndirectRSId::CBMiscs].InitAsConstantBuffer(3);
			m_CommonIndirectRS[(UINT)CommonIndirectRSId::MatrixTable].InitAsBufferSRV(0, 1);
			m_CommonIndirectRS[(UINT)CommonIndirectRSId::MaterialTable].InitAsBufferSRV(1, 1);
			m_CommonIndirectRS[(UINT)CommonIndirectRSId::MeshTable].InitAsBufferSRV(2, 1);
			m_CommonIndirectRS[(UINT)CommonIndirectRSId::MeshInstanceTable].InitAsBufferSRV(3, 1);
			m_CommonIndirectRS[(UINT)CommonIndirectRSId::TextureTable].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, -1, 1, D3D12_SHADER_VISIBILITY_PIXEL);
			m_CommonIndirectRS[(UINT)CommonIndirectRSId::SRVTable].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 8, 0, D3D12_SHADER_VISIBILITY_PIXEL);
			m_CommonIndirectRS[(UINT)CommonIndirectRSId::UAVTable].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 2);
			m_CommonIndirectRS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearWrapDesc);
			m_CommonIndirectRS.InitStaticSampler(1, Graphics::s_CommonStates.SamplerPointClampDesc);
			m_CommonIndirectRS.Finalize(pDevice, L"CommonIndirectRS", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

			// gbuffer
			m_GBufferRS.Reset((UINT)GBufferRSId::Count, 2);
			m_GBufferRS[(UINT)GBufferRSId::CBConstants].InitAsConstants(0, 10);
			m_GBufferRS[(UINT)GBufferRSId::CBPerCamera].InitAsConstantBuffer(1);
			m_GBufferRS[(UINT)GBufferRSId::MatrixTable].InitAsBufferSRV(0, 1);
			m_GBufferRS[(UINT)GBufferRSId::MaterialTable].InitAsBufferSRV(1, 1);
			m_GBufferRS[(UINT)GBufferRSId::MeshTable].InitAsBufferSRV(2, 1);
			m_GBufferRS[(UINT)GBufferRSId::MeshInstanceTable].InitAsBufferSRV(3, 1);
			m_GBufferRS[(UINT)GBufferRSId::TextureTable].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, -1, 1, D3D12_SHADER_VISIBILITY_PIXEL);
			m_GBufferRS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearWrapDesc);
			m_GBufferRS.InitStaticSampler(1, Graphics::s_CommonStates.SamplerPointClampDesc);
			m_GBufferRS.Finalize(pDevice, L"GBufferRS", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

			// deferred cs
			m_DeferredCSRS.Reset((UINT)DeferredCSRSId::Count, 3);
			m_DeferredCSRS[(UINT)DeferredCSRSId::CBConstants].InitAsConstantBuffer(0);
			m_DeferredCSRS[(UINT)DeferredCSRSId::CommonLights].InitAsConstantBuffer(1);	// CommonLights
			m_DeferredCSRS[(UINT)DeferredCSRSId::CascadedSMConstants].InitAsConstantBuffer(2);	// CascadedShadowMap constants
			m_DeferredCSRS[(UINT)DeferredCSRSId::GBuffer].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5);
			m_DeferredCSRS[(UINT)DeferredCSRSId::ShadowMap].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 1);
			m_DeferredCSRS[(UINT)DeferredCSRSId::MaterialTable].InitAsBufferSRV(1, 1);
			m_DeferredCSRS[(UINT)DeferredCSRSId::TextureTable].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, -1, 1);
			m_DeferredCSRS[(UINT)DeferredCSRSId::DecalTable].InitAsBufferSRV(0, 2);	// decal buffer
			m_DeferredCSRS[(UINT)DeferredCSRSId::DecalTextures].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 10, 2);	// decal textures
			m_DeferredCSRS[(UINT)DeferredCSRSId::OutputTarget].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
			m_DeferredCSRS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearWrapDesc);
			m_DeferredCSRS.InitStaticSampler(1, Graphics::s_CommonStates.SamplerPointClampDesc);

			SamplerDesc shadowSamplerDesc;
			shadowSamplerDesc.SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
			shadowSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
			m_DeferredCSRS.InitStaticSampler(2, shadowSamplerDesc); // Graphics::s_CommonStates.SamplerShadowDesc
			m_DeferredCSRS.Finalize(pDevice, L"DeferredCSRS");

			// visibility buffer
			m_VisibilityRS.Reset((UINT)VisibilityRSId::Count, 3);
			m_VisibilityRS[(UINT)VisibilityRSId::CBConstants].InitAsConstants(0, 16);
			m_VisibilityRS[(UINT)VisibilityRSId::CBPerCamera].InitAsConstantBuffer(1);
			m_VisibilityRS[(UINT)VisibilityRSId::CBVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 2, 16);
			m_VisibilityRS[(UINT)VisibilityRSId::MatrixBufferSRV].InitAsBufferSRV(0, 1);
			m_VisibilityRS[(UINT)VisibilityRSId::MaterialTextures].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, -1, 2);
			m_VisibilityRS[(UINT)VisibilityRSId::RenderTextureSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 32);
			m_VisibilityRS[(UINT)VisibilityRSId::BufferSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 31, 1);
			m_VisibilityRS[(UINT)VisibilityRSId::RenderTextureUAVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 16);
			m_VisibilityRS[(UINT)VisibilityRSId::BufferUAVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 16, 1);
			m_VisibilityRS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearClampDesc);
			m_VisibilityRS.InitStaticSampler(1, Graphics::s_CommonStates.SamplerLinearWrapDesc);
			m_VisibilityRS.InitStaticSampler(2, Graphics::s_CommonStates.SamplerPointClampDesc);
			m_VisibilityRS.Finalize(pDevice, L"VisibilityRS", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

			// culling cs
			m_CullingRS.Reset((UINT)IndirectCullingCSRSId::Count, 1);
			m_CullingRS[(UINT)IndirectCullingCSRSId::CBConstants].InitAsConstants(0, 10);
			m_CullingRS[(UINT)IndirectCullingCSRSId::CBCamera].InitAsConstantBuffer(1);
			m_CullingRS[(UINT)IndirectCullingCSRSId::WorldBounds].InitAsBufferSRV(0);
			m_CullingRS[(UINT)IndirectCullingCSRSId::ShaderResources].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);
			m_CullingRS[(UINT)IndirectCullingCSRSId::Output].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 4);
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

		// input layouts
		// per vertex data
		auto pVertexInputs = m_VertexLayout;
		const uint32_t numVertexElements = pVertexInputs->GetElementCount();
		// per instance data
		auto pInstanceInputs = m_InstanceLayout;
		const uint32_t numInstanceElements = pInstanceInputs ? pInstanceInputs->GetElementCount() : 0;

		uint32_t numElements = numVertexElements + numInstanceElements;
		std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements(numElements);
		{
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
		}

		// PSOs
		{
			// debug wireframe PSO
			m_DebugWireframePSO.SetRootSignature(m_CommonRS);
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
			m_DepthClipIndirectPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerTwoSided);
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
			m_MaskIndirectPSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateTestEqual);
			m_MaskIndirectPSO.Finalize(pDevice);

			// transparent indirect model
			m_TransparentIndirectPSO = m_OpaqueIndirectPSO;
			m_TransparentIndirectPSO.SetBlendState(Graphics::s_CommonStates.BlendTraditional);
			m_TransparentIndirectPSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateReadOnly);
			m_TransparentIndirectPSO.Finalize(pDevice);
		}

		// shadows
		{
			m_CascadedShadowMap.reset(new CascadedShadowMap());
			m_CascadedShadowMap->Init(pDevice, -Math::Vector3(m_CommonLights.sunDirection), *m_Camera);

			// opaque
			m_OpaqueShadowPSO = m_DepthPSO;
			m_OpaqueShadowPSO.SetRootSignature(m_CommonIndirectRS);
			m_OpaqueShadowPSO.SetVertexShader(IndirectDepthVS, sizeof(IndirectDepthVS));
			m_OpaqueShadowPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerShadow);
			m_OpaqueShadowPSO.SetRenderTargetFormats(0, nullptr, m_CascadedShadowMap->m_LightShadowTempBuffer.GetFormat());
			m_OpaqueShadowPSO.Finalize(pDevice);

			// mask
			m_MaskShadowPSO = m_OpaqueShadowPSO;
			m_MaskShadowPSO.SetPixelShader(IndirectDepthCutoutPS, sizeof(IndirectDepthCutoutPS));
			m_MaskShadowPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerShadowTwoSided);
			m_MaskShadowPSO.Finalize(pDevice);
		}

		// bindless deferred
		{
			m_BindlessDeferred.reset(new BindlessDeferred());
			m_BindlessDeferred->Init(pDevice, this);

			// opaque models
			m_OpaqueGBufferPSO = m_OpaqueModelPSO;
			m_OpaqueGBufferPSO.SetRootSignature(m_GBufferRS);
			m_OpaqueGBufferPSO.SetVertexShader(IndirectGBufferVS, sizeof(IndirectGBufferVS));
			m_OpaqueGBufferPSO.SetPixelShader(IndirectGBufferPS, sizeof(IndirectGBufferPS));
			m_OpaqueGBufferPSO.SetRenderTargetFormats(_countof(rtFormats), rtFormats, depthBuffer.GetFormat());
			m_OpaqueGBufferPSO.Finalize(pDevice);

			// mask models
			m_MaskGBufferPSO = m_OpaqueGBufferPSO;
			m_MaskGBufferPSO.SetPixelShader(IndirectGBufferCutoutPS, sizeof(IndirectGBufferCutoutPS));
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

		// visibility buffer
		{
			// per instance data
			auto pInstanceInputs = m_InstanceLayout;
			const uint32_t numInstanceElements = pInstanceInputs ? pInstanceInputs->GetElementCount() : 0;
			ASSERT(numInstanceElements > 0);

			uint32_t numElements = numInstanceElements;
			std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements(numElements);
			{
				uint32_t inputElementIdx = 0;
				for (uint32_t i = 0, imax = numInstanceElements; i < imax; ++i, ++inputElementIdx)
				{
					const auto& layout = pInstanceInputs->GetBufferLayout(i);
					inputElements[inputElementIdx] =
					{ layout.semanticName.c_str(), layout.semanticIndex, layout.format, layout.inputSlot, layout.alignedByteOffset, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, pInstanceInputs->GetInstanceStepRate() };
				}
			}

			auto &visibilityBuffer = m_BindlessDeferred->m_VisibilityBuffer;
			m_VisibilityBufferPSO.SetRootSignature(m_VisibilityRS);
			m_VisibilityBufferPSO.SetInputLayout(numElements, inputElements.data());
			m_VisibilityBufferPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
			m_VisibilityBufferPSO.SetVertexShader(VisibilityBufferVS, sizeof(VisibilityBufferVS));
			m_VisibilityBufferPSO.SetPixelShader(VisibilityBufferPS, sizeof(VisibilityBufferPS));
			m_VisibilityBufferPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerDefault);
			m_VisibilityBufferPSO.SetBlendState(Graphics::s_CommonStates.BlendDisable);
			m_VisibilityBufferPSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateReadWrite);
			m_VisibilityBufferPSO.SetRenderTargetFormat(visibilityBuffer.GetFormat(), depthBuffer.GetFormat());
			m_VisibilityBufferPSO.Finalize(pDevice);

			m_VisibilityComputePSO.SetRootSignature(m_VisibilityRS);
			m_VisibilityComputePSO.SetComputeShader(VisibilityComputeCS, sizeof(VisibilityComputeCS));
			m_VisibilityComputePSO.Finalize(pDevice);
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

	bool IsCutoutMaterial(const std::string& strTexDiffusePath)
	{
		return strTexDiffusePath.find("thorn") != std::string::npos ||
			strTexDiffusePath.find("plant") != std::string::npos ||
			strTexDiffusePath.find("chain") != std::string::npos;
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
		CreateDrawList(pDevice);
		UpdateGeometryStats();
	}

	Scene::UpdateFlags Scene::Update(float deltaTime)
	{
		m_CameraController->Update(deltaTime);

		// m_CommonLights.sunOrientation += 0.002f * Math::Pi * deltaTime;
		UpdateSunLight();

		m_CascadedShadowMap->PrepareCascades(-Math::Vector3(m_CommonLights.sunDirection), *m_Camera);

		const uint32_t bufferWidth = GfxStates::s_NativeWidth, bufferHeight = GfxStates::s_NativeHeight;

		// View uniforms
		{
			const auto& viewMat = m_Camera->GetViewMatrix();
			const auto& projMat = m_Camera->GetProjMatrix();
			const auto& camPos = m_Camera->GetPosition();

			auto &viewUniformParams = m_ViewUniformParams;

			Math::Matrix4 screenToViewMat = Math::Matrix4(
				Math::Vector4(1.0f / projMat.GetX().GetX(), 0, 0, 0),
				Math::Vector4(0, 1.0f / projMat.GetY().GetY(), 0, 0),
				Math::Vector4(0, 0, 1.0f, 0),
				Math::Vector4(0, 0, 0, 1.0f));

			viewUniformParams.viewMat = Cast(viewMat);
			viewUniformParams.projMat = Cast(projMat);
			viewUniformParams.viewProjMat = Cast(m_Camera->GetViewProjMatrix());
			viewUniformParams.invViewProjMat =  MMATH::inverse(viewUniformParams.viewProjMat);
			viewUniformParams.bufferSizeAndInvSize = Vector4((float)bufferWidth, (float)bufferHeight, 1.0f / bufferWidth, 1.0f / bufferHeight);
			viewUniformParams.camPos = Vector4(camPos.GetX(), camPos.GetY(), camPos.GetZ(), 0.0f);
			viewUniformParams.cascadeSplits = Vector4(0, 0, 0, 0);
			float farClip = m_Camera->GetFarClip(), nearClip = m_Camera->GetNearClip();
			viewUniformParams.nearClip = nearClip;
			viewUniformParams.farClip = farClip;

			// FIXME: Use UploadBuffer seems run slower ???
			m_ViewUniformBuffer.CopyToGpu(&viewUniformParams, sizeof(viewUniformParams), m_Graphics->GetCurrentFrameIndex());
		}

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

	void Scene::BeginRendering(GraphicsContext& gfx)
	{
		// Begin rendering
	}

	void Scene::EndRendering(GraphicsContext& gfx)
	{
		m_FrameDescriptorHeap.EndFrame();
	}

	void Scene::BeginDrawing(GraphicsContext& gfx, bool bIndirectRendering)
	{
		gfx.SetVertexBuffer(0, m_VertexBuffer->VertexBufferView());
		gfx.SetIndexBuffer(m_IndexBuffer->IndexBufferView());
		if (m_InstanceBuffer)
			gfx.SetVertexBuffer(1, m_InstanceBuffer->VertexBufferView());
		gfx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}

	void Scene::BeginIndexDrawing(GraphicsContext& gfx)
	{
		gfx.SetIndexBuffer(m_IndexBuffer->IndexBufferView());
		if (m_InstanceBuffer) gfx.SetVertexBuffer(1, m_InstanceBuffer->VertexBufferView());
		gfx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}

	void Scene::SetRenderCamera(GraphicsContext& gfx, const Matrix4x4& viewProjMat, const Vector3& camPos, UINT rootIdx)
	{
	#if !USE_VIEW_UNIFORMS
		CBPerCamera cbPerCamera;
		cbPerCamera._ViewProjMat = viewProjMat;	// 'viewProjMat' needn't transpose, has transposed in savings
		cbPerCamera._CamPos = camPos;
		gfx.SetDynamicConstantBufferView(rootIdx, sizeof(CBPerCamera), &cbPerCamera);
	#else
		gfx.SetConstantBuffer(rootIdx, m_ViewUniformBuffer.GetInstanceGpuPointer(m_Graphics->GetCurrentFrameIndex()));
	#endif
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
			instanceIds = &m_MaskInstances;
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
		uint32_t opaqueCommandsCount = (uint32_t)m_OpaqueInstances.size();
		uint32_t maskCommandsCount = (uint32_t)m_MaskInstances.size();
		uint32_t transparentCommandsCount = (uint32_t)m_TransparentInstances.size();
		uint32_t opaqueCommandsOffset = 0;
		uint32_t maskCommandsOffset = opaqueCommandsOffset + opaqueCommandsCount;
		uint32_t transparentCommandsOffset = maskCommandsOffset + maskCommandsCount;

		auto *frustumCulledCommandBuffer = &m_FrustumCulledBuffer;
		uint32_t commandOffset = opaqueCommandsOffset, commandCount = opaqueCommandsOffset;
		if (alphaMode == AlphaMode::kMASK)
		{
			frustumCulledCommandBuffer = &m_FrustumCulledMaskBuffer;
			commandOffset = maskCommandsOffset;
			commandCount = maskCommandsCount;
		}
		else if (alphaMode == AlphaMode::kBLEND)
		{
			frustumCulledCommandBuffer = &m_FrustumCulledTransparentBuffer;
			commandOffset = transparentCommandsOffset;
			commandCount = transparentCommandsCount;
		}

		gfx.TransitionResource(m_CommandsBuffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		gfx.TransitionResource( *frustumCulledCommandBuffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		gfx.TransitionResource((*frustumCulledCommandBuffer).GetCounterBuffer(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		
		if (m_EnableOcclusionCulling)
		{
			gfx.TransitionResource(m_OcclusionCulledBuffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			gfx.TransitionResource(m_OcclusionCulledBuffer.GetCounterBuffer(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		}

		gfx.SetConstants((UINT)CommonIndirectRSId::CBConstants, 0, 1, 2, 3);

		gfx.SetShaderResourceView((UINT)CommonIndirectRSId::MatrixTable, m_MatricesDynamicBuffer.GetGpuPointer());
		gfx.SetBufferSRV((UINT)CommonIndirectRSId::MaterialTable, m_MaterialsBuffer);
		gfx.SetBufferSRV((UINT)CommonIndirectRSId::MeshTable, m_MeshesBuffer);
		gfx.SetBufferSRV((UINT)CommonIndirectRSId::MeshInstanceTable, m_MeshInstancesBuffer);
		
		// Set descriptor heap
	#if 0
		gfx.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_TextureDescriptorHeap.GetHeapPointer());
		gfx.SetDescriptorTable((UINT)CommonIndirectRSId::TextureTable, m_TextureDescriptorHeap.GetHandleAtOffset(0).GetGpuHandle());
	#else
		gfx.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_FrameDescriptorHeap.CurrentHeap()->GetHeapPointer());

		// Material textures
		gfx.SetDescriptorTable((UINT)CommonIndirectRSId::TextureTable, m_FrameDescriptorHeap.HandleFromIndex(s_DescriptorRanges[DescriptorParams::MaterialTextures].start));
	#endif

		if (alphaMode == AlphaMode::UNKNOWN)
		{
			gfx.SetPipelineState(m_OpaqueIndirectPSO);
			
			if (m_EnableFrustumCulling)
			{
				if (m_EnableOcclusionCulling)
					gfx.ExecuteIndirect(m_CommandSignature, m_OcclusionCulledBuffer, 0, opaqueCommandsCount, &m_OcclusionCulledBuffer.GetCounterBuffer());
				else
					gfx.ExecuteIndirect(m_CommandSignature, m_FrustumCulledBuffer, 0, opaqueCommandsCount, &m_FrustumCulledBuffer.GetCounterBuffer());
			}
			else
			{
				gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, opaqueCommandsOffset * sizeof(IndirectCommand), opaqueCommandsCount);
			}

			gfx.SetPipelineState(m_MaskIndirectPSO);
			gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, maskCommandsOffset * sizeof(IndirectCommand), maskCommandsCount);

			gfx.SetPipelineState(m_TransparentIndirectPSO);
			gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, transparentCommandsOffset * sizeof(IndirectCommand), transparentCommandsCount);
		}
		else if (alphaMode == AlphaMode::kOPAQUE)
		{
			gfx.SetPipelineState(pso);
			gfx.ExecuteIndirect(m_CommandSignature, m_FrustumCulledBuffer, 0, opaqueCommandsCount, &m_FrustumCulledBuffer.GetCounterBuffer());
		}
		else if (alphaMode == AlphaMode::kMASK)
		{
			gfx.SetPipelineState(pso);
			gfx.ExecuteIndirect(m_CommandSignature, m_FrustumCulledMaskBuffer, 0, maskCommandsCount, &m_FrustumCulledMaskBuffer.GetCounterBuffer());
		}
		else if (alphaMode == AlphaMode::kBLEND)
		{
			gfx.SetPipelineState(pso);
			gfx.ExecuteIndirect(m_CommandSignature, m_FrustumCulledTransparentBuffer, 0, transparentCommandsCount, &m_FrustumCulledTransparentBuffer.GetCounterBuffer());
		}

	}

	// deferred rendering
	void Scene::PrepareGBuffer(GraphicsContext& gfx)
	{
		auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
		auto& tangentFrame = m_BindlessDeferred->m_TangentFrame;
		auto& uvTarget = m_BindlessDeferred->m_UVTarget;
		auto& uvGradientsTarget = m_BindlessDeferred->m_UVGradientsTarget;
		auto& materialIDTarget = m_BindlessDeferred->m_MaterialIDTarget;

		gfx.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		gfx.TransitionResource(tangentFrame, D3D12_RESOURCE_STATE_RENDER_TARGET);
		gfx.TransitionResource(uvTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
		gfx.TransitionResource(uvGradientsTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
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
	}

	void Scene::RenderToGBuffer(GraphicsContext& gfx, GraphicsPSO& pso, AlphaMode alphaMode)
	{
		uint32_t opaqueCommandsCount = (uint32_t)m_OpaqueInstances.size();
		uint32_t maskCommandsCount = (uint32_t)m_MaskInstances.size();
		uint32_t transparentCommandsCount = (uint32_t)m_TransparentInstances.size();
		uint32_t opaqueCommandsOffset = 0;
		uint32_t maskCommandsOffset = opaqueCommandsOffset + opaqueCommandsCount;
		uint32_t transparentCommandsOffset = maskCommandsOffset + maskCommandsCount;

		auto* frustumCulledCommandBuffer = &m_FrustumCulledBuffer;
		uint32_t commandOffset = opaqueCommandsOffset, commandCount = opaqueCommandsOffset;
		if (alphaMode == AlphaMode::kMASK)
		{
			frustumCulledCommandBuffer = &m_FrustumCulledMaskBuffer;
			commandOffset = maskCommandsOffset;
			commandCount = maskCommandsCount;
		}
		else if (alphaMode == AlphaMode::kBLEND)
		{
			frustumCulledCommandBuffer = &m_FrustumCulledTransparentBuffer;
			commandOffset = transparentCommandsOffset;
			commandCount = transparentCommandsCount;
		}

		gfx.TransitionResource(m_CommandsBuffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		gfx.TransitionResource(*frustumCulledCommandBuffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		gfx.TransitionResource((*frustumCulledCommandBuffer).GetCounterBuffer(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

		gfx.SetConstants((UINT)GBufferRSId::CBConstants, 0, 1, 2, 3);
		gfx.SetShaderResourceView((UINT)GBufferRSId::MatrixTable, m_MatricesDynamicBuffer.GetGpuPointer());
		gfx.SetBufferSRV((UINT)GBufferRSId::MaterialTable, m_MaterialsBuffer);
		gfx.SetBufferSRV((UINT)GBufferRSId::MeshTable, m_MeshesBuffer);
		gfx.SetBufferSRV((UINT)GBufferRSId::MeshInstanceTable, m_MeshInstancesBuffer);

		gfx.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_FrameDescriptorHeap.CurrentHeap()->GetHeapPointer());
		gfx.SetDescriptorTable((UINT)GBufferRSId::TextureTable, m_FrameDescriptorHeap.HandleFromIndex(s_DescriptorRanges[DescriptorParams::MaterialTextures].start));
		
		if (alphaMode == AlphaMode::UNKNOWN || alphaMode == AlphaMode::kOPAQUE)
		{
			gfx.SetPipelineState(m_OpaqueGBufferPSO);
			
			if (m_EnableFrustumCulling)
				gfx.ExecuteIndirect(m_CommandSignature, m_FrustumCulledBuffer, 0, opaqueCommandsCount, &m_FrustumCulledBuffer.GetCounterBuffer());
			else
				gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, opaqueCommandsOffset * sizeof(IndirectCommand), opaqueCommandsCount);
		}
		
		if (alphaMode == AlphaMode::UNKNOWN || alphaMode == AlphaMode::kMASK)
		{
			gfx.SetPipelineState(m_MaskGBufferPSO);

			if (m_EnableFrustumCulling)
				gfx.ExecuteIndirect(m_CommandSignature, m_FrustumCulledMaskBuffer, 0, maskCommandsCount, &m_FrustumCulledMaskBuffer.GetCounterBuffer());
			else
				gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, maskCommandsOffset * sizeof(IndirectCommand), maskCommandsCount);
		}
	}

	void Scene::DeferredRender(ComputeContext& computeContext, ComputePSO& pso)
	{
		auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
		auto& tangentFrame = m_BindlessDeferred->m_TangentFrame;
		auto& uvTarget = m_BindlessDeferred->m_UVTarget;
		auto& uvGradientsTarget = m_BindlessDeferred->m_UVGradientsTarget;
		auto& materialIDTarget = m_BindlessDeferred->m_MaterialIDTarget;
		auto& shadowMap = m_CascadedShadowMap->m_LightShadowArray;

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
		const auto& cascadeSplits = m_CascadedShadowMap->m_CascadeSplits;
		csConstants._CascadeSplits = XMFLOAT4(cascadeSplits.GetX(), cascadeSplits.GetY(), cascadeSplits.GetZ(), cascadeSplits.GetW());
		csConstants._ViewProjMat = Cast(m_Camera->GetViewProjMatrix());
		csConstants._InvViewProjMat = MMATH::inverse(csConstants._ViewProjMat);
		csConstants._ViewMat = Cast(m_Camera->GetViewMatrix());
		csConstants._ProjMat = Cast(m_Camera->GetProjMatrix());
		computeContext.SetDynamicConstantBufferView((UINT)DeferredCSRSId::CBConstants, sizeof(DeferredCSConstants), &csConstants);

		computeContext.SetDynamicConstantBufferView((UINT)DeferredCSRSId::CommonLights, sizeof(CommonLightSettings), &m_CommonLights);

		constexpr uint32_t numCascades = CascadedShadowMap::s_NumCascades;
		Math::Matrix4 cascadedShadowMats[numCascades];
		for (uint32_t i = 0; i < numCascades; ++i)
		{
			cascadedShadowMats[i] = Math::Transpose(m_CascadedShadowMap->m_ViewProjMat[i]);
		}
		computeContext.SetDynamicConstantBufferView((UINT)DeferredCSRSId::CascadedSMConstants, sizeof(cascadedShadowMats), cascadedShadowMats);

		computeContext.SetBufferSRV((UINT)DeferredCSRSId::MaterialTable, m_MaterialsBuffer);
		computeContext.SetBufferSRV((UINT)DeferredCSRSId::DecalTable, m_BindlessDeferred->m_DecalBuffer);
	#if 0
		D3D12_CPU_DESCRIPTOR_HANDLE gbufferHandles[] =
		{
			depthBuffer.GetDepthSRV(),
			tangentFrame.GetSRV(), uvTarget.GetSRV(),
			uvGradientsTarget.GetSRV(), materialIDTarget.GetSRV(),
		};
		computeContext.SetDynamicDescriptors((UINT)DeferredCSRSId::GBuffer, 0, _countof(gbufferHandles), gbufferHandles);
		computeContext.SetDynamicDescriptor((UINT)DeferredCSRSId::ShadowMap, 0, shadowMap.GetSRV());
		
		computeContext.SetDescriptorTable((UINT)DeferredCSRSId::TextureTable, m_TextureDescriptorHeap.GetHandleAtOffset(0).GetGpuHandle());

		// decal
		computeContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_BindlessDeferred.m_DecalTextureHeap.GetHeapPointer());
		computeContext.SetDescriptorTable((UINT)DeferredCSRSId::DecalTextures, m_BindlessDeferred.m_DecalTextureHeap.GetHandleAtOffset(0).GetGpuHandle());

		computeContext.SetDynamicDescriptor((UINT)DeferredCSRSId::OutputTarget, 0, colorBuffer.GetUAV());
	#else
		/// NEW:
		computeContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_FrameDescriptorHeap.CurrentHeap()->GetHeapPointer());
		
		// material textures
		computeContext.SetDescriptorTable((UINT)DeferredCSRSId::TextureTable, m_FrameDescriptorHeap.HandleFromIndex(s_DescriptorRanges[DescriptorParams::MaterialTextures].start));
		// decal textures
		computeContext.SetDescriptorTable((UINT)DeferredCSRSId::DecalTextures, m_FrameDescriptorHeap.HandleFromIndex(s_DescriptorRanges[DescriptorParams::DecalTextures].start));

		computeContext.SetDescriptorTable((UINT)DeferredCSRSId::GBuffer, m_FrameDescriptorHeap.HandleFromIndex(s_DescriptorRanges[DescriptorParams::RenderTextureSRVs].start + RenderTextureSRVs::DepthBuffer));
		computeContext.SetDescriptorTable((UINT)DeferredCSRSId::ShadowMap, m_FrameDescriptorHeap.HandleFromIndex(s_DescriptorRanges[DescriptorParams::RenderTextureSRVs].start + RenderTextureSRVs::ShadowMap));
		computeContext.SetDescriptorTable((UINT)DeferredCSRSId::OutputTarget, m_FrameDescriptorHeap.HandleFromIndex(s_DescriptorRanges[DescriptorParams::RenderTextureUAVs].start + RenderTextureUAVs::ColorBuffer));
	#endif

		uint32_t groupCountX = Math::DivideByMultiple(width, BindlessDeferred::s_DeferredTileSize);
		uint32_t groupCountY = Math::DivideByMultiple(height, BindlessDeferred::s_DeferredTileSize);
		computeContext.Dispatch(groupCountX, groupCountY);

		computeContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
	}

	// visibility buffer
	void Scene::PrepareVisibilityBuffer(GraphicsContext& gfx)
	{
		auto &visibilityBuffer = m_BindlessDeferred->m_VisibilityBuffer;
		auto &depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;

		gfx.TransitionResource(visibilityBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
		gfx.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);

		gfx.ClearColor(visibilityBuffer);
		gfx.ClearDepth(depthBuffer);

		gfx.SetRenderTarget(visibilityBuffer.GetRTV(), depthBuffer.GetDSV());
	}

	void Scene::RenderVisibilityBuffer(GraphicsContext& gfx, AlphaMode alphaMode)
	{
		uint32_t opaqueCommandsCount = (uint32_t)m_OpaqueInstances.size();
		uint32_t maskCommandsCount = (uint32_t)m_MaskInstances.size();
		uint32_t transparentCommandsCount = (uint32_t)m_TransparentInstances.size();
		uint32_t opaqueCommandsOffset = 0;
		uint32_t maskCommandsOffset = opaqueCommandsOffset + opaqueCommandsCount;
		uint32_t transparentCommandsOffset = maskCommandsOffset + maskCommandsCount;

		auto* frustumCulledCommandBuffer = &m_FrustumCulledBuffer;
		uint32_t commandOffset = opaqueCommandsOffset, commandCount = opaqueCommandsOffset;
		if (alphaMode == AlphaMode::kMASK)
		{
			frustumCulledCommandBuffer = &m_FrustumCulledMaskBuffer;
			commandOffset = maskCommandsOffset;
			commandCount = maskCommandsCount;
		}
		else if (alphaMode == AlphaMode::kBLEND)
		{
			frustumCulledCommandBuffer = &m_FrustumCulledTransparentBuffer;
			commandOffset = transparentCommandsOffset;
			commandCount = transparentCommandsCount;
		}

		gfx.TransitionResource(m_CommandsBuffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		gfx.TransitionResource(*frustumCulledCommandBuffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		gfx.TransitionResource((*frustumCulledCommandBuffer).GetCounterBuffer(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

		gfx.SetConstants((UINT)VisibilityRSId::CBConstants, 0, 1, 2, 3);
		gfx.SetConstantBuffer((UINT)VisibilityRSId::CBPerCamera, m_ViewUniformBuffer.GetInstanceGpuPointer(m_Graphics->GetCurrentFrameIndex()));
		gfx.SetShaderResourceView((UINT)VisibilityRSId::MatrixBufferSRV, m_MatricesDynamicBuffer.GetGpuPointer());
	#if 0
		gfx.SetBufferSRV((UINT)VisibilityRSId::MaterialTable, m_MaterialsBuffer);
		gfx.SetBufferSRV((UINT)VisibilityRSId::MeshTable, m_MeshesBuffer);
		gfx.SetBufferSRV((UINT)VisibilityRSId::MeshInstanceTable, m_MeshInstancesBuffer);
	#endif

		gfx.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_FrameDescriptorHeap.CurrentHeap()->GetHeapPointer());
		gfx.SetDescriptorTable((UINT)VisibilityRSId::BufferSRVs, m_FrameDescriptorHeap.HandleFromIndex(s_DescriptorRanges[DescriptorParams::BufferSRVs].start));
		gfx.SetDescriptorTable((UINT)VisibilityRSId::MaterialTextures, m_FrameDescriptorHeap.HandleFromIndex(s_DescriptorRanges[DescriptorParams::MaterialTextures].start));

		if (alphaMode == AlphaMode::UNKNOWN || alphaMode == AlphaMode::kOPAQUE)
		{
			gfx.SetPipelineState(m_VisibilityBufferPSO);

			if (m_EnableFrustumCulling)
				gfx.ExecuteIndirect(m_CommandSignature, m_FrustumCulledBuffer, 0, opaqueCommandsCount, &m_FrustumCulledBuffer.GetCounterBuffer());
			else
				gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, opaqueCommandsOffset * sizeof(IndirectCommand), opaqueCommandsCount);
		}

	#if 0
		if (alphaMode == AlphaMode::UNKNOWN || alphaMode == AlphaMode::kMASK)
		{
			gfx.SetPipelineState(m_VisibilityBufferPSO);

			if (m_EnableFrustumCulling)
				gfx.ExecuteIndirect(m_CommandSignature, m_FrustumCulledMaskBuffer, 0, maskCommandsCount, &m_FrustumCulledMaskBuffer.GetCounterBuffer());
			else
				gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, maskCommandsOffset * sizeof(IndirectCommand), maskCommandsCount);
		}
	#endif
	}

	void Scene::VisibilityCompute(ComputeContext& computeContext, ComputePSO& pso)
	{
		auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
		auto& visibilityBuffer = m_BindlessDeferred->m_VisibilityBuffer;

		auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
		uint32_t width = colorBuffer.GetWidth(), height = colorBuffer.GetHeight();

		computeContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(visibilityBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		computeContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		
		// clear
		computeContext.ClearUAV(colorBuffer);
		computeContext.InsertUAVBarrier(colorBuffer);

		computeContext.SetPipelineState(pso);

		computeContext.SetConstantBuffer((UINT)VisibilityRSId::CBPerCamera, m_ViewUniformBuffer.GetInstanceGpuPointer(m_Graphics->GetCurrentFrameIndex()));
		computeContext.SetShaderResourceView((UINT)VisibilityRSId::MatrixBufferSRV, m_MatricesDynamicBuffer.GetGpuPointer());
	#if 0
		computeContext.SetBufferSRV((UINT)VisibilityRSId::MatrixBufferSRV, m_MaterialsBuffer);
	#endif
		computeContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_FrameDescriptorHeap.CurrentHeap()->GetHeapPointer());
	#if 0
		// material textures
		computeContext.SetDescriptorTable((UINT)VisibilityRSId::TextureTable, m_FrameDescriptorHeap.HandleFromIndex(s_DescriptorRanges[DescriptorParams::MaterialTextures].start));
		// decal textures
		computeContext.SetDescriptorTable((UINT)VisibilityRSId::DecalTextures, m_FrameDescriptorHeap.HandleFromIndex(s_DescriptorRanges[DescriptorParams::DecalTextures].start));
	#endif
		computeContext.SetDescriptorTable((UINT)VisibilityRSId::MaterialTextures, m_FrameDescriptorHeap.HandleFromIndex(s_DescriptorRanges[DescriptorParams::MaterialTextures].start));
		computeContext.SetDescriptorTable((UINT)VisibilityRSId::BufferSRVs, m_FrameDescriptorHeap.HandleFromIndex(s_DescriptorRanges[DescriptorParams::BufferSRVs].start));
		computeContext.SetDescriptorTable((UINT)VisibilityRSId::RenderTextureSRVs, m_FrameDescriptorHeap.HandleFromIndex(s_DescriptorRanges[DescriptorParams::RenderTextureSRVs].start));
		computeContext.SetDescriptorTable((UINT)VisibilityRSId::RenderTextureUAVs, m_FrameDescriptorHeap.HandleFromIndex(s_DescriptorRanges[DescriptorParams::RenderTextureUAVs].start + RenderTextureUAVs::ColorBuffer));

		static const uint32_t GroupSize = 8;
		uint32_t groupCountX = Math::DivideByMultiple(width, GroupSize);
		uint32_t groupCountY = Math::DivideByMultiple(height, GroupSize);
		computeContext.Dispatch(groupCountX, groupCountY);

		// unnecessary
		// computeContext.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
	}

	void Scene::RenderSunShadows(GraphicsContext& gfx)
	{
		gfx.TransitionResource(m_CommandsBuffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

		gfx.SetRootSignature(m_CommonIndirectRS, false);

		gfx.SetConstants((UINT)CommonIndirectRSId::CBConstants, 0, 1, 2, 3);
		gfx.SetShaderResourceView((UINT)CommonIndirectRSId::MatrixTable, m_MatricesDynamicBuffer.GetGpuPointer());
		gfx.SetBufferSRV((UINT)CommonIndirectRSId::MaterialTable, m_MaterialsBuffer);
		gfx.SetBufferSRV((UINT)CommonIndirectRSId::MeshTable, m_MeshesBuffer);
		gfx.SetBufferSRV((UINT)CommonIndirectRSId::MeshInstanceTable, m_MeshInstancesBuffer);

	#if 0
		gfx.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_TextureDescriptorHeap.GetHeapPointer());
		gfx.SetDescriptorTable((UINT)CommonIndirectRSId::TextureTable, m_TextureDescriptorHeap.GetHandleAtOffset(0).GetGpuHandle());
	#else
		gfx.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_FrameDescriptorHeap.CurrentHeap()->GetHeapPointer());

		// Material textures
		gfx.SetDescriptorTable((UINT)CommonIndirectRSId::TextureTable, m_FrameDescriptorHeap.HandleFromIndex(s_DescriptorRanges[DescriptorParams::MaterialTextures].start));
	#endif

		uint32_t opaqueCommandsCount = (uint32_t)m_OpaqueInstances.size();
		uint32_t maskCommandsCount = (uint32_t)m_MaskInstances.size();
		uint32_t transparentCommandsCount = (uint32_t)m_TransparentInstances.size();
		uint32_t opaqueCommandsOffset = 0;
		uint32_t maskCommandsOffset = opaqueCommandsOffset + opaqueCommandsCount;
		uint32_t transparentOffset = maskCommandsOffset + maskCommandsCount;

		ViewUniformParameters viewUniformParams;
		viewUniformParams.bufferSizeAndInvSize = Vector4(1, 1, 1, 1);
		
		CBPerCamera cbPerCamera;
		uint32_t numCascades = CascadedShadowMap::s_NumCascades;
		auto &cascadedShadowMap = *m_CascadedShadowMap;
		// TODO: Only Cascade 1, no cullings yet
		for (uint32_t i = 0; i < 1; ++i)
		{
			// clear depth
			m_CascadedShadowMap->m_LightShadowTempBuffer.BeginRendering(gfx);

			const auto &camPos = cascadedShadowMap.m_CamPos[i];
		#if !USE_VIEW_UNIFORMS
			cbPerCamera._ViewProjMat = Cast(m_CascadedShadowMap.m_ViewProjMat[i]);
			cbPerCamera._CamPos = Cast(m_CascadedShadowMap.m_CamPos[i]);
			gfx.SetDynamicConstantBufferView((UINT)CommonIndirectRSId::CBPerCamera, sizeof(CBPerCamera), &cbPerCamera);
		#else
			viewUniformParams.viewProjMat = Cast(m_CascadedShadowMap->m_ViewProjMat[i]);
			viewUniformParams.invViewProjMat = MMATH::inverse(viewUniformParams.viewProjMat);
			viewUniformParams.camPos = Vector4(camPos.GetX(), camPos.GetY(), camPos.GetZ(), 0.0);
			gfx.SetDynamicConstantBufferView((UINT)CommonIndirectRSId::CBPerCamera, sizeof(ViewUniformParameters), &viewUniformParams);
		#endif

			// opaque shadow
			gfx.SetPipelineState(m_OpaqueShadowPSO);
			gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, opaqueCommandsOffset * sizeof(IndirectCommand), opaqueCommandsCount);

			// mask shadow
			gfx.SetPipelineState(m_MaskShadowPSO);
			gfx.ExecuteIndirect(m_CommandSignature, m_CommandsBuffer, maskCommandsOffset * sizeof(IndirectCommand), maskCommandsCount);

			gfx.TransitionResource(cascadedShadowMap.m_LightShadowTempBuffer, D3D12_RESOURCE_STATE_GENERIC_READ);
			gfx.TransitionResource(cascadedShadowMap.m_LightShadowArray, D3D12_RESOURCE_STATE_COPY_DEST);
			gfx.CopySubresource(cascadedShadowMap.m_LightShadowArray, i, cascadedShadowMap.m_LightShadowTempBuffer, 0);
		}

		// m_CascadedShadowMap.m_LightShadowTempBuffer.EndRendering(gfx);
	}

	void Scene::FrustumCulling(ComputeContext& computeContext, const Matrix4x4& viewMat, const Matrix4x4& projMat, AlphaMode alphaMode)
	{
		uint32_t opaqueCommandsCount = (uint32_t)m_OpaqueInstances.size();
		uint32_t maskCommandsCount = (uint32_t)m_MaskInstances.size();
		uint32_t transparentCommandsCount = (uint32_t)m_TransparentInstances.size();
		uint32_t opaqueCommandsOffset = 0;
		uint32_t maskCommandsOffset = opaqueCommandsOffset + opaqueCommandsCount;
		uint32_t transparentOffset = maskCommandsOffset + maskCommandsCount;
		uint32_t instanceCount = opaqueCommandsCount + maskCommandsCount + transparentCommandsCount;
		uint32_t gridDimensions = Math::DivideByMultiple(instanceCount, 1024);

		uint32_t start = opaqueCommandsOffset, count = opaqueCommandsCount;
		auto* culledCommandBuffer = &m_FrustumCulledBuffer;
		if (alphaMode == AlphaMode::kMASK)
		{
			start = maskCommandsOffset;
			count = maskCommandsCount;
			culledCommandBuffer = &m_FrustumCulledMaskBuffer;
		}
		else if (alphaMode == AlphaMode::kBLEND)
		{
			start = transparentOffset;
			count = transparentCommandsCount;
			culledCommandBuffer = &m_FrustumCulledTransparentBuffer;
		}

		computeContext.TransitionResource(m_CommandsBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		// debug
		if (m_EnableDebugCulling)
		{
			computeContext.TransitionResource(m_DCullValues, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			computeContext.TransitionResource(m_DSummedIndex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}

		computeContext.ResetCounter(*culledCommandBuffer);

		computeContext.SetRootSignature(m_CullingRS);
		computeContext.SetPipelineState(m_FrustumCSPSO);
		
		struct {
			uint32_t _ReadStart, _ReadCount;
			uint32_t _WriteStart;
			uint32_t _InstanceCount;
			uint32_t _GridDimensions;
		} cbuffer = { start, count, 0, instanceCount, gridDimensions };
		computeContext.SetConstants((UINT)IndirectCullingCSRSId::CBConstants, sizeof(cbuffer)/4, &cbuffer);

	#if !USE_VIEW_UNIFORMS
		CullingCSConstants csConstants;
		csConstants._ViewProjMat = viewMat * projMat;
			// 注：MMath::Cast 其实是将Math::Matrix4 转置(0000 | 1111 | 2222 | 3333)->(0123 | 0123 | ...)，
			// 但是因为Math::Matrix4 将0000视作一行，MMath::Matrix4x4 将0123视作一列，所以其实矩阵没变，
			// 所以MMath 矩阵乘法 类似 (V^T * P^T) = (P * V)^T
			// 这里相乘仍用 行向量 的矩阵左乘(向量在左)

			// MMath::Cast is doing Math::Matrix4 transpose (0000 | 1111 | 2222 | 3333)->(0123 | 0123 | ...),
			// But because Math::Matrix4 see 0000 as one row, MMath::Matrix4x4 see 0123 as one column, so it's the same.
			// So MMath multiply is like (V^T * P^T) = (P * V)^T
			// It's still use 'Row vector's Left Matrix Multiply'.
		csConstants._InvViewProjMat = MMATH::inverse(csConstants._ViewProjMat);
		csConstants._ViewMat = viewMat;
		csConstants._ProjMat = projMat;
		computeContext.SetDynamicConstantBufferView((UINT)IndirectCullingCSRSId::CBCamera, sizeof(CullingCSConstants), &csConstants);
	#else
		computeContext.SetConstantBuffer((UINT)IndirectCullingCSRSId::CBCamera, m_ViewUniformBuffer.GetInstanceGpuPointer(m_Graphics->GetCurrentFrameIndex()));
	#endif

		computeContext.SetShaderResourceView((UINT)IndirectCullingCSRSId::WorldBounds, m_BoundsDynamicBuffer.GetGpuPointer());

		D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
			m_CommandsBuffer.GetSRV()
		};
		computeContext.SetDynamicDescriptors((UINT)IndirectCullingCSRSId::ShaderResources, 0, _countof(srvs), srvs);

		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> uavVec;
		uavVec.push_back(culledCommandBuffer->GetUAV());
		if (m_EnableDebugCulling)
		{
			uavVec.push_back(m_DCullValues.GetUAV());
			uavVec.push_back(m_DSummedIndex.GetUAV());
		}
		computeContext.SetDynamicDescriptors((UINT)IndirectCullingCSRSId::Output, 0, (uint32_t)uavVec.size(), uavVec.data());

		computeContext.Dispatch(gridDimensions, 1, 1);

		if (m_EnableDebugCulling)
		{
			computeContext.TransitionResource(m_DCullValues, D3D12_RESOURCE_STATE_GENERIC_READ);
			computeContext.TransitionResource(m_DSummedIndex, D3D12_RESOURCE_STATE_GENERIC_READ);
		}
		computeContext.InsertUAVBarrier(*culledCommandBuffer, true);
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
		m_CascadedShadowMap->Clean();
		// bindless deferred
		m_BindlessDeferred->Clean();

		// culling
		m_FrustumCulledBuffer.Destroy();
		m_FrustumCulledMaskBuffer.Destroy();
		m_FrustumCulledTransparentBuffer.Destroy();
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

		m_TextureDescriptorHeap.Destroy();
		m_FrameDescriptorHeap.Destroy();

		m_CommandSignature.Destroy();

		// globals
		m_ViewUniformBuffer.Destroy();
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

		// material buffer
		std::vector<MaterialData> materialData(numMaterials);
		for (uint32_t matId = 0; matId < numMaterials; ++matId)
		{
			materialData[matId] = m_Materials[matId]->GetMaterialData();
		}
		m_MaterialsBuffer.Create(pDevice, L"MaterialsBuffer", numMaterials, sizeof(MaterialData), materialData.data());

		// mesh buffer
		uint32_t numMeshes = (uint32_t)m_MeshDescs.size();
		m_MeshesBuffer.Create(pDevice, L"MeshesBuffer", numMeshes, sizeof(MeshDesc), m_MeshDescs.data());

		// mesh instance buffer
		uint32_t numInstances = (uint32_t)m_MeshInstanceData.size();
		m_MeshInstancesBuffer.Create(pDevice, L"MeshInstancesBuffer", numInstances, sizeof(MeshInstanceData), m_MeshInstanceData.data());

		// FIXME: put somewhere else
		m_ViewUniformBuffer.Create(pDevice, L"ViewUniformBuffer", SWAP_CHAIN_BUFFER_COUNT, sizeof(ViewUniformParameters), true);
	}

	void Scene::InitDescriptors(ID3D12Device* pDevice)
	{
		uint32_t numMaterials = (uint32_t)m_Materials.size();

		auto &bindlessDeferred = *m_BindlessDeferred;
		auto &cascadedShadowMap = *m_CascadedShadowMap;

		// texture descriptor heap

		uint32_t numTexturesPerMat = (uint32_t)TextureType::Count;
		uint32_t numMaterialTextures = numTexturesPerMat * numMaterials;

		m_TextureDescriptorHeap.Create(pDevice, L"TextureDescriptorHeap");
		m_TextureDescriptorHeap.Alloc(numMaterials * numTexturesPerMat);
		UINT srvDescriptorStepSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		m_FrameDescriptorHeap.Create(pDevice, L"FrameDescriptorHeap", 512);
		uint32_t frames = m_FrameDescriptorHeap.FrameHeapCount();

		[[maybe_unused]]
		constexpr auto DescriptorHeapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

		// material textures
		{
			auto &materialTextureRange = s_DescriptorRanges[DescriptorParams::MaterialTextures];
			materialTextureRange.start = m_FrameDescriptorHeap.PersistentAllocated();
			materialTextureRange.count = numMaterialTextures;

			std::vector<MaterialData> materialData(numMaterials);
			for (uint32_t matId = 0; matId < numMaterials; ++matId)
			{
				const auto& curMat = m_Materials[matId];
				const auto& srcTexHandles = curMat->GetDescriptors();

			#if 0
				auto dstHandle = m_TextureDescriptorHeap.GetHandleAtOffset(matId * numTexturesPerMat);
				for (uint32_t texIdx = 0; texIdx < numTexturesPerMat; ++texIdx)
				{
					pDevice->CopyDescriptorsSimple(1, dstHandle.GetCpuHandle(), srcTexHandles[texIdx], DescriptorHeapType);
					dstHandle += srvDescriptorStepSize;
				}
			#else
				// Frame descriptor heaps
				for (uint32_t texIdx = 0; texIdx < numTexturesPerMat; texIdx++)
				{
					m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, srcTexHandles[texIdx]);
				}
			#endif
			}
		}

		// decal textures
		{
			uint32_t numDecalTextures = bindlessDeferred.NumDecalTextures();
			auto pDecalSRVs = bindlessDeferred.DecalSRVs();

			auto& decalTextureRange = s_DescriptorRanges[DescriptorParams::DecalTextures];
			decalTextureRange.start = m_FrameDescriptorHeap.PersistentAllocated();
			decalTextureRange.count = numDecalTextures;

			for (uint32_t i = 0; i < numDecalTextures; i++)
				m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, pDecalSRVs[i]);
		}

		// render textures
		{
			auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
			auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;

			auto& tangentFrame = bindlessDeferred.m_TangentFrame;
			auto& uvTarget = bindlessDeferred.m_UVTarget;
			auto& uvGradientsTarget = bindlessDeferred.m_UVGradientsTarget;
			auto& materialIDTarget = bindlessDeferred.m_MaterialIDTarget;
			auto& shadowMap = cascadedShadowMap.m_LightShadowArray;

			auto &visibilityBuffer = bindlessDeferred.m_VisibilityBuffer;

			auto& rtSRVRange = s_DescriptorRanges[DescriptorParams::RenderTextureSRVs];
			rtSRVRange.start = m_FrameDescriptorHeap.PersistentAllocated();
			rtSRVRange.count = RenderTextureSRVs::Count;

			m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, colorBuffer.GetSRV());
			m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, depthBuffer.GetDepthSRV());
			m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, tangentFrame.GetSRV());
			m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, uvTarget.GetSRV());
			m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, uvGradientsTarget.GetSRV());
			m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, materialIDTarget.GetSRV());
			m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, shadowMap.GetSRV());
			m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, visibilityBuffer.GetSRV());

			auto &rtUAVRange = s_DescriptorRanges[DescriptorParams::RenderTextureUAVs];
			rtUAVRange.start = m_FrameDescriptorHeap.PersistentAllocated();
			rtUAVRange.count = RenderTextureUAVs::Count;

			m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, colorBuffer.GetUAV());
		}

		// buffers
		{
			auto &materialBuffer = m_MaterialsBuffer;
			auto &meshBuffer = m_MeshesBuffer;
			auto &meshInstanceBuffer = m_MeshInstancesBuffer;
			const auto *vertexBuffer = m_VertexBuffer.get();
			const auto *indexBuffer = m_IndexBuffer.get();

			auto& bufferSRVRange = s_DescriptorRanges[DescriptorParams::BufferSRVs];
			bufferSRVRange.start = m_FrameDescriptorHeap.PersistentAllocated();
			bufferSRVRange.count = BufferSRVs::Count-1;

			// m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, matrixBuffer.Create);
			m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, materialBuffer.GetSRV());
			m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, meshBuffer.GetSRV());
			m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, meshInstanceBuffer.GetSRV());
			m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, vertexBuffer->GetSRV());
			m_FrameDescriptorHeap.AllocAndCopyPersistentDescriptor(pDevice, indexBuffer->GetSRV());
		}
	}

	void Scene::InitCamera(GameInput* pInput)
	{
		if (m_Camera == nullptr)
			m_Camera = std::make_shared<Math::Camera>();
		ResetCamera(true);
		m_Camera->Update();

		if (pInput != nullptr)
		{
			m_pInput = pInput;
			m_CameraController.reset(new CameraController(*m_Camera, Math::Vector3(Math::kYUnitVector), *m_pInput));
		}

		SaveNewViewport();
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
		m_MaskInstances.reserve(numInstance);
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
					curVec = &m_MaskInstances;
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
		std::vector<uint16_t> drawIds(drawCount, 0);
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
				globalMat = m_GlobalMatrices[parentIdx] * globalMat;
			}
			m_InvTransposeGlobalMatrices[i] = MMATH::transpose(MMATH::inverse(globalMat));
			++i;
		} );
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

			// Copy a single
			// m_BoundsDynamicBuffer.CopyToGpu((void*)&worldBB, sizeof(BoundingBox), instanceIndex);
			// ++instanceIndex;
		}
		// Copy as total
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

			auto &drawArgs = drawCommands[instanceId].drawArgs;
			drawArgs.IndexCountPerInstance = curMesh.indexCount;
			drawArgs.InstanceCount = 1;
			drawArgs.StartIndexLocation = curMesh.indexByteOffset / curMesh.indexStrideSize;
			drawArgs.BaseVertexLocation = curMesh.vertexOffset;
			drawArgs.StartInstanceLocation = instanceId;
		}

		m_CommandsBuffer.Create(pDevice, L"CommandBuffer", drawCount, sizeof(IndirectCommand), drawCommands.data());

		// frustum cull
		m_FrustumCulledBuffer.Create(pDevice, L"FrustumCulledBuffer", drawCount, sizeof(IndirectCommand));
		m_FrustumCulledMaskBuffer.Create(pDevice, L"FrustumCulledMaskBuffer", drawCount, sizeof(IndirectCommand));
		m_FrustumCulledTransparentBuffer.Create(pDevice, L"FrustumCulledTransparentBuffer", drawCount, sizeof(IndirectCommand));

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
