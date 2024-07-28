#pragma once
#include "pch.h"
#include "Color.h"

namespace MyDirectX
{
	class SamplerDesc : public D3D12_SAMPLER_DESC
	{
	public:
		static std::map<size_t, D3D12_CPU_DESCRIPTOR_HANDLE> s_SamplerCache;

		// these defaults match the default values for HLSL-defined root signature static samplers.
		// So not overriding them here means you can safely not define them in HLSL
		SamplerDesc()
		{
			Filter = D3D12_FILTER_ANISOTROPIC;
			AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			MipLODBias = 0.0f;
			MaxAnisotropy = 16;
			ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;	// Filter = D3D12_FILTER_COMPARISON_XXXX
			BorderColor[0] = 1.0f;
			BorderColor[1] = 1.0f;
			BorderColor[2] = 1.0f;
			BorderColor[3] = 1.0f;
			MinLOD = 0.0f;
			MaxLOD = D3D12_FLOAT32_MAX;
		}

		void SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE addressMode)
		{
			AddressU = addressMode;
			AddressV = addressMode;
			AddressW = addressMode;
		}

		void SetBorderColor(Color borderColor)
		{
			BorderColor[0] = borderColor.R();
			BorderColor[1] = borderColor.G();
			BorderColor[2] = borderColor.B();
			BorderColor[3] = borderColor.A();
		}

		// allocate new descriptor as needed, return handle to existing descriptor when possible
		D3D12_CPU_DESCRIPTOR_HANDLE CreateDescriptor(ID3D12Device *pDevice);

		// create descriptor in place (no deduplication). Handle must preallocated.
		void CreateDescriptor(ID3D12Device* pDevice, D3D12_CPU_DESCRIPTOR_HANDLE handle);
	};

}
