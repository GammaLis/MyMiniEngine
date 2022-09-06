#pragma once
#include "pch.h"
#include "ColorBuffer.h"

namespace MyDirectX
{
	class ColorBuffer;
	class CommandContext;

	class TemporalEffects
	{
	public:
		void Init(ID3D12Device* pDevice);
		void Shutdown();
		void Resize(UINT width, UINT height);

		void Render();
		void UpdateHistory(CommandContext &context);

		// Pointers to resources
		ColorBuffer* m_pColorHistory = nullptr;
		ColorBuffer* m_pDepthHistory = nullptr;
		ColorBuffer* m_pNormalHistory = nullptr;
	};
}
