#include "Model.h"
#include <DirectXPackedVector.h>

namespace MyDirectX
{
	using namespace Math;
	using namespace DirectX::PackedVector;

	/**
		XMCOLOR - ARGB Color; 8-8-8-8 bit unsigned normalized integer components packed into a 32 bit integer
		XMXDECN4 - 10-10-10-2 bit normalized components packed into a 32 bit integer
		XMFLOAT4 - DXGI_FORMAT_R32G32B32A32_FLOAT
	*/
	struct Vertex
	{
		XMFLOAT3 position;
		XMCOLOR color;
	};

	void Model::Cleanup()
	{
		m_VertexBuffer.Destroy();
		m_IndexBuffer.Destroy();
	}

	void Model::Create(ID3D12Device* pDevice)
	{
		Vertex vertices[] =
		{
			Vertex{XMFLOAT3( .0f, +.6f, 0.f), XMCOLOR(1.0f ,0.0f, 0.0f, 1.0f)},
			Vertex{XMFLOAT3(-.6f, -.6f, 0.f), XMCOLOR(0.0f, 1.0f, 0.0f, 1.0f)},
			Vertex{XMFLOAT3(+.6f, -.6f, 0.f), XMCOLOR(0.0f, 0.0f, 1.0f, 1.0f)},
		};

		uint16_t indices[] = { 0, 1, 2 };

		uint32_t vertexCount = _countof(vertices);
		uint32_t vertexByteSize = sizeof(vertices);
		uint32_t vertexStride = sizeof(Vertex);
		m_VertexStride = vertexStride;

		uint32_t indexCount = _countof(indices);
		uint32_t indexStride = sizeof(uint16_t);
		m_IndexCount = indexCount;

		m_VertexBuffer.Create(pDevice, L"VertexBuffer", vertexCount, vertexStride, &vertices);
		m_IndexBuffer.Create(pDevice, L"IndexBuffer", indexCount, indexStride, &indices);
	}
}
