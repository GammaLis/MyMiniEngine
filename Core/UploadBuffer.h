#pragma once
#include "GpuResource.h"

namespace MyDirectX
{
	/**
	*	Description: An upload buffer is visible to both the CPU and the GPU, but because the memory is write combined,
	* you should avoid reading data with CPU. An upload buffer is intended for moving data to a default GPU buffer. You can
	* read from a file directly into an upload buffer, rather than reading into regular cached memory, copying that to
	* an upload buffer, and copying that to the CPU.
	*/
	class UploadBuffer : public GpuResource
	{
	public:
		virtual ~UploadBuffer() { Destroy(); }

		void Create(ID3D12Device *pDevice, const std::wstring &name, size_t bufferSize);

		void *Map(void);
		void Unmap(size_t begin = 0, size_t end = -1);

		size_t GetBufferSize() const { return m_BufferSize; }

	protected:
		size_t m_BufferSize = 0;
	};
}
