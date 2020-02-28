#pragma once
#include "ParticleShaderStructs.h"

namespace MyDirectX
{
	namespace ParticleEffects
	{
		struct ParticleEffectProperties
		{
			ParticleEffectProperties()
			{
				ZeroMemory(this, sizeof(*this));
				MinStartColor = Color(0.8f, 0.8f, 1.0f);
				MaxStartColor = Color(0.9f, 0.9f, 1.0f);
				MinEndColor = Color(1.0f, 1.0f, 1.0f);
				MaxEndColor = Color(1.0f, 1.0f, 1.0f);
				EmitProperties = *CreateEmissionProperties();	// properties passe to the shader
				EmitRate = 200;
				LifeMinMax = XMFLOAT2(1.0f, 2.0f);
				MassMinMax = XMFLOAT2(0.5f, 1.0f);
				Size = Vector4(0.07f, 0.7f, 0.8f, 0.8f);	// (start size min, start size max, end size min, end size max)
				Spread = XMFLOAT3(0.5f, 1.5f, 0.1f);
				TexturePath = L"Textures/sparkTex.dds";
				TotalActiveLifeTime = 20.0f;
				Velocity = Vector4(0.5f, 3.0f, -0.5f, 3.0f);	// (x velocity min, x velocity max, y velocity min, y velocity max)
			}

			Color MinStartColor;
			Color MaxStartColor;
			Color MinEndColor;
			Color MaxEndColor;
			EmissionProperties EmitProperties;
			float EmitRate;
			XMFLOAT2 LifeMinMax;
			XMFLOAT2 MassMinMax;
			Vector4 Size;
			XMFLOAT3 Spread;
			std::wstring TexturePath;
			float TotalActiveLifeTime;
			Vector4 Velocity;			
		};
	}
}