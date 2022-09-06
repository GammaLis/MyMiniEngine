#include "Effects.h"
#include "GfxCommon.h"

namespace MyDirectX
{
	/// Effects
	// Motion blur
	MotionBlur Effects::s_MotionBlur;
	// Temporal antialiasing
	TemporalAA Effects::s_TemporalAA;
	// Temporal effects
	TemporalEffects Effects::s_TemporalEffects;
	// Post effects
	PostEffects Effects::s_PostEffects;

	// Text
	TextRenderer Effects::s_TextRenderer;

	// Forward+ lighting
	ForwardPlusLighting Effects::s_ForwardPlusLighting;

	// Particle effects
	ParticleEffects::ParticleEffectManager Effects::s_ParticleEffectManager;

	void Effects::Init(ID3D12Device* pDevice)
	{
		s_MotionBlur.Init(pDevice);
		s_TemporalAA.Init(pDevice);
		s_PostEffects.Init(pDevice);

		s_TextRenderer.Init(pDevice);
		s_ForwardPlusLighting.Init(pDevice);

		uint32_t maxWidth, maxHeight;
		GfxStates::GetWHFromResolution(Resolutions::k2160p, maxWidth, maxHeight);
		s_ParticleEffectManager.Init(pDevice, maxWidth, maxHeight);

		s_TemporalAA.SetTAAMethod(TemporalAA::ETAAMethod::MSTAA); // INTELTAA
	}

	void Effects::Shutdown()
	{
		s_MotionBlur.Shutdown();
		s_TemporalAA.Shutdown();
		s_TemporalEffects.Shutdown();
		s_PostEffects.Shutdown();

		s_TextRenderer.Shutdown();
		s_ForwardPlusLighting.Shutdown();

		s_ParticleEffectManager.Shutdown();
	}

	void Effects::Resize(UINT width, UINT height)
	{
		s_TemporalEffects.Resize(width, height);
	}
}
