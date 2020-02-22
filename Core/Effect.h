#pragma once
#include "pch.h"
#include "MotionBlur.h"
#include "TemporalAA.h"
#include "TextRenderer.h"
#include "ForwardPlusLighting.h"

namespace MyDirectX
{
	class Effect
	{
	public:
		static void Init(ID3D12Device* pDevice);
		static void Shutdown();

		// effects
		static MotionBlur s_MotionBlur;
		static TemporalAA s_TemporalAA;

		// text
		static TextRenderer s_TextRenderer;

		// light
		static ForwardPlusLighting s_ForwardPlusLighting;
	};

}