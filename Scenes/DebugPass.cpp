#include "DebugPass.h"
#include "Graphics.h"
#include "GfxCommon.h"
#include "CommandContext.h"

// Compiled shader bytecode
#include "CommonDebug.h"

using namespace MyDirectX;

bool DebugPass::Init()
{
	// root signature
	{
		m_DebugRS.Reset(4, 2);
		m_DebugRS[0].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 2);    // SceanColorBuffer, OveralyBuffer
		m_DebugRS[1].InitAsConstants(0, 6, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_DebugRS[2].InitAsBufferSRV(2, 0, D3D12_SHADER_VISIBILITY_PIXEL);
		m_DebugRS[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		m_DebugRS.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearClampDesc);
		m_DebugRS.InitStaticSampler(1, Graphics::s_CommonStates.SamplerPointClampDesc);
		m_DebugRS.Finalize(Graphics::s_Device, L"DebugRS"); // RootFlags(0), need not ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	}
	// PSO
	{
		const auto &colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;

		m_DebugPSO.SetRootSignature(m_DebugRS);
		m_DebugPSO.SetInputLayout(0, nullptr); // no input, full screen triangle
		m_DebugPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		m_DebugPSO.SetVertexShader(Graphics::s_ShaderManager.m_ScreenQuadVS);
		m_DebugPSO.SetPixelShader(CommonDebug, sizeof(CommonDebug));
		m_DebugPSO.SetRasterizerState(Graphics::s_CommonStates.RasterizerDefault);
		m_DebugPSO.SetBlendState(Graphics::s_CommonStates.BlendDisable);
		m_DebugPSO.SetDepthStencilState(Graphics::s_CommonStates.DepthStateDisabled);
		m_DebugPSO.SetRenderTargetFormat(colorBuffer.GetFormat(), DXGI_FORMAT_UNKNOWN);
		m_DebugPSO.SetSampleMask(0xFFFFFFFF);
		m_DebugPSO.Finalize(Graphics::s_Device);

	}
	return true;
}

// 'SetRenderTarget' somewhere else
void DebugPass::Render(GraphicsContext& gfx, D3D12_CPU_DESCRIPTOR_HANDLE srv)
{
	gfx.SetRootSignature(m_DebugRS);
	gfx.SetPipelineState(m_DebugPSO);
	gfx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	gfx.SetDynamicDescriptor(0, 0, srv);

	gfx.Draw(3);
}

void DebugPass::Cleanup()
{
}
