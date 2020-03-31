#pragma once
#include "GpuBuffer.h"

namespace MyDirectX
{
	class ReadbackBuffer : public GpuBuffer
	{
	public:
		virtual ~ReadbackBuffer() { Destroy(); }

		void Create(ID3D12Device *pDevice, const std::wstring &name, uint32_t numElements, uint32_t elementSize);

		void* Map();

		void Unmap();

	protected:
		virtual void CreateDerivedViews(ID3D12Device *pDevice) override {}
	};

}
