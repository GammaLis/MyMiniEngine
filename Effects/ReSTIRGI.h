#pragma once
#include "RootSignature.h"
#include "PipelineState.h"
#include "ColorBuffer.h"
#include "DescriptorHeap.h"
#include "ModelViewerRayInputs.h"

namespace MyDirectX
{
	class UserDescriptorHeap;
	class ComputeContext;

	class ReSTIRGI
	{
		friend class ModelViewer;

	public:
		void Init(ID3D12Device* pDevice, const RootSignature &rootSig);
		void Shutdown();

		void Update();
		void Render(ComputeContext &context);

		bool IsHalfSize() const { return m_bHalfSize; }
		D3D12_DISPATCH_RAYS_DESC GetDispatchRayDesc(UINT width, UINT height);

	private:
		void InitUAVs(ID3D12Device* pDevice, UserDescriptorHeap& descHeap);
		void InitSRVs(ID3D12Device* pDevice, UserDescriptorHeap& descHeap);

		// RootSignature & Pipelines
		RootSignature m_RootSig;

		ComputePSO m_RaytraceDiffusePSO{ L"Raytrace Diffuse" }; // not use yet
		ComputePSO m_TemporalResamplingPSO{ L"Temporal Resampling" };
		ComputePSO m_SpatialResamplingPSO{ L"Spatial Resampling" };
		ComputePSO m_ResolvePSO{ L"Resolve" };

		// Pingpong buffers
		ColorBuffer m_TemporalSampleRadiance[2];
		ColorBuffer m_TemporalSampleNormal[2];
		ColorBuffer m_TemporalSampleHitInfo[2];
		ColorBuffer m_TemporalRayOrigin[2];
		ColorBuffer m_TemporalReservoir[2];

		ColorBuffer m_SampleRadiance;
		ColorBuffer m_SampleNormal;
		ColorBuffer m_SampleHitInfo;

		ColorBuffer m_Irradiance;

		DescriptorHandle m_SampleRadianceUAV;
		DescriptorHandle m_TemporalSampleRadianceUAV;	// Radiance, Normal, HitInfo, RayOrigin, Reservoir
		DescriptorHandle m_SpatialSampleRadianceUAV;	// Radiance, Normal, HitInfo, RayOrigin, Reservoir
		DescriptorHandle m_IrradianceUAV;

		DescriptorHandle m_SampleRadianceSRV;
		DescriptorHandle m_TemporalSampleRadianceSRV;	// Radiance, Normal, HitInfo, RayOrigin, Reservoir
		DescriptorHandle m_SpatialSampleRadianceSRV;	// Radiance, Normal, HitInfo, RayOrigin, Reservoir
		DescriptorHandle m_ResolveSampleRadianceSRV;

		RaytracingDispatchRayInputs m_ReSTIRGIInputs;

		bool m_bHalfSize = true;
		bool m_bInited = false;
		bool m_bClearHistory = true;

	};
}
