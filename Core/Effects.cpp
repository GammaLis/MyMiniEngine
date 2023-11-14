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
	// Denoiser
	Denoiser Effects::s_Denoier;
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
		if (GfxStates::s_bEnableTemporalEffects)
			s_TemporalEffects.Init(pDevice);
		if (Denoiser::s_bEnabled)
			s_Denoier.Init(pDevice);

		s_TextRenderer.Init(pDevice);
		s_ForwardPlusLighting.Init(pDevice);

		uint32_t maxWidth, maxHeight;
		GfxStates::GetSizeFromResolution(Resolutions::k2160p, maxWidth, maxHeight);
		s_ParticleEffectManager.Init(pDevice, maxWidth, maxHeight);

		s_TemporalAA.SetTAAMethod(TemporalAA::ETAAMethod::MSTAA); // INTELTAA
	}

	void Effects::Shutdown()
	{
		s_MotionBlur.Shutdown();
		s_TemporalAA.Shutdown();
		s_TemporalEffects.Shutdown();
		s_Denoier.Shutdown();
		s_PostEffects.Shutdown();

		s_TextRenderer.Shutdown();
		s_ForwardPlusLighting.Shutdown();

		s_ParticleEffectManager.Shutdown();
	}
}
