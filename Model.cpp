#include "Model.h"

using namespace MyDirectX;
using namespace Math;

struct Vertex
{
	Vector3 position;
	Vector3 color;
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
		Vertex{Vector3( .0f, +.6f, 0.f), Vector3(0.f, 1.2f, 0.f)},
		Vertex{Vector3(-.6f, -.6f, 0.f), Vector3(1.2f, 0.f, 0.f)},		
		Vertex{Vector3(+.6f, -.6f, 0.f), Vector3(0.f, 0.f, 1.2f)},
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
