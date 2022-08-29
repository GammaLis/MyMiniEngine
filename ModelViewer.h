#pragma once
#include "IGameApp.h"
#include "Camera.h"
#include "ShadowCamera.h"
#include "CameraController.h"
#include "DescriptorHeap.h"
#include "ColorBuffer.h"
#include "GameInput.h"
#include "Skybox.h"

/// Raytracing
#include "ReservoirSampling.h"

#include <atlbase.h>

namespace MyDirectX
{
	class GraphicsContext;

	enum class RSId
	{
		kMeshConstants = 0,
		kMaterialConstants,
		kMaterialSRVs,
		kMaterialSamplers,
		kCommonCBV,
		kCommonSRVs,
		kSkinMatrices,

		kNum
	};

	/// Raytracing
	enum class RaytracingType
	{
		Primarybarycentric = 0,
		Reflectionbarycentric,
		Shadows,
		DiffuseHitShader,
		Reflection,

		ReferencePathTracing,
		ReSTIRWithDirectLights,

		Num
	};

	enum class RaytracingMode
	{
		Off,
		Traversal,
		SSR,
		Shadows,
		DiffuseWithShadowMaps,
		DiffuseWithShadowRays,
		Reflections,
		ReferencePathTracing,
		ReSTIRWithDirectLights
	};

	struct RaytracingDispatchRayInputs
	{
		RaytracingDispatchRayInputs() {  }
		RaytracingDispatchRayInputs(ID3D12Device* pDevice, ID3D12StateObject* pPSO,
			void* pHitGroupShaderTable, UINT HitGroupStride, UINT HitGroupTableSize,
			LPCWSTR rayGenExportName, LPCWSTR missExportName);

		void Cleanup();

		D3D12_DISPATCH_RAYS_DESC GetDispatchRayDesc(UINT DispatchWidth, UINT DispatchHeight);

		UINT m_HitGroupStride = 0;
		CComPtr<ID3D12StateObject> m_pPSO;
		ByteAddressBuffer m_RayGenShaderTable;
		ByteAddressBuffer m_MissShaderTable;
		ByteAddressBuffer m_HitShaderTable;

	};

	class ModelViewer final : public IGameApp
	{
	public:
		ModelViewer(HINSTANCE hInstance, const char *modelName, const wchar_t* title = L"Hello, World!", UINT width = SCR_WIDTH, UINT height = SCR_HEIGHT);

		virtual void Update(float deltaTime) override;
		virtual void Render() override;

		virtual void Raytrace(GraphicsContext &gfxContext);

		struct CommonStates
		{
			float SunLightIntensity = 4.0f;	// 0.0 - 16.0
			float AmbientIntensity = 0.1f;	// 
			float SunOrientation = -0.5f;	// -100.0 - 100.0
			float SunInclination = 0.75f;	// 0.0 - 1.0
			float ShadowDimX = 5000;		// 1000 - 10000
			float ShadowDimY = 3000;		// 1000 - 10000
			float ShadowDimZ = 3000;		// 1000 - 10000
		};
		CommonStates m_CommonStates;

		static const UINT c_MaxRayRecursion = 2;
		static const UINT c_MaxPayloadSize = 64;
		static const UINT c_MaxAttributeSize = 8;

	private:
		virtual void InitPipelineStates() override;
		virtual void InitGeometryBuffers() override;
		virtual void InitCustom() override;
		virtual void CleanCustom() override;

		virtual void PostProcess() override;

		void RenderLightShadows(GraphicsContext& gfxContext);
		void RenderObjects(GraphicsContext& gfxContext, const Math::Matrix4 viewProjMat, ObjectFilter filter = ObjectFilter::kAll);
		void CreateParticleEffects();

		Math::Camera m_Camera;
		std::unique_ptr<CameraController> m_CameraController;
		Math::Matrix4 m_ViewProjMatrix;

		// Root signature & PSOs
		RootSignature m_RootSig;
		GraphicsPSO m_DepthPSO{ L"Depth PSO" };
		GraphicsPSO m_CutoutDepthPSO{ L"Cutout Depth PSO" };
		GraphicsPSO m_ModelPSO{ L"Color PSO" };
		GraphicsPSO m_CutoutModelPSO{ L"Cutout Color PSO" };
		GraphicsPSO m_ShadowPSO{ L"Shadow PSO" };
		GraphicsPSO m_CutoutShadowPSO{ L"Cutout Shadow PSO" };

		// 临时设置，后面需要移到别处 -20-2-21
		RootSignature m_LinearDepthRS;
		ComputePSO m_LinearDepthCS;

		Math::Vector3 m_SunDirection;
		ShadowCamera m_SunShadow;

		D3D12_CPU_DESCRIPTOR_HANDLE m_ExtraTextures[6];

		void InitRaytracing();
		void InitRaytracingViews(ID3D12Device* pDevice);
		void InitRaytracingAS(ID3D12Device* pDevice);
		void InitRaytracingStateObjects(ID3D12Device* pDevice);
		void CleanRaytracing();

		void RaytraceBarycentrics(CommandContext& context);
		void RaytraceBarycentricsSSR(CommandContext& context);
		void RaytraceDiffuse(GraphicsContext& gfxContext);
		void RaytraceShadows(GraphicsContext& gfxContext);
		void RaytraceReflections(GraphicsContext& gfxContext);
		void ReferencePathTracing(GraphicsContext& gfxContext);
		void ReSTIRWithDirectLights(GraphicsContext& gfxContext);

		CComPtr<ID3D12Device5> m_RaytracingDevice;
		std::vector<CComPtr<ID3D12Resource>> m_BLAS;
		CComPtr<ID3D12Resource> m_TLAS;
		RootSignature m_GlobalRaytracingRS;
		RootSignature m_LocalRaytracingRS;

		UserDescriptorHeap m_RaytracingDescHeap;
		// UAVs & SRVs
		D3D12_GPU_DESCRIPTOR_HANDLE m_OutColorUAV;
		D3D12_GPU_DESCRIPTOR_HANDLE m_DepthAndNormalsTable;
		D3D12_GPU_DESCRIPTOR_HANDLE m_SceneSrvs;
		D3D12_GPU_DESCRIPTOR_HANDLE m_LightBufferSrv;
		// Meshes
		D3D12_GPU_DESCRIPTOR_HANDLE m_GpuMeshInfo; // Not used now
		// Texture srv descriptors
		D3D12_GPU_DESCRIPTOR_HANDLE m_GpuSceneMaterialSrvs[32];

		RaytracingDispatchRayInputs m_RaytracingInputs[(uint32_t)RaytracingType::Num];
		D3D12_CPU_DESCRIPTOR_HANDLE m_BVHAttribSrvs[40];

		// Ray tracing constants
		ByteAddressBuffer m_HitConstantBuffer;
		ByteAddressBuffer m_DynamicConstantBuffer;

		ComputePSO m_BufferCopyPSO{ L"Copy Buffer PSO"};
		ComputePSO m_ClearBufferPSO{ L"Clear Buffer PSO" };
		ColorBuffer m_AccumulationBuffer;
		DescriptorHandle m_AccumulationBufferUAV;
		DescriptorHandle m_AccumulationBufferSRV;
		int m_AccumulationIndex = -1;

		// Reservoir sampling
		ComputePSO m_ClearReservoirPSO{ L"Clear Reservoir PSO" };
		ComputePSO m_TemporalReservoirReusePSO{ L"Temporal Reuse PSO" };
		ComputePSO m_SpatialReservoirReusePSO{ L"Spatial Reuse PSO" };
		StructuredBuffer m_ReservoirBuffer[2];
		StructuredBuffer m_IntermediateReservoirBuffer; // TODO...
		DescriptorHandle m_ReservoirBufferUAV;
		DescriptorHandle m_ReservoirBufferSRV;
		bool m_bNeedClearReservoirs = true;
		
		// Skybox
		Skybox m_Skybox;

		const char* m_ModelName = nullptr;
	};

}
