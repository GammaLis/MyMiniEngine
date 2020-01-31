#include "Effect.h"

namespace MyDirectX
{
	TextRenderer Effect::s_TextRenderer;

	void Effect::Init(ID3D12Device* pDevice)
	{
		s_TextRenderer.Init(pDevice);
	}

	void Effect::Shutdown()
	{
		s_TextRenderer.Shutdown();
	}
}