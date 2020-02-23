#include "Effects.h"

namespace MyDirectX
{
	// effects
	// motion blur
	MotionBlur Effects::s_MotionBlur;
	// temporal antialiasing
	TemporalAA Effects::s_TemporalAA;

	// text
	TextRenderer Effects::s_TextRenderer;

	// forward+ lighting
	ForwardPlusLighting Effects::s_ForwardPlusLighting;

	void Effects::Init(ID3D12Device* pDevice)
	{
		s_MotionBlur.Init(pDevice);
		s_TemporalAA.Init(pDevice);

		s_TextRenderer.Init(pDevice);
		s_ForwardPlusLighting.Init(pDevice);
	}

	void Effects::Shutdown()
	{
		s_MotionBlur.Shutdown();
		s_TemporalAA.Shutdown();

		s_TextRenderer.Shutdown();
		s_ForwardPlusLighting.Shutdown();
	}
}