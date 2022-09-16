#include "TemporalEffects.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "ProfilingScope.h"

using namespace MyDirectX;


void TemporalEffects::Init(ID3D12Device* pDevice)
{
	// Set pointers
	m_pColorHistory = &Graphics::s_BufferManager.m_ColorHistory;
	m_pDepthHistory = &Graphics::s_BufferManager.m_DepthHistory;
	m_pNormalHistory = &Graphics::s_BufferManager.m_NormalHistory;

	const uint32_t& width = GfxStates::s_NativeWidth, height = GfxStates::s_NativeHeight;
	if (m_pColorHistory->GetResource() == nullptr)
	{
		m_pColorHistory->Create(pDevice, L"Color History", width, height, 1, GfxStates::s_DefaultHdrColorFormat);
	}
	if (m_pDepthHistory->GetResource() == nullptr)
	{
		m_pDepthHistory->Create(pDevice, L"Depth History", width, height, 1, DXGI_FORMAT_R32_FLOAT);
	}
	if (m_pNormalHistory->GetResource() == nullptr)
	{
		m_pNormalHistory->Create(pDevice, L"Normal History", width, height, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
	}

	m_bInited = true;
}

void TemporalEffects::Shutdown()
{
	m_pColorHistory = nullptr;
	m_pDepthHistory = nullptr;
	m_pNormalHistory = nullptr;
}

#if 0
void TemporalEffects::Resize(UINT width, UINT height)
{
	ID3D12Device* pDevice = Graphics::s_Device;

	if (m_pColorHistory != nullptr && m_pColorHistory->GetResource() != nullptr)
	{
		m_pColorHistory->Create(pDevice, L"Color History", width, height, 1, GfxStates::s_DefaultHdrColorFormat);
	}
	if (m_pDepthHistory != nullptr && m_pDepthHistory->GetResource() != nullptr)
	{
		m_pDepthHistory->Create(pDevice, L"Depth History", width, height, 1, DXGI_FORMAT_R32_FLOAT);
	}
	if (m_pNormalHistory != nullptr && m_pNormalHistory->GetResource() != nullptr)
	{
		m_pNormalHistory->Create(pDevice, L"Normal History", width, height, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
	}
}
#endif

void TemporalEffects::Render()
{

}

void TemporalEffects::UpdateHistory(CommandContext& context)
{
	ProfilingScope profilingScope(L"Update History", context);

	auto &colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto &depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
	auto &normalBuffer = Graphics::s_BufferManager.m_SceneNormalBuffer;

	if (m_pColorHistory != nullptr)
	{
		context.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
		context.TransitionResource(*m_pColorHistory, D3D12_RESOURCE_STATE_COPY_DEST);
	}
	if(m_pDepthHistory != nullptr)
	{
		context.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
		context.TransitionResource(*m_pDepthHistory, D3D12_RESOURCE_STATE_COPY_DEST);
	}
	if(m_pNormalHistory != nullptr)
	{
		context.TransitionResource(normalBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
		context.TransitionResource(*m_pNormalHistory, D3D12_RESOURCE_STATE_COPY_DEST);
	}
	context.FlushResourceBarriers();

	if (m_pColorHistory != nullptr)
	{
		context.CopyBuffer(*m_pColorHistory, colorBuffer);
	}
	if (m_pDepthHistory != nullptr)
	{
		context.CopyBuffer(*m_pDepthHistory, depthBuffer);
	}
	if (m_pNormalHistory != nullptr)
	{
		context.CopyBuffer(*m_pNormalHistory, normalBuffer);
	}
}
