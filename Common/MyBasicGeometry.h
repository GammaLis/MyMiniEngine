//***************************************************************************************
// GeometryGenerator.h by Frank Luna (C) 2011 All Rights Reserved.
//   
// Defines a static class for procedurally generating the geometry of 
// common mathematical objects.
//
// All triangles are generated "outward" facing.  If you want "inward" 
// facing triangles (for example, if you want to place the camera inside
// a sphere to simulate a sky), you will need to:
//   1. Change the Direct3D cull mode or manually reverse the winding order.
//   2. Invert the normal.
//   3. Update the texture coordinates and tangent vectors.
//***************************************************************************************
#pragma once

#include "pch.h"

using DirectX::XMFLOAT2;
using DirectX::XMFLOAT3;

namespace MyDirectX
{
	namespace Geometry
	{
		struct Vertex
		{
			Vertex() = default;
			Vertex(const XMFLOAT3& pos, const XMFLOAT3& norm, const XMFLOAT3& tan, const XMFLOAT2& texuv)
				: position{ pos }, normal{ norm }, tangent{ tan }, uv{ texuv }
			{  }
			Vertex(
				float px, float py, float pz,
				float nx, float ny, float nz,
				float tx, float ty, float tz,
				float u, float v)
				: position{ px, py, pz }, normal{ nx, ny, nz }, tangent{ tx, ty, tz }, uv{ u, v }
			{  }

			XMFLOAT3 position;
			XMFLOAT3 normal;
			XMFLOAT3 tangent;
			XMFLOAT2 uv;
		};

		struct Mesh
		{
			std::vector<Vertex> vertices;
			std::vector<int> indices;
		};

		class MyBasicGeometry
		{
		public:
			static void BasicBox(float width, float height, float depth, Mesh& mesh);
			static void BasicSphere(float radius, unsigned sliceCount, unsigned stackCount, Mesh& mesh);
			static void BasicCylinder(float bottomRadius, float topRadius, float height, unsigned slickCount, unsigned stackCount, Mesh& mesh);
			static void BasicGrid(float width, float depth, unsigned m, unsigned n, Mesh& mesh);
			static void BasicFullScreenQuad(Mesh& mesh);
		};
	}
}
