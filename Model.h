#pragma once
#include "pch.h"
#include "GpuBuffer.h"

namespace MyDirectX
{
	class Model
	{
	public:
		Model()
		{
			Cleanup();
		}
		~Model() { Cleanup(); }

		void Cleanup();

		StructuredBuffer m_VertexBuffer;
		ByteAddressBuffer m_IndexBuffer;
		uint32_t m_VertexStride = 0;
		uint32_t m_IndexCount = 0;

		virtual void Create(ID3D12Device *pDevice);
		virtual void Load() {  }

	private:

	};

}
