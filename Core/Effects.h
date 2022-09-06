#pragma once
#include "pch.h"
#include "MotionBlur.h"
#include "TemporalAA.h"
#include "TemporalEffects.h"
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
		static void Resize(UINT width, UINT height);

		// effects
		static MotionBlur s_MotionBlur;
		static TemporalAA s_TemporalAA;
		static TemporalEffects s_TemporalEffects;
		// post effects
		static PostEffects s_PostEffects;

		// text
		static TextRenderer s_TextRenderer;

		// light
		static ForwardPlusLighting s_ForwardPlusLighting;

		// particle effects
		static ParticleEffects::ParticleEffectManager s_ParticleEffectManager;
	};

}
