#pragma once
#include "IGameApp.h"
#include "DescriptorHeap.h"
#include "ColorBuffer.h"
#include "DynamicUploadBuffer.h"
#include "GameInput.h"
#include "ModelViewerRayInputs.h"

namespace Math 
{
	class Camera;
}

namespace MyDirectX
{
	class GraphicsContext;
	class CameraController;
	class ShadowCamera;

	class DebugPass;
	class Skybox;
	class ReSTIRGI;

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
		ReSTIRWithDirectLights,
		ReSTIRGI
	};

	enum class RTGlobalRSId
	{
		ViewUniforms = 0,
		SceneBuffers,
		HitConstants,
		DynamicCB,
		InputTextures,
		Outputs,
		U0,
		U1,
		AccelerationStructure,

		ReSTIRGINewSamples,
		ReSTIRGISPSamples,
		ReSTIRGIOutputs,

		Num
	};

	class ModelViewer final : public IGameApp
	{
	public:
		ModelViewer(HINSTANCE hInstance, const char *modelName, const wchar_t* title = L"Hello, World!", UINT width = SCR_WIDTH, UINT height = SCR_HEIGHT);
		// std::unique_ptr deleter is invoked, T is incomplete
		~ModelViewer();

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

		static constexpr UINT c_MaxRayRecursion = 2;
		static constexpr UINT c_MaxPayloadSize = 64;
		static constexpr UINT c_MaxAttributeSize = 8;
		static constexpr UINT INDEX_NONE = std::numeric_limits<UINT>::max();

		using BatchElements = std::vector<uint32_t>;

	private:
		virtual void InitPipelineStates() override;
		virtual void InitGeometryBuffers() override;
		virtual void InitCustom() override;
		virtual void CleanCustom() override;

		virtual void PostProcess() override;
		virtual void CustomUI(GraphicsContext &context) override;

		void RenderLightShadows(GraphicsContext& gfxContext);
		void RenderObjects(GraphicsContext& gfxContext, const Math::Matrix4 &viewProjMat, ObjectFilter filter = ObjectFilter::kAll, uint32_t cullingIndex = INDEX_NONE);
		void RenderMultiViewportObjects(GraphicsContext& gfxContext, const std::vector<Math::Matrix4> &viewProjMats, const BatchElements &renderBatch, ObjectFilter filter = ObjectFilter::kAll);
		void CreateParticleEffects();

		/**
		* Ref: std::unique_ptr
		* https://en.cppreference.com/w/cpp/memory/unique_ptr
		* 
		* std::unique_ptr may be constructed for an incomplete type T, such as to facilitate the use as a handle in the pImpl idiom. 
		* If the default deleter is used, T must be complete at the point in code where the deleter is invoked, which happens in the destructor, 
		* move assignment operator, and reset member function of std::unique_ptr.
		*/
		std::unique_ptr<Math::Camera> m_Camera;
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
		GraphicsPSO m_PointLightShadowPSO{ L"Point Light Shadow PSO"};

		// TODO: put somewhere else, -20-2-21
		RootSignature m_LinearDepthRS;
		ComputePSO m_LinearDepthCS;

		Math::Vector3 m_SunDirection;
		std::unique_ptr<ShadowCamera> m_SunShadow;

		D3D12_CPU_DESCRIPTOR_HANDLE m_ExtraTextures[7] = {};

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
		void RaytraceReSTIRGI(GraphicsContext& gfxContext);

		ComPtr<ID3D12Device5> m_RaytracingDevice;
		std::vector<ComPtr<ID3D12Resource>> m_BLAS;
		ComPtr<ID3D12Resource> m_TLAS;
		RootSignature m_GlobalRaytracingRS;
		RootSignature m_LocalRaytracingRS;

		UserDescriptorHeap m_RaytracingDescHeap;
		// UAVs & SRVs
		D3D12_GPU_DESCRIPTOR_HANDLE m_OutColorUAV{};
		D3D12_GPU_DESCRIPTOR_HANDLE m_DepthAndNormalsTable{};
		D3D12_GPU_DESCRIPTOR_HANDLE m_SceneSrvs{};
		D3D12_GPU_DESCRIPTOR_HANDLE m_LightBufferSrv{};
		// Meshes
		D3D12_GPU_DESCRIPTOR_HANDLE m_GpuMeshInfo{}; // Not used now
		// Texture srv descriptors
		D3D12_GPU_DESCRIPTOR_HANDLE m_GpuSceneMaterialSrvs[32]{};
		
		// Default all elements
		BatchElements m_DefaultBatchList;
		// Batch list per pass
		std::vector<BatchElements> m_PassCullingResults;
		// Fixed indices
		uint32_t m_MainCullingIndex{ INDEX_NONE };
		uint32_t m_MainLightCullingIndex{ INDEX_NONE };
		uint32_t m_PointLightCullingIndex{ INDEX_NONE };
		// Main light shadow culling efficiency is quite low
		bool m_bCullMainLightShadow = false;

		RaytracingDispatchRayInputs m_RaytracingInputs[(uint32_t)RaytracingType::Num];
		D3D12_CPU_DESCRIPTOR_HANDLE m_BVHAttribSrvs[40]{};

		// Ray tracing constants
		ByteAddressBuffer m_HitConstantBuffer;
		ByteAddressBuffer m_DynamicConstantBuffer;

		ComputePSO m_BufferCopyPSO{ L"Copy Buffer PSO"};
		ComputePSO m_ClearBufferPSO{ L"Clear Buffer PSO" };
		ColorBuffer m_AccumulationBuffer;
		DescriptorHandle m_AccumulationBufferUAV;
		DescriptorHandle m_AccumulationBufferSRV;
		int m_AccumulationIndex = -1;
		bool m_bEnablePathTracing = true;

		// Reservoir sampling
		ComputePSO m_ClearReservoirPSO{ L"Clear Reservoir PSO" };
		ComputePSO m_TemporalReservoirReusePSO{ L"Temporal Reuse PSO" };
		ComputePSO m_SpatialReservoirReusePSO{ L"Spatial Reuse PSO" };
		StructuredBuffer m_ReservoirBuffer[2];
		StructuredBuffer m_IntermediateReservoirBuffer; // TODO...
		DescriptorHandle m_ReservoirBufferUAV;
		DescriptorHandle m_ReservoirBufferSRV;
		bool m_bNeedClearReservoirs = true;
		bool m_bEnableReSTIRDI = true;

		// ReSTIR GI
		std::unique_ptr<ReSTIRGI> m_ReSTIRGI;
		bool m_bEnableReSTIRGI = true;
		
		// Skybox
		std::unique_ptr<Skybox> m_Skybox;

		// DEBUG
		std::unique_ptr<DebugPass> m_DebugPass;
		bool m_bEnableDebug = false;

		const char* m_ModelName = nullptr;
	};

}
