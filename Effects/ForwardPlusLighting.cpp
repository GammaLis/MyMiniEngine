#include "ForwardPlusLighting.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "GfxCommon.h"
#include "Math/Random.h"
#include "Camera.h"

// compiled CS
#include "FillLightGridCS_8.h"
#include "FillLightGridCS_16.h"
#include "FillLightGridCS_24.h"
#include "FillLightGridCS_32.h"

namespace MyDirectX
{
	using namespace DirectX;
	using namespace Math;

	struct CSConstants
	{
		uint32_t _ViewportWidth, _ViewportHeight;
		float _InvTileDim;
		float _RcpZMagic;
		uint32_t _TileCount;
		//uint32_t _Padding[3];	// Matrix4 已经128位(16字节)对齐，不用补齐 -20-2-17
		Matrix4 _ViewProjMat; 
	};

	void ForwardPlusLighting::Init(ID3D12Device* pDevice)
	{
		// root signature
		{
			m_FillLightRS.Reset(3, 0);
			m_FillLightRS[0].InitAsConstantBuffer(0);
			m_FillLightRS[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 2);
			m_FillLightRS[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 2);
			m_FillLightRS.Finalize(pDevice, L"FillLightRS");
		}

		// PSOs
		{
			// 8
			m_FillLightGridCS_8.SetRootSignature(m_FillLightRS);
			m_FillLightGridCS_8.SetComputeShader(CD3DX12_SHADER_BYTECODE(FillLightGridCS_8, sizeof(FillLightGridCS_8)));
			m_FillLightGridCS_8.Finalize(pDevice);

			// 16
			m_FillLightGridCS_16.SetRootSignature(m_FillLightRS);
			m_FillLightGridCS_16.SetComputeShader(CD3DX12_SHADER_BYTECODE(FillLightGridCS_16, sizeof(FillLightGridCS_16)));
			m_FillLightGridCS_16.Finalize(pDevice);

			// 24
			m_FillLightGridCS_24.SetRootSignature(m_FillLightRS);
			m_FillLightGridCS_24.SetComputeShader(CD3DX12_SHADER_BYTECODE(FillLightGridCS_24, sizeof(FillLightGridCS_24)));
			m_FillLightGridCS_24.Finalize(pDevice);

			// 32
			m_FillLightGridCS_32.SetRootSignature(m_FillLightRS);
			m_FillLightGridCS_32.SetComputeShader(CD3DX12_SHADER_BYTECODE(FillLightGridCS_32, sizeof(FillLightGridCS_32)));
			m_FillLightGridCS_32.Finalize(pDevice);
		}
	}

	void ForwardPlusLighting::CreateRandomLights(ID3D12Device* pDevice, const Math::Vector3 minBound, const Math::Vector3 maxBound)
	{
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

			Camera shadowCamera;
			shadowCamera.SetEyeAtUp(pos, pos + coneDir, Vector3(0, 1, 0));
			shadowCamera.SetPerspectiveMatrix(coneOuter * 2, 1.0f, lightRadius * 0.05f, lightRadius * 1.0f);
			shadowCamera.Update();
			m_LightShadowMatrix[n] = shadowCamera.GetViewProjMatrix();
			Matrix4 shadowTextureMatrix = Matrix4(AffineTransform(Matrix3::MakeScale(0.5f, -0.5f, 1.0f), Vector3(0.5f, 0.5f, 0.0f))) *
				m_LightShadowMatrix[n];

			m_LightData[n].position = XMFLOAT3(pos.GetX(), pos.GetY(), pos.GetZ());
			m_LightData[n].radiusSq = lightRadius * lightRadius;
			m_LightData[n].color = XMFLOAT3(color.GetX(), color.GetY(), color.GetZ());
			m_LightData[n].type = type;
			m_LightData[n].coneDir = XMFLOAT3(coneDir.GetX(), coneDir.GetY(), coneDir.GetZ());
			m_LightData[n].coneAngles = XMFLOAT2(1.0f / (cos(coneInner) - cos(coneOuter)), cos(coneOuter));
			// WARNING:不能存为Math::Matrix4 - 16字节对齐 SIMD指令 ！！！
			// m_LightData[n].shadowTextureMatrix = DirectX::XMMATRIX(Transpose(shadowTextureMatrix));
			DirectX::XMStoreFloat4x4(&m_LightData[n].shadowTextureMatrix, DirectX::XMMATRIX(Transpose(shadowTextureMatrix)));
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
		// light grid cells 最大数量
		uint32_t lightGridCells = Math::DivideByMultiple(maxWidth, MinLightGridDim) * Math::DivideByMultiple(maxHeight, MinLightGridDim);
		uint32_t lightGridSizeBytes = lightGridCells * (4 + MaxLights * 4);
		m_LightGrid.Create(pDevice, L"m_LightGrid", lightGridSizeBytes, 1, nullptr);

		uint32_t lightGridBitMaskSizeBytes = lightGridCells * 4 * 4;	// 4 uints
		m_LightGridBitMask.Create(pDevice, L"m_LightGridBitMask", lightGridBitMaskSizeBytes, 1, nullptr);

		m_LightShadowArray.CreateArray(pDevice, L"m_LightShadowArray", m_ShadowDim, m_ShadowDim, MaxLights, DXGI_FORMAT_R16_UNORM);
		m_LightShadowTempBuffer.Create(pDevice, L"m_LightShadowTempBuffer", m_ShadowDim, m_ShadowDim);
	}

	void ForwardPlusLighting::FillLightGrid(GraphicsContext& gfxContext, const Math::Camera& camera, uint64_t frameIndex)
	{
		ComputeContext& context = gfxContext.GetComputeContext();

		context.SetRootSignature(m_FillLightRS);

		switch (m_LightGridDim)
		{
		case 8:	context.SetPipelineState(m_FillLightGridCS_8); break;
		default:
		case 16:context.SetPipelineState(m_FillLightGridCS_16); break;
		case 24:context.SetPipelineState(m_FillLightGridCS_24); break;
		case 32:context.SetPipelineState(m_FillLightGridCS_32); break;
		}

		ColorBuffer& linearDepth = Graphics::s_BufferManager.m_LinearDepth[frameIndex % 2];
		ColorBuffer& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
		DepthBuffer& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;		

		context.TransitionResource(m_LightBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		context.TransitionResource(linearDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		context.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		context.TransitionResource(m_LightGrid, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.TransitionResource(m_LightGridBitMask, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		// rootIndex 1 - 2 SRVs
		context.SetDynamicDescriptor(1, 0, m_LightBuffer.GetSRV());
		// 目前 没有实现LinearDepth -20-2-17
		// context.SetDynamicDescriptor(1, 1, linearDepth.GetSRV());
		context.SetDynamicDescriptor(1, 1, depthBuffer.GetDepthSRV());

		// rootIndex 2 - 2 UAVs
		context.SetDynamicDescriptor(2, 0, m_LightGrid.GetUAV());
		context.SetDynamicDescriptor(2, 1, m_LightGridBitMask.GetUAV());

		// assumes 1920x1080 resolution
		uint32_t tileCountX = Math::DivideByMultiple(colorBuffer.GetWidth(), m_LightGridDim);
		uint32_t tileCountY = Math::DivideByMultiple(colorBuffer.GetHeight(), m_LightGridDim);

		float farClipDist = camera.GetFarClip();
		float nearClipDist = camera.GetNearClip();
		const float rcpZMagic = nearClipDist / (farClipDist - nearClipDist);

		CSConstants csConstants;
		csConstants._ViewportWidth = colorBuffer.GetWidth();
		csConstants._ViewportHeight = colorBuffer.GetHeight();
		csConstants._InvTileDim = 1.0f / m_LightGridDim;
		csConstants._RcpZMagic = rcpZMagic;
		csConstants._TileCount = tileCountX;
		csConstants._ViewProjMat = Math::Transpose(camera.GetViewProjMatrix());

		// rootIndex 0 - 1 CBV
		context.SetDynamicConstantBufferView(0, sizeof(csConstants), &csConstants);

		context.Dispatch(tileCountX, tileCountY, 1);

		context.TransitionResource(m_LightGrid, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		context.TransitionResource(m_LightGridBitMask, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	void ForwardPlusLighting::Shutdown()
	{
		m_LightBuffer.Destroy();
		m_LightGrid.Destroy();
		m_LightGridBitMask.Destroy();

		m_LightShadowArray.Destroy();
		m_LightShadowTempBuffer.Destroy();
	}
}
