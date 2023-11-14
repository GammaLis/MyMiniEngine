#pragma once
#include "pch.h"
#include "Scenes/LightDefines.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "GpuBuffer.h"

namespace Math
{
	class Camera;
}

namespace MyDirectX
{
	class GraphicsContext;
	class ClusteredLighting
	{
	public:
		static const unsigned MaxLights = 128;
		static const unsigned MinLightGridDim = 16;

		void Init(ID3D12Device* pDevice);
		void CreateRandomLights(ID3D12Device* pDevice, const Math::Vector3 minBound, const Math::Vector3 maxBound);
		void FillLightCluster(GraphicsContext& gfxContext, const Math::Camera& camera, uint64_t frameIndex);
		void Shutdown();

		// must keep in sync with HLSL
		struct LightData
		{
			DirectX::XMFLOAT3 position;
			float radiusSq;
			DirectX::XMFLOAT3 color;
			uint32_t type;

			DirectX::XMFLOAT3 coneDir;
			DirectX::XMFLOAT2 coneAngles;
			DirectX::XMFLOAT4X4 shadowTextureMatrix;
		};
		LightData m_LightData[MaxLights]{};

		uint32_t m_LightGridDim = 16;
		uint32_t m_NumDepthSlice = 16;

		// light data
		StructuredBuffer m_LightBuffer;
		ByteAddressBuffer m_LightCluster;
		ByteAddressBuffer m_LightGridBitMask;

		uint32_t m_FirstConeLight = 0;
		uint32_t m_FirstConeShadowedLight = 0;

	private:
		RootSignature m_FillLightRS;
		ComputePSO m_FillLightClusterPSO;

	};

}
