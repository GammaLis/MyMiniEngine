#pragma once
#include "RootSignature.h"
#include "PipelineState.h"

namespace MyDirectX
{
	class GraphicsContext;
	class DebugPass
	{
	public:
		DebugPass() = default;

		bool Init();
		void Render(GraphicsContext &gfx, D3D12_CPU_DESCRIPTOR_HANDLE srv);
		void Cleanup();

	private:
		RootSignature m_DebugRS;	// use 'PresentRS'
		GraphicsPSO m_DebugPSO;
	};

}
