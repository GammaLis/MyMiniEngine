#include "VoronoiTextureGenerator.h"
#include "Graphics.h"
#include "GfxCommon.h"
#include "CommandContext.h"
#include "Math/Random.h"
#include "GameTimer.h"

// compiled shader bytecode
#include "VoronoiVS.h"
#include "VoronoiPS.h"
#include "InitSeedsCS.h"
#include "JumpFloodVoronoiCS.h"
#include "JFAVoronoiTexCS.h"

#define _DEBUGPASS 0

using namespace MyDirectX;

const DXGI_FORMAT VoronoiTextureGenerator::s_DepthFormat = DXGI_FORMAT_D32_FLOAT;

VoronoiTextureGenerator::VoronoiTextureGenerator(HINSTANCE hInstance, const wchar_t* title, UINT width, UINT height)
	: IGameApp(hInstance, title, width, height), m_DepthBuffer(1.0f)
{

}

void VoronoiTextureGenerator::Update(float deltaTime)
{
	static float dt = 0.0f;
	
	dt += deltaTime;
	if (dt >= 1.0f)
	{
		UpdateVoronoi();
		dt = 0.0f;
	}
}

void VoronoiTextureGenerator::Render()
{
	GraphicsContext &gfx = GraphicsContext::Begin(L"Render VoronoiTexture");

#if _DEBUGPASS == 0
	auto &colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;

	gfx.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);

	gfx.ClearColor(colorBuffer);
	gfx.SetRenderTarget(colorBuffer.GetRTV());
	gfx.SetViewportAndScissor(m_MainViewport, m_MainScissor);

	// pingpong buffers
	uint32_t nPass = static_cast<uint32_t>(std::ceil(std::log2(s_Size)));
	uint32_t finalPass = nPass & 1;
	auto &finalTex = m_PingpongBuffers[finalPass];
	
	// voronoi texture

	m_DebugPass.Render(gfx, m_VoronoiTexture.GetSRV());	// finalTex.GetSRV()

#elif _DEBUGPASS == 1
	gfx.SetRootSignature(m_VoronoiRS);
	gfx.SetPipelineState(m_VoronoiPSO);

	auto &colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	// auto &depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
	auto &depthBuffer = m_DepthBuffer;
	gfx.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
	gfx.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
	gfx.ClearColor(colorBuffer);
	gfx.ClearDepth(depthBuffer);
	gfx.SetRenderTarget(colorBuffer.GetRTV(), depthBuffer.GetDSV());
	gfx.SetViewportAndScissor(m_MainViewport, m_MainScissor);

	gfx.SetVertexBuffer(0, m_InstanceBuffer.VertexBufferView());
	gfx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	gfx.SetConstants((UINT)VoronoiTextureRSId::CBConstants, m_Width, m_Height);

	gfx.DrawInstanced(4, m_NumVertex);

#elif _DEBUGPASS == 3
	
#endif

	gfx.Finish();
}

void VoronoiTextureGenerator::InitPipelineStates()
{
	{
		m_VoronoiRS.Reset((UINT)VoronoiTextureRSId::Count, 2);
		m_VoronoiRS[(UINT)VoronoiTextureRSId::CBConstants].InitAsConstants((UINT)VoronoiTextureRSId::CBConstants, 8);
		m_VoronoiRS[(UINT)VoronoiTextureRSId::SRVTable].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 2);
		m_VoronoiRS[(UINT)VoronoiTextureRSId::UAVTable].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 2);
		m_VoronoiRS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerPointClampDesc);
		m_VoronoiRS.InitStaticSampler(1, Graphics::s_CommonStates.SamplerLinearWrapDesc);
		m_VoronoiRS.Finalize(Graphics::s_Device, L"VoronoiTextureRS", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	}

	// voronoi PSO
	{
		D3D12_INPUT_ELEMENT_DESC InputElements[] = 
		{
			{"CENTROID", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1}
		};

		auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
		// auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;	// ²»ÓÃÄ¬ÈÏDepthBuffer

		D3D12_DEPTH_STENCIL_DESC depthStencilDesc = Graphics::s_CommonStates.DepthStateReadWrite;
		depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;	// D3D12_COMPARISON_FUNC_LESS D3D12_COMPARISON_FUNC_LESS_EQUAL
		// D3D12_COMPARISON_FUNC_ALWAYS

		m_VoronoiPSO.SetRootSignature(m_VoronoiRS);
		m_VoronoiPSO.SetInputLayout(_countof(InputElements), InputElements);
		m_VoronoiPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		m_VoronoiPSO.SetVertexShader(VoronoiVS, sizeof(VoronoiVS));
		m_VoronoiPSO.SetPixelShader(VoronoiPS, sizeof(VoronoiPS));
		m_VoronoiPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerDefault);
		m_VoronoiPSO.SetBlendState(Graphics::s_CommonStates.BlendDisable);
		m_VoronoiPSO.SetDepthStencilState(depthStencilDesc);
		m_VoronoiPSO.SetRenderTargetFormat(colorBuffer.GetFormat(), s_DepthFormat);	// depthBuffer.GetFormat()
		m_VoronoiPSO.Finalize(Graphics::s_Device);
	}

	// cs
	{
		m_InitSeedsPSO.SetRootSignature(m_VoronoiRS);
		m_InitSeedsPSO.SetComputeShader(InitSeedsCS, sizeof(InitSeedsCS));
		m_InitSeedsPSO.Finalize(Graphics::s_Device);

		m_JFAPSO.SetRootSignature(m_VoronoiRS);
		m_JFAPSO.SetComputeShader(JumpFloodVoronoiCS, sizeof(JumpFloodVoronoiCS));
		m_JFAPSO.Finalize(Graphics::s_Device);

		m_GenVoronoiTexPSO.SetRootSignature(m_VoronoiRS);
		m_GenVoronoiTexPSO.SetComputeShader(JFAVoronoiTexCS, sizeof(JFAVoronoiTexCS));
		m_GenVoronoiTexPSO.Finalize(Graphics::s_Device);
	}
}

void VoronoiTextureGenerator::InitGeometryBuffers()
{
	ID3D12Device *pDevice = Graphics::s_Device;

	// ZTest Voronoi
	{
		std::vector<XMFLOAT2> positions(m_NumVertex);

		Math::RandomNumberGenerator rng;
		rng.SetSeed(4);
		for (uint32_t i = 0; i < m_NumVertex; ++i)
		{
			float x = rng.NextFloat() * m_Width;
			float y = rng.NextFloat() * m_Height;
			positions[i] = XMFLOAT2(x, y);
		}

		m_InstanceBuffer.Create(pDevice, L"InstanceBuffer", m_NumVertex, sizeof(XMFLOAT2), positions.data());

		auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
		m_DepthBuffer.Create(Graphics::s_Device, L"VoronoiDepthBuffer", colorBuffer.GetWidth(), colorBuffer.GetHeight(), s_DepthFormat);
	}

	// JFA Voronoi
	{
		m_InitTexture.Create(pDevice, L"InitSeedTexture", s_Size, s_Size, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
		m_PingpongBuffers[0].Create(pDevice, L"JFA PingpongBuffer0", s_Size, s_Size, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
		m_PingpongBuffers[1].Create(pDevice, L"JFA PingpongBuffer1", s_Size, s_Size, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
		m_VoronoiTexture.Create(pDevice, L"Voronoit Texture", s_Size, s_Size, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
	}
}

void VoronoiTextureGenerator::InitCustom()
{
	m_DebugPass.Init();

	UpdateVoronoi();
}

void VoronoiTextureGenerator::CleanCustom()
{
	m_InstanceBuffer.Destroy();
	m_DepthBuffer.Destroy();

	m_InitTexture.Destroy();
	m_PingpongBuffers[0].Destroy();
	m_PingpongBuffers[1].Destroy();
}

void VoronoiTextureGenerator::UpdateVoronoi()
{
	GraphicsContext &gfx = GraphicsContext::Begin(L"Update Voronoi");

	gfx.TransitionResource(m_InitTexture, D3D12_RESOURCE_STATE_RENDER_TARGET);
	gfx.TransitionResource(m_PingpongBuffers[0], D3D12_RESOURCE_STATE_RENDER_TARGET);
	gfx.TransitionResource(m_PingpongBuffers[1], D3D12_RESOURCE_STATE_RENDER_TARGET, true);

	gfx.ClearColor(m_InitTexture);
	gfx.ClearColor(m_PingpongBuffers[0]);
	gfx.ClearColor(m_PingpongBuffers[1]);

	ComputeContext &computeContext = gfx.GetComputeContext();
	computeContext.SetRootSignature(m_VoronoiRS);
	UpdateSeedTexture(computeContext);
	DoJFA(computeContext);
	GenerateVoronoiTexture(computeContext);

	gfx.Finish();

}

void VoronoiTextureGenerator::UpdateSeedTexture(ComputeContext& computeContext)
{
	computeContext.SetPipelineState(m_InitSeedsPSO);

	computeContext.TransitionResource(m_InitTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	computeContext.SetConstant((UINT)VoronoiTextureRSId::CBConstants, s_Size);
	float curTime = m_Timer->TotalTime();
	float time[] = { curTime / 20.0f, curTime, 2.0f * curTime, 3.0f * curTime };
	computeContext.SetConstants((UINT)VoronoiTextureRSId::CBConstants, _countof(time), time, 4);

	computeContext.SetDynamicDescriptor((UINT)VoronoiTextureRSId::UAVTable, 0, m_InitTexture.GetUAV());

	computeContext.Dispatch2D(s_Size, s_Size);

	computeContext.TransitionResource(m_InitTexture, D3D12_RESOURCE_STATE_GENERIC_READ);
}

void VoronoiTextureGenerator::DoJFA(ComputeContext& computeContext)
{
	computeContext.CopyBuffer(m_PingpongBuffers[0], m_InitTexture);

	computeContext.SetPipelineState(m_JFAPSO);

	computeContext.SetConstants((UINT)VoronoiTextureRSId::CBConstants, s_Size, s_Size);

	uint32_t nPass = static_cast<uint32_t>(std::ceil(std::log2(s_Size)));

	uint32_t inId = 0;
	uint32_t outId = 1;
	uint32_t stepSize = s_Size;
	for (uint32_t i = 0; i < nPass; ++i)
	{
		auto &srcTex = m_PingpongBuffers[inId];
		auto &dstTex = m_PingpongBuffers[outId];
		computeContext.TransitionResource(srcTex, D3D12_RESOURCE_STATE_GENERIC_READ);
		computeContext.TransitionResource(dstTex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		stepSize >>= 1;
		computeContext.SetConstant((UINT)VoronoiTextureRSId::CBConstants, stepSize, 2);

		computeContext.SetDynamicDescriptor((UINT)VoronoiTextureRSId::SRVTable, 0, srcTex.GetSRV());
		computeContext.SetDynamicDescriptor((UINT)VoronoiTextureRSId::UAVTable, 0, dstTex.GetUAV());

		computeContext.Dispatch2D(s_Size, s_Size);

		inId = (inId + 1) & 1;	// % 2
		outId = (outId + 1) & 1;
	}
	computeContext.TransitionResource(m_PingpongBuffers[inId], D3D12_RESOURCE_STATE_GENERIC_READ);
	
}

void VoronoiTextureGenerator::GenerateVoronoiTexture(ComputeContext& computeContext)
{
	computeContext.SetPipelineState(m_GenVoronoiTexPSO);

	uint32_t nPass = static_cast<uint32_t>(std::ceil(std::log2(s_Size)));
	uint32_t finalPass = nPass & 1;
	auto& finalTex = m_PingpongBuffers[finalPass];
	computeContext.TransitionResource(finalTex, D3D12_RESOURCE_STATE_GENERIC_READ);
	computeContext.TransitionResource(m_VoronoiTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	computeContext.SetDynamicDescriptor((UINT)VoronoiTextureRSId::SRVTable, 0, finalTex.GetSRV());
	computeContext.SetDynamicDescriptor((UINT)VoronoiTextureRSId::UAVTable, 0, m_VoronoiTexture.GetUAV());

	computeContext.Dispatch2D(s_Size, s_Size);

	computeContext.TransitionResource(m_VoronoiTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

/**
	GPU Accelerated Voronoi Textures and Filters
	https://weigert.vsos.ethz.ch/2020/08/01/gpu-accelerated-voronoi/

	Jump flooding
	https://blog.demofox.org/2016/02/29/fast-voronoi-diagrams-and-distance-dield-textures-on-the-gpu-with-the-jump-flooding-algorithm/
*/
