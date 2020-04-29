#include "ShadowBuffer.h"
#include "CommandContext.h"

using namespace MyDirectX;

void ShadowBuffer::Create(ID3D12Device* pDevice, const std::wstring& name, uint32_t width, uint32_t height, 
	DXGI_FORMAT format, D3D12_GPU_VIRTUAL_ADDRESS vidMemPtr)
{
	DepthBuffer::Create(pDevice, name, width, height, format, vidMemPtr);

	m_Viewport.TopLeftX = 0.0f;
	m_Viewport.TopLeftY = 0.0f;
	m_Viewport.Width = (float)width;
	m_Viewport.Height = (float)height;
	m_Viewport.MinDepth = 0.0f;
	m_Viewport.MaxDepth = 1.0f;

	// preventing drawing to the boundary pixels so that we don't have to worry about shadow stretching
	m_Scissor.left = 1;
	m_Scissor.top = 1;
	m_Scissor.right = (LONG)width - 2;
	m_Scissor.bottom = (LONG)height - 2;
}

void ShadowBuffer::BeginRendering(GraphicsContext& context)
{
	context.TransitionResource(*this, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
	context.ClearDepth(*this);
	context.SetDepthStencilTarget(GetDSV());
	context.SetViewportAndScissor(m_Viewport, m_Scissor);
}

void ShadowBuffer::EndRendering(GraphicsContext& context)
{
	context.TransitionResource(*this, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}
