#pragma once
#include "pch.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "GpuBuffer.h"
#include "Camera.h"

namespace MyDirectX
{
	class GraphicsContext;

	class Skybox
	{
	public:
		void Init(ID3D12Device* pDevice, const std::wstring& fileName);
		void Render(GraphicsContext &gfx, const Math::Camera &camera);
		void Shutdown();

	private:

		// rootsignature & pso
		RootSignature m_SkyboxRS;
		GraphicsPSO m_SkyboxPSO;

		StructuredBuffer m_SkyVB;
		ByteAddressBuffer m_SkyIB;
		
		UINT m_VertexCount = 0;
		UINT m_VertexStride = 0;
		UINT m_VertexOffset = 0;

		UINT m_IndexCount = 0;
		UINT m_IndexOffset = 0;

		D3D12_CPU_DESCRIPTOR_HANDLE srv;
	};

}
