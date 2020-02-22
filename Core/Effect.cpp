#include "Effect.h"

namespace MyDirectX
{
	// effects
	// motion blur
	MotionBlur Effect::s_MotionBlur;
	// temporal antialiasing
	TemporalAA Effect::s_TemporalAA;

	// text
	TextRenderer Effect::s_TextRenderer;

	// forward+ lighting
	ForwardPlusLighting Effect::s_ForwardPlusLighting;

	void Effect::Init(ID3D12Device* pDevice)
	{
		s_MotionBlur.Init(pDevice);
		s_TemporalAA.Init(pDevice);

		s_TextRenderer.Init(pDevice);
		s_ForwardPlusLighting.Init(pDevice);
	}

	void Effect::Shutdown()
	{
		s_MotionBlur.Shutdown();
		s_TemporalAA.Shutdown();

		s_TextRenderer.Shutdown();
		s_ForwardPlusLighting.Shutdown();
	}
}