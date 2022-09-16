#pragma once
#include "pch.h"
#include "MotionBlur.h"
#include "TemporalAA.h"
#include "TemporalEffects.h"
#include "Denoiser.h"
#include "PostEffects.h"
#include "TextRenderer.h"
#include "ForwardPlusLighting.h"
#include "ParticleEffectManager.h"

namespace MyDirectX
{
	class Effects
	{
	public:
		static void Init(ID3D12Device* pDevice);
		static void Shutdown();

		// Effects
		static MotionBlur s_MotionBlur;
		static TemporalAA s_TemporalAA;
		static TemporalEffects s_TemporalEffects;
		static Denoiser s_Denoier;
		// Post effects
		static PostEffects s_PostEffects;

		// Text
		static TextRenderer s_TextRenderer;

		// Light
		static ForwardPlusLighting s_ForwardPlusLighting;

		// Particle effects
		static ParticleEffects::ParticleEffectManager s_ParticleEffectManager;
	};
}
