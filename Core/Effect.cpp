#include "Effect.h"

namespace MyDirectX
{
	TextRenderer Effect::s_TextRenderer;

	// forward+ lighting
	ForwardPlusLighting Effect::s_ForwardPlusLighting;

	void Effect::Init(ID3D12Device* pDevice)
	{
		s_TextRenderer.Init(pDevice);
		s_ForwardPlusLighting.Init(pDevice);
	}

	void Effect::Shutdown()
	{
		s_TextRenderer.Shutdown();
		s_ForwardPlusLighting.Shutdown();
	}
}