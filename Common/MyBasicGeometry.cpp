#include "MyBasicGeometry.h"

using namespace MyDirectX::Geometry;

void MyBasicGeometry::BasicBox(float width, float height, float depth, Mesh& mesh)
{
	// 1. vertices
	Vertex vertices[24];

	float w = 0.5f * width;
	float h = 0.5f * height;
	float d = 0.5f * depth;

	//                   // position	// normal			// tangent			// uv
	// fill in the front face vertex data
	vertices[0] = Vertex{ -w, -h, -d,	0.0f, 0.0f, -1.0f,	+1.0f, 0.0f, 0.0f,	0.0f, 1.0f };
	vertices[1] = Vertex{ -w, +h, -d,	0.0f, 0.0f, -1.0f,	+1.0f, 0.0f, 0.0f,	0.0f, 0.0f };
	vertices[2] = Vertex{ +w, +h, -d,	0.0f, 0.0f, -1.0f,	+1.0f, 0.0f, 0.0f,	1.0f, 0.0f };
	vertices[3] = Vertex{ +w, -h, -d,	0.0f, 0.0f, -1.0f,	+1.0f, 0.0f, 0.0f,	1.0f, 1.0f };
	// fill in the back face vertex data
	vertices[4] = Vertex{ -w, -h, +d,	0.0f, 0.0f, +1.0f,	-1.0f, 0.0f, 0.0f,	1.0f, 1.0f };
	vertices[5] = Vertex{ +w, -h, +d,	0.0f, 0.0f, +1.0f,	-1.0f, 0.0f, 0.0f,	0.0f, 1.0f };
	vertices[6] = Vertex{ +w, +h, +d,	0.0f, 0.0f, +1.0f,	-1.0f, 0.0f, 0.0f,	0.0f, 0.0f };
	vertices[7] = Vertex{ -w, +h, +d,	0.0f, 0.0f, +1.0f,	-1.0f, 0.0f, 0.0f,	1.0f, 0.0f };
	// fill in the top face vertex data
	vertices[8] = Vertex{ -w, +h, -d,	0.0f, +1.0f, 0.0f,	+1.0f, 0.0f, 0.0f,	0.0f, 1.0f };
	vertices[9] = Vertex{ -w, +h, +d,	0.0f, +1.0f, 0.0f,	+1.0f, 0.0f, 0.0f,	0.0f, 0.0f };
	vertices[10] = Vertex{ +w, +h, +d,	0.0f, +1.0f, 0.0f,	+1.0f, 0.0f, 0.0f,	1.0f, 0.0f };
	vertices[11] = Vertex{ +w, +h, -d,	0.0f, +1.0f, 0.0f,	+1.0f, 0.0f, 0.0f,	1.0f, 1.0f };
	// fill in the bottom face vertex data.
	vertices[12] = Vertex{ -w, -h, -d,	0.0f, -1.0f, 0.0f,	-1.0f, 0.0f, 0.0f,	1.0f, 1.0f };
	vertices[13] = Vertex{ +w, -h, -d,	0.0f, -1.0f, 0.0f,	-1.0f, 0.0f, 0.0f,	0.0f, 1.0f };
	vertices[14] = Vertex{ +w, -h, +d,	0.0f, -1.0f, 0.0f,	-1.0f, 0.0f, 0.0f,	0.0f, 0.0f };
	vertices[15] = Vertex{ -w, -h, +d,	0.0f, -1.0f, 0.0f,	-1.0f, 0.0f, 0.0f,	1.0f, 0.0f };
	// fill in the left face vertex data.
	vertices[16] = Vertex{ -w, -h, +d,	-1.0f, 0.0f, 0.0f,	0.0f, 0.0f, -1.0f,	0.0f, 1.0f };
	vertices[17] = Vertex{ -w, +h, +d,	-1.0f, 0.0f, 0.0f,	0.0f, 0.0f, -1.0f,	0.0f, 0.0f };
	vertices[18] = Vertex{ -w, +h, -d,	-1.0f, 0.0f, 0.0f,	0.0f, 0.0f, -1.0f,	1.0f, 0.0f };
	vertices[19] = Vertex{ -w, -h, -d,	-1.0f, 0.0f, 0.0f,	0.0f, 0.0f, -1.0f,	1.0f, 1.0f };
	// fill in the right face vertex data.
	vertices[20] = Vertex{ +w, -h, -d,	+1.0f, 0.0f, 0.0f,	0.0f, 0.0f, +1.0f,	0.0f, 1.0f };
	vertices[21] = Vertex{ +w, +h, -d,	+1.0f, 0.0f, 0.0f,	0.0f, 0.0f, +1.0f,	0.0f, 0.0f };
	vertices[22] = Vertex{ +w, +h, +d,	+1.0f, 0.0f, 0.0f,	0.0f, 0.0f, +1.0f,	1.0f, 0.0f };
	vertices[23] = Vertex{ +w, -h, +d,	+1.0f, 0.0f, 0.0f,	0.0f, 0.0f, +1.0f,	1.0f, 1.0f };

	mesh.vertices.assign(&vertices[0], &vertices[24]);

	// 2. indices
	unsigned indices[36];
	// fill in the front face index data
	indices[0] = 0; indices[1] = 1; indices[2] = 2;
	indices[3] = 0; indices[4] = 2; indices[5] = 3;
	// fill in the back face index data
	indices[6] = 4; indices[7] = 5;  indices[8] = 6;
	indices[9] = 4; indices[10] = 6; indices[11] = 7;
	// fill in the top face index data
	indices[12] = 8; indices[13] = 9;  indices[14] = 10;
	indices[15] = 8; indices[16] = 10; indices[17] = 11;
	// fill in the bottom face index data
	indices[18] = 12; indices[19] = 13; indices[20] = 14;
	indices[21] = 12; indices[22] = 14; indices[23] = 15;
	// fill in the left face index data
	indices[24] = 16; indices[25] = 17; indices[26] = 18;
	indices[27] = 16; indices[28] = 18; indices[29] = 19;
	// fill in the right face index data
	indices[30] = 20; indices[31] = 21; indices[32] = 22;
	indices[33] = 20; indices[34] = 22; indices[35] = 23;

	mesh.indices.assign(&indices[0], &indices[36]);
}

void MyBasicGeometry::BasicSphere(float radius, unsigned sliceCount, unsigned stackCount, Mesh& mesh)
{
	mesh.vertices.clear();
	mesh.indices.clear();

	//
	// Compute the vertices stating at the top pole and moving down the stacks.
	//

	// Poles: note that there will be texture coordinate distortion as there is
	// not a unique point on the texture map to assign to the pole when mapping
	// a rectangular texture onto a sphere.
	Vertex topVertex{	0.0f, +radius, 0.0f,	0.0f, +1.0f, 0.0f,	1.0f, 0.0f, 0.0f,	0.0f, 0.0f };
	Vertex bottomVertex{0.0f, -radius, 0.0f,	0.0f, -1.0f, 0.0f,	1.0f, 0.0f, 0.0f,	0.0f, 1.0f };

	mesh.vertices.push_back(topVertex);

	float phiStep = XM_PI / stackCount;
	float thetaStep = 2.0f * XM_PI / sliceCount;

	// Compute vertices for each stack ring (do not count the poles as rings).
	for (UINT i = 1; i <= stackCount - 1; ++i)
	{
		float phi = i * phiStep;

		// Vertices of ring.
		for (UINT j = 0; j <= sliceCount; ++j)
		{
			float theta = j * thetaStep;

			Vertex v;

			// spherical to cartesian
			v.position.x = radius * sinf(phi) * cosf(theta);
			v.position.y = radius * cosf(phi);
			v.position.z = radius * sinf(phi) * sinf(theta);

			// Partial derivative of P with respect to theta
			v.tangent.x = -radius * sinf(phi) * sinf(theta);
			v.tangent.y = 0.0f;
			v.tangent.z = +radius * sinf(phi) * cosf(theta);

			XMVECTOR T = XMLoadFloat3(&v.tangent);
			XMStoreFloat3(&v.tangent, XMVector3Normalize(T));

			XMVECTOR p = XMLoadFloat3(&v.position);
			XMStoreFloat3(&v.normal, XMVector3Normalize(p));

			v.uv.x = theta / XM_2PI;
			v.uv.y = phi / XM_PI;

			mesh.vertices.push_back(v);
		}
	}

	mesh.vertices.push_back(bottomVertex);

	//
	// Compute indices for top stack.  The top stack was written first to the vertex buffer
	// and connects the top pole to the first ring.
	//

	for (UINT i = 1; i <= sliceCount; ++i)
	{
		mesh.indices.push_back(0);
		mesh.indices.push_back(i + 1);
		mesh.indices.push_back(i);
	}

	//
	// Compute indices for inner stacks (not connected to poles).
	//

	// Offset the indices to the index of the first vertex in the first ring.
	// This is just skipping the top pole vertex.
	UINT baseIndex = 1;
	UINT ringVertexCount = sliceCount + 1;
	for (UINT i = 0; i < stackCount - 2; ++i)
	{
		for (UINT j = 0; j < sliceCount; ++j)
		{
			mesh.indices.push_back(baseIndex + i * ringVertexCount + j);
			mesh.indices.push_back(baseIndex + i * ringVertexCount + j + 1);
			mesh.indices.push_back(baseIndex + (i + 1) * ringVertexCount + j);

			mesh.indices.push_back(baseIndex + (i + 1) * ringVertexCount + j);
			mesh.indices.push_back(baseIndex + i * ringVertexCount + j + 1);
			mesh.indices.push_back(baseIndex + (i + 1) * ringVertexCount + j + 1);
		}
	}

	//
	// Compute indices for bottom stack.  The bottom stack was written last to the vertex buffer
	// and connects the bottom pole to the bottom ring.
	//

	// South pole vertex was added last.
	UINT southPoleIndex = (UINT)mesh.vertices.size() - 1;

	// Offset the indices to the index of the first vertex in the last ring.
	baseIndex = southPoleIndex - ringVertexCount;

	for (UINT i = 0; i < sliceCount; ++i)
	{
		mesh.indices.push_back(southPoleIndex);
		mesh.indices.push_back(baseIndex + i);
		mesh.indices.push_back(baseIndex + i + 1);
	}
}

void MyBasicGeometry::BasicCylinder(float bottomRadius, float topRadius, float height, unsigned slickCount, unsigned stackCount, Mesh& mesh)
{
	// ÔÝÊ±¿ÕÖÃ
}

void MyBasicGeometry::BasicGrid(float width, float depth, unsigned m, unsigned n, Mesh& mesh)
{
	UINT vertexCount = m * n;
	UINT faceCount = (m - 1) * (n - 1) * 2;

	//
	// Create the vertices.
	//

	float halfWidth = 0.5f * width;
	float halfDepth = 0.5f * depth;

	float dx = width / (n - 1);
	float dz = depth / (m - 1);

	float du = 1.0f / (n - 1);
	float dv = 1.0f / (m - 1);

	mesh.vertices.resize(vertexCount);
	for (UINT i = 0; i < m; ++i)
	{
		float z = halfDepth - i * dz;
		for (UINT j = 0; j < n; ++j)
		{
			float x = -halfWidth + j * dx;

			mesh.vertices[i * n + j].position = XMFLOAT3(x, 0.0f, z);
			mesh.vertices[i * n + j].normal  = XMFLOAT3(0.0f, 1.0f, 0.0f);
			mesh.vertices[i * n + j].tangent = XMFLOAT3(1.0f, 0.0f, 0.0f);

			// Stretch texture over grid.
			mesh.vertices[i * n + j].uv.x = j * du;
			mesh.vertices[i * n + j].uv.y = i * dv;
		}
	}

	//
	// Create the indices.
	//

	mesh.indices.resize(faceCount * 3); // 3 indices per face

	// Iterate over each quad and compute indices.
	UINT k = 0;
	for (UINT i = 0; i < m - 1; ++i)
	{
		for (UINT j = 0; j < n - 1; ++j)
		{
			mesh.indices[k] = i * n + j;
			mesh.indices[k + 1] = i * n + j + 1;
			mesh.indices[k + 2] = (i + 1) * n + j;

			mesh.indices[k + 3] = (i + 1) * n + j;
			mesh.indices[k + 4] = i * n + j + 1;
			mesh.indices[k + 5] = (i + 1) * n + j + 1;

			k += 6; // next quad
		}
	}
}

void MyBasicGeometry::BasicFullScreenQuad(Mesh& mesh)
{
	mesh.vertices.resize(4);
	mesh.indices.resize(6);

	// Position coordinates specified in NDC space.
	mesh.vertices[0] = Vertex(
		-1.0f, -1.0f,  0.0f,
		 0.0f,  0.0f, -1.0f,
		 1.0f,  0.0f,  0.0f,
		 0.0f,  1.0f);

	mesh.vertices[1] = Vertex(
		-1.0f, +1.0f,  0.0f,
		 0.0f,  0.0f, -1.0f,
		 1.0f,  0.0f,  0.0f,
		 0.0f,  0.0f);

	mesh.vertices[2] = Vertex(
		+1.0f, +1.0f,  0.0f,
		 0.0f,  0.0f, -1.0f,
		 1.0f,  0.0f,  0.0f,
		 1.0f,  0.0f);

	mesh.vertices[3] = Vertex(
		+1.0f, -1.0f,  0.0f,
		 0.0f,  0.0f, -1.0f,
		 1.0f,  0.0f,  0.0f,
		 1.0f,  1.0f);

	mesh.indices[0] = 0;
	mesh.indices[1] = 1;
	mesh.indices[2] = 2;

	mesh.indices[3] = 0;
	mesh.indices[4] = 2;
	mesh.indices[5] = 3;
}
