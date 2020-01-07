// <<Introduction to 3D ...>>
// MathHelper
#pragma once
//#include <Windows.h>
//#include <DirectXMath.h>
//#include <string>
//#include <cstdint>
#include "pch.h"

namespace MyDirectX
{
	class MyMath
	{
	public:
		// returns random float in [0, 1)
		static float RandF()
		{
			return (float)rand() / (float)RAND_MAX;
		}

		// returns random float in [a, b)
		static float RandF(float a, float b)
		{
			return a + RandF() * (b - a);
		}

		static int Rand(int a, int b)
		{
			return a + rand() % ((b - a) + 1);
		}
	};

	// simple struct to represent a material for the demos. 
	class MyMaterial
	{
	public:
		// unique material name for lookup
		std::string name;

		// index into constant buffer corresponding to this material
		int cbvIndex = -1;

		// index into SRV heap for diffuse texture
		int diffuseSRVHeapIndex = -1;

		// index into SRV heap for normal texture
		int normalSRVHeapIndex = -1;

		// material constant buffer data used for shading
		DirectX::XMFLOAT4 diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
		DirectX::XMFLOAT3 Fresnel0 = { 0.04f, 0.04f, 0.04f };
		float metalness = 0.0f;
		float roughness = 0.25f;
	};

	class Helpers
	{
	public:
		static std::vector<CD3DX12_STATIC_SAMPLER_DESC> GetStaticSamplers();
	};
}

