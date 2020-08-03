#include "OceanViewer.h"
#include "Graphics.h"
#include "CommandContext.h"
#include "MyBasicGeometry.h"
#include "GameTimer.h"

// compiled shader bytecode
#include "InitH0SpectrumCS.h"
#include "GenerateFFTWeightsCS.h"
#include "UpdateSpectrumCS.h"
#include "FFTHorizontalCS.h"
#include "FFTVerticalCS.h"
#include "CombineIFFTOutputsCS.h"

// for debug
#include "WaterShadingVS.h"
#include "WaterShadingPS.h"

#define _DEBUGPASS 0

using namespace MyDirectX;

struct MyVertex
{
	XMFLOAT3 position;
	XMFLOAT3 normal;
	XMFLOAT2 uv;
};

struct alignas(16) CBPerObject
{
	XMMATRIX _WorldMat;
	XMMATRIX _InvWorldMat;
};

struct alignas(16) CBPerCamera
{
	XMMATRIX _ViewProjMat;
	XMFLOAT3 _CamPos;
};

OceanViewer::OceanViewer(HINSTANCE hInstance, const wchar_t* title, UINT width, UINT height)
	: IGameApp(hInstance, title, width, height)
{
}

void OceanViewer::Update(float deltaTime)
{
	IGameApp::Update(deltaTime);

	// input
	{
		m_CameraController->Update(deltaTime);
		m_ViewProjMatrix = m_Camera.GetViewProjMatrix();
	}

	// update spectrum
	{
		UpdateSpectrumsAndDoIFFT();
	}	

}

void OceanViewer::Render()
{
	GraphicsContext &gfx = GraphicsContext::Begin(L"Render Mesh");

	auto &colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto &depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
	
#if !_DEBUGPASS
	gfx.SetRootSignature(m_WaterWaveRS);
	// mesh
	{
		gfx.SetPipelineState(m_BasicShadingPSO);

		gfx.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfx.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);

		gfx.ClearColor(colorBuffer);
		gfx.ClearDepthAndStencil(depthBuffer);
		gfx.SetRenderTarget(colorBuffer.GetRTV(), depthBuffer.GetDSV());
		gfx.SetViewportAndScissor(m_MainViewport, m_MainScissor);

		gfx.SetVertexBuffer(0, m_MeshVB.VertexBufferView());
		gfx.SetIndexBuffer(m_MeshIB.IndexBufferView());
		gfx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		//
		gfx.SetConstants((uint32_t)WaterWaveRSId::CBConstants, 1, 0, 1, 0);
		// 
		CBPerObject cbPerObject;
		XMMATRIX worldMat = XMMatrixIdentity();
		cbPerObject._WorldMat = XMMatrixTranspose(worldMat);
		cbPerObject._InvWorldMat = XMMatrixInverse(nullptr, cbPerObject._WorldMat);
		gfx.SetDynamicConstantBufferView((uint32_t)WaterWaveRSId::CBPerObject, sizeof(cbPerObject), &cbPerObject);
		// 
		CBPerCamera cbPerCamera;
		const auto &viewProjMat = m_Camera.GetViewProjMatrix();
		const auto &camPos = m_Camera.GetPosition();
		cbPerCamera._ViewProjMat = XMMatrixTranspose(XMMATRIX(viewProjMat));
		cbPerCamera._CamPos = XMFLOAT3(camPos.GetX(), camPos.GetY(), camPos.GetZ());
		gfx.SetDynamicConstantBufferView((uint32_t)WaterWaveRSId::CBPerCamera, sizeof(cbPerCamera), &cbPerCamera);
		// 
		D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = 
		{
			m_DisplacementMap.GetSRV(),
			m_NormalAndFoldMap.GetSRV(),
		};
		gfx.SetDynamicDescriptors((UINT)WaterWaveRSId::SRVTable, 0, _countof(srvs), srvs);

		gfx.DrawIndexedInstanced(m_IndexCount, 1, m_IndexOffset, m_VertexOffset, 0);
	}

#else
	// debug
	{
		gfx.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);

		gfx.ClearColor(colorBuffer);
		gfx.SetRenderTarget(colorBuffer.GetRTV());
		gfx.SetViewportAndScissor(m_MainViewport, m_MainScissor);

		m_DebugPass.Render(gfx, m_DisplacementMap.GetSRV());
	}
#endif

	gfx.Finish();

}

void OceanViewer::InitPipelineStates()
{
	// root signature
	{
		m_WaterWaveRS.Reset((UINT)WaterWaveRSId::Count, 1);
		m_WaterWaveRS[(UINT)WaterWaveRSId::CBConstants].InitAsConstants(0, 8);
		m_WaterWaveRS[(UINT)WaterWaveRSId::CBPerObject].InitAsConstantBuffer(1);
		m_WaterWaveRS[(UINT)WaterWaveRSId::CBPerCamera].InitAsConstantBuffer(2);
		m_WaterWaveRS[(UINT)WaterWaveRSId::SRVTable].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 4);
		m_WaterWaveRS[(UINT)WaterWaveRSId::UAVTable].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 4);
		m_WaterWaveRS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearWrapDesc);
		m_WaterWaveRS.Finalize(Graphics::s_Device, L"WaterWaveRS", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	}

	// PSOs
	{
		/// ComputePipelineStates
		// InitH0Spectrum
		m_InitH0SpectrumPSO.SetRootSignature(m_WaterWaveRS);
		m_InitH0SpectrumPSO.SetComputeShader(InitH0SpectrumCS, sizeof(InitH0SpectrumCS));
		m_InitH0SpectrumPSO.Finalize(Graphics::s_Device);

		// GenerateFFTWeights
		m_GenerateFFTWeightsPSO.SetRootSignature(m_WaterWaveRS);
		m_GenerateFFTWeightsPSO.SetComputeShader(GenerateFFTWeightsCS, sizeof(GenerateFFTWeightsCS));
		m_GenerateFFTWeightsPSO.Finalize(Graphics::s_Device);

		// UpdateSpectrum
		m_UpdateSpectrumPSO.SetRootSignature(m_WaterWaveRS);
		m_UpdateSpectrumPSO.SetComputeShader(UpdateSpectrumCS, sizeof(UpdateSpectrumCS));
		m_UpdateSpectrumPSO.Finalize(Graphics::s_Device);

		// FFT
		m_FFTHorizontalPSO.SetRootSignature(m_WaterWaveRS);
		m_FFTHorizontalPSO.SetComputeShader(FFTHorizontalCS, sizeof(FFTHorizontalCS));
		m_FFTHorizontalPSO.Finalize(Graphics::s_Device);

		m_FFTVerticalPSO.SetRootSignature(m_WaterWaveRS);
		m_FFTVerticalPSO.SetComputeShader(FFTVerticalCS, sizeof(FFTVerticalCS));
		m_FFTVerticalPSO.Finalize(Graphics::s_Device);

		// Combine Outputs
		m_CombineOutputsPSO.SetRootSignature(m_WaterWaveRS);
		m_CombineOutputsPSO.SetComputeShader(CombineIFFTOutputsCS, sizeof(CombineIFFTOutputsCS));
		m_CombineOutputsPSO.Finalize(Graphics::s_Device);

		/// GraphicsPipelineStates
		// color buffer & depth buffer
		const auto &colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
		const auto &depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;

		D3D12_INPUT_ELEMENT_DESC InputElements[] =
		{
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};
		m_BasicShadingPSO.SetRootSignature(m_WaterWaveRS);
		m_BasicShadingPSO.SetInputLayout(_countof(InputElements), InputElements);
		m_BasicShadingPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		m_BasicShadingPSO.SetVertexShader(WaterShadingVS, sizeof(WaterShadingVS));
		m_BasicShadingPSO.SetPixelShader(WaterShadingPS, sizeof(WaterShadingPS));
		m_BasicShadingPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerDefaultWireframe);
		m_BasicShadingPSO.SetBlendState(Graphics::s_CommonStates.BlendDisable);
		m_BasicShadingPSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateReadWrite);
		m_BasicShadingPSO.SetRenderTargetFormat(colorBuffer.GetFormat(), depthBuffer.GetFormat());
		m_BasicShadingPSO.Finalize(Graphics::s_Device);
	}

}

void OceanViewer::InitGeometryBuffers()
{
	// grid
	Geometry::Mesh grid;
	Geometry::MyBasicGeometry::BasicGrid(256.0f, 256.0f, m_Rows, m_Columns, grid);
	
	m_VertexCount = (uint32_t)grid.vertices.size();
	m_IndexCount = (uint32_t)grid.indices.size();
	// vertices
	{
		std::vector<MyVertex> vertices(m_VertexCount);
		uint32_t i = 0;
		std::for_each(grid.vertices.begin(), grid.vertices.end(), [&vertices, i](const auto &v) mutable {
				auto &vertex = vertices[i++];
				vertex.position = v.position;
				vertex.normal = v.normal;
				vertex.uv = v.uv;
			});
		m_MeshVB.Create(Graphics::s_Device, L"MeshVB", m_VertexCount, sizeof(MyVertex), vertices.data());
	}
	// indices
	{
		std::vector<uint16_t> indices(m_IndexCount);
		uint32_t i = 0;
		std::for_each(grid.indices.begin(), grid.indices.end(), [&indices, i](const auto &v) mutable {
			indices[i++] = static_cast<uint16_t>(v);
			});
		m_MeshIB.Create(Graphics::s_Device, L"MeshIB", m_IndexCount, sizeof(uint16_t), indices.data());
	}

}

void OceanViewer::InitCustom()
{
	ID3D12Device *pDevice = Graphics::s_Device;

	// resources
	{
		uint32_t nPass = (uint32_t)std::ceil(std::log2(N));
		m_H0SpectrumTexture.Create(pDevice, L"H0SpectrumTexture", m_Columns, m_Rows, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
		m_FFTWeightsTexture.Create(pDevice, L"FFTWeightsTexture", N, nPass, 1, DXGI_FORMAT_R32G32_FLOAT);
		// spectrum textures
		m_HeightSpectrumTexture.Create(pDevice, L"HeightSpectrumTexture", m_Columns, m_Rows, 1, DXGI_FORMAT_R32G32_FLOAT);
		m_DXSpectrumTexture.Create(pDevice, L"DXSpectrumTexture", m_Columns, m_Rows, 1, DXGI_FORMAT_R32G32_FLOAT);
		m_DYSpectrumTexture.Create(pDevice, L"DYSPectrumTexture", m_Columns, m_Rows, 1, DXGI_FORMAT_R32G32_FLOAT);
		// ifft result textures
		m_HeightMap.Create(pDevice, L"HeightMap", m_Columns, m_Rows, 1, DXGI_FORMAT_R32G32_FLOAT);
		m_TempHeightMap.Create(pDevice, L"Temp HeightMap", m_Columns, m_Rows, 1, DXGI_FORMAT_R32G32_FLOAT);
		m_DXMap.Create(pDevice, L"DXMap", m_Columns, m_Rows, 1, DXGI_FORMAT_R32G32_FLOAT);
		m_TempDXMap.Create(pDevice, L"Temp DXMap", m_Columns, m_Rows, 1, DXGI_FORMAT_R32G32_FLOAT);
		m_DYMap.Create(pDevice, L"DYMap", m_Columns, m_Rows, 1, DXGI_FORMAT_R32G32_FLOAT);
		m_TempDYMap.Create(pDevice, L"Temp DYMap", m_Columns, m_Rows, 1, DXGI_FORMAT_R32G32_FLOAT);
		// displacement & normal/fold map
		m_DisplacementMap.Create(pDevice, L"DisplacementMap", m_Columns, m_Rows, 1, DXGI_FORMAT_R32G32B32A32_FLOAT); // DXGI_FORMAT_R32G32B32_FLOAT
		// Error::ID3D12Device::CreateCommittedResource:
		// A texture that has D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS set in D3D12_RESOURCE_DESC::Flags
		// must be created with a format that either can be used as unordered access, or cast to a format that
		// could be used as unordered access.FeatureLevel is D3D_FEATURE_LEVEL_12_1.
		// D3D12_RESOURCE_DESC::Format is R32G32B32_FLOAT
		m_NormalAndFoldMap.Create(pDevice, L"NormalAndFoldMap", m_Columns, m_Rows, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
	}

	// camera
	{
		Math::Vector3 camPos{0.0f, 50.0f, 0.0f};
		m_Camera.SetEyeAtUp(camPos, camPos + Math::Vector3(0.0f, 0.0f, 1.0f), Math::Vector3(Math::kYUnitVector));
		m_Camera.SetZRange(1.0f, 1000.0f);
		m_CameraController.reset(new CameraController(m_Camera, Math::Vector3(Math::kYUnitVector), *m_Input));
	}

	// debug
	{
		m_DebugPass.Init();
	}

	// initial computes
	{
		InitUnchangedResources();
	}

}

void OceanViewer::CleanCustom()
{
	m_MeshVB.Destroy();
	m_MeshIB.Destroy();

	// init textures
	m_H0SpectrumTexture.Destroy();
	m_FFTWeightsTexture.Destroy();
	// spectrum textures
	m_HeightSpectrumTexture.Destroy();
	m_DXSpectrumTexture.Destroy();
	m_DYSpectrumTexture.Destroy();
	// ifft result textures
	m_HeightMap.Destroy();
	m_TempHeightMap.Destroy();
	m_DXMap.Destroy();
	m_TempDXMap.Destroy();
	m_DYMap.Destroy();
	m_TempDYMap.Destroy();
	// combined results
	m_DisplacementMap.Destroy();
	m_NormalAndFoldMap.Destroy();
}

void OceanViewer::InitUnchangedResources()
{
	ComputeContext& computeContext = ComputeContext::Begin(L"InitUnchangedResources");

	// H0 spectrum texture
	computeContext.SetRootSignature(m_WaterWaveRS);
	{
		computeContext.TransitionResource(m_H0SpectrumTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		computeContext.SetPipelineState(m_InitH0SpectrumPSO);

		float L = m_L;
		float V = m_V;	// 改小 e.g.4,5， 中间会有一个 暗区(0区)
		float A = m_A;
		XMFLOAT2 W = m_W;
		float fConstants[] = {L, V, A, W.x, W.y};
		computeContext.SetConstants((UINT)WaterWaveRSId::CBConstants, N);
		computeContext.SetConstants((UINT)WaterWaveRSId::CBConstants, _countof(fConstants), (const void*)fConstants, 1);
		computeContext.SetDynamicDescriptor((UINT)WaterWaveRSId::UAVTable, 0, m_H0SpectrumTexture.GetUAV());

		const uint32_t GroupSizeX = 16, GroupSizeY = 16;
		computeContext.Dispatch2D(m_Columns, m_Rows, GroupSizeX, GroupSizeY);

		computeContext.TransitionResource(m_H0SpectrumTexture, D3D12_RESOURCE_STATE_GENERIC_READ);	// D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
	}

	// FFT weights texture
	{
		computeContext.TransitionResource(m_FFTWeightsTexture, D3D12_RESOURCE_STATE_GENERIC_READ);
		
		computeContext.SetPipelineState(m_GenerateFFTWeightsPSO);

		computeContext.SetDynamicDescriptor((uint32_t)WaterWaveRSId::UAVTable, 0, m_FFTWeightsTexture.GetUAV());

		const uint32_t GroupSizeX = N, GroupSizeY = 1;
		uint32_t nPass = (uint32_t)(std::ceil(std::log2(N)));
		computeContext.Dispatch2D(GroupSizeX, nPass, GroupSizeX, GroupSizeY);

		computeContext.TransitionResource(m_FFTWeightsTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}

	computeContext.Finish(true);
}

void OceanViewer::UpdateSpectrumsAndDoIFFT()
{
	ComputeContext &computeContext = ComputeContext::Begin(L"Update Spectrums");

	computeContext.SetRootSignature(m_WaterWaveRS);
	
	// update spectrums
	{
		computeContext.SetPipelineState(m_UpdateSpectrumPSO);

		computeContext.TransitionResource(m_HeightSpectrumTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		computeContext.TransitionResource(m_DXSpectrumTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		computeContext.TransitionResource(m_DYSpectrumTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		// 
		float curTime = m_Timer->TotalTime();
		float time[] = {curTime / 20.0f, curTime, 2.0f * curTime, 3.0f * curTime};
		float L = m_L, depth = m_Depth;
		computeContext.SetConstants((UINT)WaterWaveRSId::CBConstants, N, L, depth);
		computeContext.SetConstants((UINT)WaterWaveRSId::CBConstants, _countof(time), time, 4);
		
		//
		computeContext.SetDynamicDescriptor((UINT)WaterWaveRSId::SRVTable, 0, m_H0SpectrumTexture.GetSRV());
		//
		D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = 
		{
			m_HeightSpectrumTexture.GetUAV(),
			m_DXSpectrumTexture.GetUAV(),
			m_DYSpectrumTexture.GetUAV(),
		};
		computeContext.SetDynamicDescriptors((UINT)WaterWaveRSId::UAVTable, 0, _countof(uavs), uavs);

		const uint32_t GroupSizeX = 16, GroupSizeY = 16;
		computeContext.Dispatch2D(m_Columns, m_Rows, GroupSizeX, GroupSizeY);

		computeContext.TransitionResource(m_HeightSpectrumTexture, D3D12_RESOURCE_STATE_GENERIC_READ);
		computeContext.TransitionResource(m_DXSpectrumTexture, D3D12_RESOURCE_STATE_GENERIC_READ);
		computeContext.TransitionResource(m_DYSpectrumTexture, D3D12_RESOURCE_STATE_GENERIC_READ);
	}

	// update Height&Dx&Dy map
	{
		DoIFFT(computeContext, m_HeightSpectrumTexture, m_TempHeightMap, m_HeightMap);
		DoIFFT(computeContext, m_DXSpectrumTexture, m_TempDXMap, m_DXMap);
		DoIFFT(computeContext, m_DYSpectrumTexture, m_TempDYMap, m_DYMap);
	}

	// combine outputs
	{
		CombineOutputs(computeContext);
	}

	computeContext.Finish();
}

void OceanViewer::DoIFFT(ComputeContext& computeContext, ColorBuffer& spectrumInput, ColorBuffer& horizontalOutput, ColorBuffer& verticalOutput)
{
	/// horizontal pass
	computeContext.SetPipelineState(m_FFTHorizontalPSO);

	computeContext.TransitionResource(horizontalOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	computeContext.TransitionResource(verticalOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// 
	computeContext.SetConstants((UINT)WaterWaveRSId::CBConstants, N);
	// 
	D3D12_CPU_DESCRIPTOR_HANDLE h_SRVs[] =
	{
		spectrumInput.GetSRV(),
		m_FFTWeightsTexture.GetSRV(),
	};
	computeContext.SetDynamicDescriptors((UINT)WaterWaveRSId::SRVTable, 0, _countof(h_SRVs), h_SRVs);
	// 
	computeContext.SetDynamicDescriptor((UINT)WaterWaveRSId::UAVTable, 0, horizontalOutput.GetUAV());

	uint32_t GroupSizeX = N, GroupSizeY = 1;
	computeContext.Dispatch2D(N, N, GroupSizeX, GroupSizeY);

	computeContext.TransitionResource(horizontalOutput, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	// 须要 写完 Horizontal Pass 才能继续写 Vertical Pass

	/// vertical pass
	computeContext.SetPipelineState(m_FFTVerticalPSO);

	// 
	D3D12_CPU_DESCRIPTOR_HANDLE v_SRVs[] =
	{
		horizontalOutput.GetSRV(),
		m_FFTWeightsTexture.GetSRV(),
	};
	computeContext.SetDynamicDescriptors((UINT)WaterWaveRSId::SRVTable, 0, _countof(v_SRVs), v_SRVs);
	// 
	computeContext.SetDynamicDescriptor((UINT)WaterWaveRSId::UAVTable, 0, verticalOutput.GetUAV());

	GroupSizeX = 1, GroupSizeY = N;
	computeContext.Dispatch2D(N, N, GroupSizeX, GroupSizeY);

	computeContext.TransitionResource(verticalOutput, D3D12_RESOURCE_STATE_GENERIC_READ);
}

void OceanViewer::CombineOutputs(ComputeContext& computeContext)
{
	computeContext.SetPipelineState(m_CombineOutputsPSO);

	computeContext.TransitionResource(m_DisplacementMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	computeContext.TransitionResource(m_NormalAndFoldMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	//
	float L = m_L, heightScale = 16.0f, lambda = 16.0f;
	computeContext.SetConstants((UINT)WaterWaveRSId::CBConstants, N, L, heightScale, lambda);
	//
	D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = 
	{
		m_HeightMap.GetSRV(),
		m_DXMap.GetSRV(),
		m_DYMap.GetSRV(),
	};
	computeContext.SetDynamicDescriptors((UINT)WaterWaveRSId::SRVTable, 0, _countof(srvs), srvs);
	// 
	D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = 
	{
		m_DisplacementMap.GetUAV(),
		m_NormalAndFoldMap.GetUAV(),
	};
	computeContext.SetDynamicDescriptors((UINT)WaterWaveRSId::UAVTable, 0, _countof(uavs), uavs);

	const uint32_t GroupSizeX = 16, GroupSizeY = 16;
	computeContext.Dispatch2D(m_Columns, m_Rows, GroupSizeX, GroupSizeY);

	computeContext.TransitionResource(m_DisplacementMap, D3D12_RESOURCE_STATE_GENERIC_READ);
	computeContext.TransitionResource(m_NormalAndFoldMap, D3D12_RESOURCE_STATE_GENERIC_READ);
}
