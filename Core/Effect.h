#pragma once
#include "pch.h"
#include "TextRenderer.h"

namespace MyDirectX
{
	class Effect
	{
	public:
		static void Init(ID3D12Device* pDevice);
		static void Shutdown();

		static TextRenderer s_TextRenderer;

	};

}