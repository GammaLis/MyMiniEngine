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
#if 0
		void Resize(UINT width, UINT height);
#endif

		void Render();
		void UpdateHistory(CommandContext &context);

		bool HasInited() const { return m_bInited; }

	private:
		// Pointers to resources
		ColorBuffer* m_pColorHistory = nullptr;
		ColorBuffer* m_pDepthHistory = nullptr;
		ColorBuffer* m_pNormalHistory = nullptr;

		bool m_bInited = false;
	};
}
