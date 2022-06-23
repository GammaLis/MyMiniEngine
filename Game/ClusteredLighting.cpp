#include "ClusteredLighting.h"
#include "Math/Random.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "GfxCommon.h"
#include "Camera.h"

// compiled shader bytecode
#include "FillLightClusterCS.h"

using namespace MyDirectX;
using namespace Math;

struct CSConstants
{
	uint32_t _ViewportWidth, _ViewportHeight;
	uint32_t _TileSizeX, _TileSizeY;
	float _NearClip, _FarClip;
	Matrix4 _ViewProjMat;
	Matrix4 _ViewMat;
	Matrix4 _ProjMat;
};

void ClusteredLighting::Init(ID3D12Device* pDevice)
{
	// root signature
	{
		m_FillLightRS.Reset(3);
		m_FillLightRS[0].InitAsConstantBuffer(0);
		m_FillLightRS[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 2);
		m_FillLightRS[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		m_FillLightRS.Finalize(pDevice, L"FillLightClusterRS");
	}

	// PSO
	{
		m_FillLightClusterPSO.SetRootSignature(m_FillLightRS);
		m_FillLightClusterPSO.SetComputeShader(FillLightClusterCS, sizeof(FillLightClusterCS));
		m_FillLightClusterPSO.Finalize(pDevice);
	}
}

void ClusteredLighting::CreateRandomLights(ID3D12Device* pDevice, const Math::Vector3 minBound, const Math::Vector3 maxBound)
{
	using namespace Math;

	Vector3 posScale = maxBound - minBound;
	Vector3 posBias = minBound;

	RandomNumberGenerator rng;
	// rng.SetSeed(1);

	auto randVecUniform = [&rng]() -> Vector3
	{
		return Vector3(rng.NextFloat(), rng.NextFloat(), rng.NextFloat());
	};
	auto randGaussian = [&rng]() -> float
	{
		// polar box-muller
		// X = cos(2Pi * U1) * sqrt(-2In(U2))
		// Y = sin(2Pi * U1) * sqrt(-2In(U2))
		static bool gaussianPair = true;
		static float y2;

		if (gaussianPair)
		{
			gaussianPair = false;

			float x1, x2, w;
			do
			{
				x1 = 2 * rng.NextFloat() - 1;
				x2 = 2 * rng.NextFloat() - 1;
				w = x1 * x1 + x2 * x2;
			} while (w >= 1);

			w = sqrt(-2 * log(w) / w);
			y2 = x2 * w;
			return x1 * w;
		}
		else
		{
			gaussianPair = true;
			return y2;
		}
	};
	auto randVecGaussian = [&randGaussian]() -> Vector3
	{
		return Normalize(Vector3(randGaussian(), randGaussian(), randGaussian()));
	};

	for (uint32_t n = 0; n < MaxLights; ++n)
	{
		Vector3 pos = randVecUniform() * posScale + posBias;
		float lightRadius = rng.NextFloat() * 800.0f + 200.0f;

		Vector3 color = randVecUniform();
		float colorScale = rng.NextFloat() * 0.3f + 0.3f;
		color = color * colorScale;

		uint32_t type;
		// force types to match 32-bit boundaries for the BIT_MASK_SORTED case
		// 0->31: type 0, 32->32*3-1: type 1, 32*3->+MaxLights: type 2 
		if (n < 32 * 1)
			type = 0;
		else if (n < 32 * 3)
			type = 1;
		else
			type = 2;

		Vector3 coneDir = randVecGaussian();
		float coneInner = (rng.NextFloat() * 0.2f + 0.025f) * Math::Pi;
		float coneOuter = coneInner + rng.NextFloat() * 0.1f * Math::Pi;

		if (type == 1 || type == 2)
		{
			// emphasize cone lights
			color = color * 5.0f;
		}

		m_LightData[n].position = XMFLOAT3(pos.GetX(), pos.GetY(), pos.GetZ());
		m_LightData[n].radiusSq = lightRadius * lightRadius;
		m_LightData[n].color = XMFLOAT3(color.GetX(), color.GetY(), color.GetZ());
		m_LightData[n].type = type;
		m_LightData[n].coneDir = XMFLOAT3(coneDir.GetX(), coneDir.GetY(), coneDir.GetZ());
		m_LightData[n].coneAngles = XMFLOAT2(1.0f / (cos(coneInner) - cos(coneOuter)), cos(coneOuter));
		// WARNING:不能存为Math::Matrix4 - 16字节对齐 SIMD指令 ！！！
		// m_LightData[n].shadowTextureMatrix = DirectX::XMMATRIX(Transpose(shadowTextureMatrix));
		// DirectX::XMStoreFloat4x4(&m_LightData[n].shadowTextureMatrix, DirectX::XMMATRIX(Transpose(shadowTextureMatrix)));
	}

	// 
	for (uint32_t n = 0; n < MaxLights; ++n)
	{
		if (m_LightData[n].type == 1)
		{
			m_FirstConeLight = n;
			break;
		}
	}
	for (uint32_t n = 0; n < MaxLights; ++n)
	{
		if (m_LightData[n].type == 2)
		{
			m_FirstConeShadowedLight = n;
			break;
		}
	}

	// create light buffer
	m_LightBuffer.Create(pDevice, L"m_LightBuffer", MaxLights, sizeof(LightData), m_LightData);

	// assumes max resolution of 1920x1080
	uint32_t maxWidth = 1920, maxHeight = 1080;
	// light clusters 最大数量
	// [1920 / 16] * [1080 / 16] * 16 = 120 * 67 * 16 = 128,640
	uint32_t lightGridCells = Math::DivideByMultiple(maxWidth, MinLightGridDim) * Math::DivideByMultiple(maxHeight, MinLightGridDim);
	uint32_t lightClusters = m_NumDepthSlice * lightGridCells;
	uint32_t lightGridSizeBytes = lightClusters * (4 + MaxLights * 4);
	m_LightCluster.Create(pDevice, L"m_LightCluster", lightGridSizeBytes, 1, nullptr);

	uint32_t lightGridBitMaskSizeBytes = lightClusters * 4 * 4;	// 4 uints
	// m_LightGridBitMask.Create(pDevice, L"m_LightGridBitMask", lightGridBitMaskSizeBytes, 1, nullptr);
}

void ClusteredLighting::FillLightCluster(GraphicsContext& gfxContext, const Math::Camera& camera, uint64_t frameIndex)
{
	auto& context = gfxContext.GetComputeContext();

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;

	context.TransitionResource(m_LightBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(m_LightCluster, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	context.SetRootSignature(m_FillLightRS);
	context.SetPipelineState(m_FillLightClusterPSO);
	
	uint32_t width = colorBuffer.GetWidth(), height = colorBuffer.GetHeight();
	uint32_t tileCountX = Math::DivideByMultiple(width, m_LightGridDim);
	uint32_t tileCountY = Math::DivideByMultiple(height, m_LightGridDim);

	CSConstants csConstants;
	csConstants._ViewportWidth = width;
	csConstants._ViewportHeight = height;
	csConstants._TileSizeX = csConstants._TileSizeY = m_LightGridDim;
	csConstants._NearClip = camera.GetNearClip();
	csConstants._FarClip = camera.GetFarClip();
	csConstants._ViewProjMat = Math::Transpose(camera.GetViewProjMatrix());
	csConstants._ViewMat = Math::Transpose(camera.GetViewMatrix());
	csConstants._ProjMat = Math::Transpose(camera.GetProjMatrix());

	context.SetDynamicConstantBufferView(0, sizeof(CSConstants), &csConstants);

	context.SetDynamicDescriptor(1, 0, m_LightBuffer.GetSRV());
	context.SetDynamicDescriptor(1, 1, depthBuffer.GetDepthSRV());
	context.SetDynamicDescriptor(2, 0, m_LightCluster.GetUAV());

	context.Dispatch(tileCountX, tileCountY);

	context.TransitionResource(m_LightCluster, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

}

void ClusteredLighting::Shutdown()
{
	m_LightBuffer.Destroy();
	m_LightCluster.Destroy();
}
