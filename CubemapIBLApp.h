#pragma once
#include "IComputingApp.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "ColorBuffer.h"

namespace MyDirectX
{
	class CubemapIBLApp : public IComputingApp
	{
	public:
		void Init(const std::wstring &fileName, UINT width, UINT height, DXGI_FORMAT format = DXGI_FORMAT_R32G32B32_FLOAT);

		virtual void Run() override;

		int SaveToFile(const std::string& fileName);

	private:

		static const UINT GroupSize = 16;

		RootSignature m_IrradianceMapRS;
		ComputePSO m_IrradianceMapPSO;

		D3D12_CPU_DESCRIPTOR_HANDLE m_SRV;

		ColorBuffer m_IrradianceMap;

		UINT m_Width = 0;
		UINT m_Height = 0;
	};

}
