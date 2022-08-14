#include "Accelerations.h"
#include "Utility.h"
#include <fstream>

#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"

#define ALIGNMENT 64
#define AS_LOG 0

namespace rtrt
{
	bool IntersectTriangle(Ray &ray, Intersection &isect, const Triangle &tri, const uint inst_prim)
	{
		// Moeller-Trumbore ray/triangle intersection algorithm
		const float3 edge1 = tri.v1 - tri.v0;
		const float3 edge2 = tri.v2 - tri.v0;
		const float3 h = glm::cross(ray.rd, edge2);
		const float a = glm::dot(edge1, h);
		if (std::abs(a) < 0.0001f) // ray parallel to triangle
			return false;

		const float f = 1.0f / a;
		const float3 s = ray.ro - tri.v0;
		const float u = f * glm::dot(s, h);
		if (u < 0 || u > 1)
			return false;

		const float3 q = glm:: cross(s, edge1);
		const float v = f * glm::dot(ray.rd, q);
		if (v < 0 || u + v > 1)
			return false;

		const float t = f * glm::dot(edge2, q);
		if (t > 0.0001f && t < ray.tMax)
		{
			ray.tMax = t;
			isect.t = t;
			isect.u = u; isect.v = v;
			isect.inst_prim = inst_prim;

			return true;
		}

		return false;
	}

	inline float IntersectAABB(const Ray &ray, const float3 &bmin, const float3 &bmax)
	{
		// "Slab test" ray/AABB intersection
		float tx1 = (bmin.x - ray.ro.x) * ray.rcpD.x, tx2 = (bmax.x - ray.ro.x) * ray.rcpD.x;
		float tmin = std::min(tx1, tx2), tmax = std::max(tx1, tx2);

		float ty1 = (bmin.y - ray.ro.y) * ray.rcpD.y, ty2 = (bmax.y - ray.ro.y) * ray.rcpD.y;
		tmin = std::max(tmin, std::min(ty1, ty2));
		tmax = std::min(tmax, std::max(ty1, ty2));

		float tz1 = (bmin.z - ray.ro.z) * ray.rcpD.z, tz2 = (bmax.z - ray.ro.z) * ray.rcpD.z;
		tmin = std::max(tmin, std::min(tz1, tz2));
		tmax = std::min(tmax, std::max(tz1, tz2));

		bool bIntersect = tmax >= tmin && tmin < ray.tMax && tmax > 0;
		return bIntersect ? tmin : Ray::TMAX;
	}

	float IntersectAABB_SSE(const Ray &ray, const __m128 &bmin4, const __m128 &bmax4)
	{
		static __m128 mask4 = _mm_cmpeq_ps(_mm_setzero_ps(), _mm_set_ps(1, 0, 0, 0));
		__m128 t0 = _mm_mul_ps(_mm_sub_ps(_mm_and_ps(bmin4, mask4), ray.o4), ray.rd4);
		__m128 t1 = _mm_mul_ps(_mm_sub_ps(_mm_and_ps(bmax4, mask4), ray.o4), ray.rd4);
		__m128 vmax4 = _mm_max_ps(t0, t1), vmin4 = _mm_min_ps(t0, t1);
		float tmax = std::min(vmax4.m128_f32[0], std::min(vmax4.m128_f32[1], vmax4.m128_f32[2]));
		float tmin = std::max(vmin4.m128_f32[0], std::max(vmin4.m128_f32[1], vmin4.m128_f32[2]));

		bool bIntersect = tmax >= tmin && tmin < ray.tMax&& tmax > 0;
		return bIntersect ? tmin : Ray::TMAX;
	}


	/// Surface
	char g_Font[51][5][6];
	bool g_bFontInited = false;
	int  g_Translation[256];

#pragma  region Surface
	Surface::Surface(int w, int h, uint* buffer) : pixels(buffer), width(w), height(h)
	{
		if (buffer == nullptr)
		{
			pixels = (uint*)_aligned_malloc(w * h * sizeof(uint), ALIGNMENT);
			ownBuffer = true; // Needs to be deleted in destructor
		}
	}

	Surface::Surface(const char* file) : pixels(nullptr), width(1), height(1)
	{
#if 1
		FILE* f;
		errno_t errorCode = fopen_s(&f, file, "rb");
#else
		// 'strcpy': This function or variable may be unsafe.Consider using strcpy_s instead.
		// To disable deprecation, use _CRT_SECURE_NO_WARNINGS.
		f = fopen(file, "rb");
#endif
		if (errorCode != 0)
		{
			Utility::Printf("File not found: %s", file);
			while (1) exit(0);
		}
		fclose(f);
		LoadTexture(file);
	}

	void Surface::LoadTexture(const char* file)
	{
		int n;
		uchar* data = stbi_load(file, &width, &height, &n, 0);
		if (data)
		{
			pixels = (uint*)_aligned_malloc(width * height * sizeof(uint), ALIGNMENT);
			ownBuffer = true;
			const int s = width * height;
			if (n == 1) // Greyscale
			{
				for (int i = 0; i < s; ++i)
				{
					const uchar p = data[i];
					pixels[i] = p | (p << 8) | (p << 16);
				}
			}
			else
			{
				for (int i = 0; i < s; ++i)
				{
					pixels[i] = (data[i * n + 0] << 16) + (data[i * n + 1] << 8) + data[i * n + 2];
				}
			}
		}

		stbi_image_free(data);
	}

	Surface::~Surface()
	{
		if (ownBuffer)
			_aligned_free(pixels); // Free only if we allocated the buffer ourselves
	}

	void Surface::Clear(uint c)
	{
		const int s = width * height;
		for (int i = 0; i < s; ++i)
			pixels[i] = c;
	}

	void Surface::Plot(int x, int y, uint c)
	{
		if (x < 0 || x >= width || y < 0 || y >= height)
			return;
		int index = flipY ? (x + (height - 1 - y) * width) : (x + y * width);
		pixels[index] = c;
	}

	void Surface::Print(const char* s, int x, int y, uint c)
	{
		static int c_offset = 'A' - 'a';
		if (!g_bFontInited)
		{
			InitCharset();
			g_bFontInited = true;
		}
		uint* t = pixels + x + y * width;
		for (int i = 0, imax = (int)strlen(s); i < imax; ++i)
		{
			int pos = 0;
			if ((s[i] >= 'A') && (s[i] <= 'Z'))
				pos = g_Translation[(ushort)(s[i] - c_offset)];
			else
				pos = g_Translation[(ushort)s[i]];
			uint* a = t;
			const char* u = (const char*)g_Font[pos];
			for (int v = 0; v < 5; ++v, ++u, a += width)
			{
				for (int h = 0; h < 5; ++h)
				{
					if (*u++ == 'o')
					{
						*(a + h) = c;
						*(a + h * width) = 0;
					}
				}
			}
		}
	}

	void Surface::Line(float x0, float y0, float x1, float y1, uint c)
	{
		// TODO
	}

	void Surface::CopyTo(Surface* dst, int x, int y)
	{
		uint* pDst = dst->pixels;
		uint* pSrc = pixels;
		if (pDst && pSrc)
		{
			int srcWidth = width, srcHeight = height;
			int dstWidth = dst->width, dstHeight = dst->height;
			if ((srcWidth + x) > dstWidth)
				srcWidth = dstWidth - x;
			if ((srcHeight + y) > dstHeight)
				srcHeight = dstHeight - y;
			if (x < 0)
			{
				pSrc -= x; srcWidth += x; x = 0;
			}
			if (y < 0)
			{
				pSrc -= y * srcWidth; srcHeight += y; y = 0;
			}
			if (srcWidth > 0 && srcHeight > 0)
			{
				pDst += x + y * dstWidth;
				for (int y = 0; y < srcHeight; ++y)
				{
					memcpy(pDst, pSrc, srcWidth * 4);
					pDst += dstWidth;
					pSrc += srcWidth;
				}
			}
		}
	}

	void Surface::SetChar(int c, const char* c1, const char* c2, const char* c3, const char* c4, const char* c5)
	{
		strcpy_s(g_Font[c][0], c1);
		strcpy_s(g_Font[c][1], c2);
		strcpy_s(g_Font[c][2], c3);
		strcpy_s(g_Font[c][3], c4);
		strcpy_s(g_Font[c][4], c5);
	}

	void Surface::InitCharset()
	{
		SetChar(0, ":ooo:", "o:::o", "ooooo", "o:::o", "o:::o");
		SetChar(1, "oooo:", "o:::o", "oooo:", "o:::o", "oooo:");
		SetChar(2, ":oooo", "o::::", "o::::", "o::::", ":oooo");
		SetChar(3, "oooo:", "o:::o", "o:::o", "o:::o", "oooo:");
		SetChar(4, "ooooo", "o::::", "oooo:", "o::::", "ooooo");
		SetChar(5, "ooooo", "o::::", "ooo::", "o::::", "o::::");
		SetChar(6, ":oooo", "o::::", "o:ooo", "o:::o", ":ooo:");
		SetChar(7, "o:::o", "o:::o", "ooooo", "o:::o", "o:::o");
		SetChar(8, "::o::", "::o::", "::o::", "::o::", "::o::");
		SetChar(9, ":::o:", ":::o:", ":::o:", ":::o:", "ooo::");
		SetChar(10, "o::o:", "o:o::", "oo:::", "o:o::", "o::o:");
		SetChar(11, "o::::", "o::::", "o::::", "o::::", "ooooo");
		SetChar(12, "oo:o:", "o:o:o", "o:o:o", "o:::o", "o:::o");
		SetChar(13, "o:::o", "oo::o", "o:o:o", "o::oo", "o:::o");
		SetChar(14, ":ooo:", "o:::o", "o:::o", "o:::o", ":ooo:");
		SetChar(15, "oooo:", "o:::o", "oooo:", "o::::", "o::::");
		SetChar(16, ":ooo:", "o:::o", "o:::o", "o::oo", ":oooo");
		SetChar(17, "oooo:", "o:::o", "oooo:", "o:o::", "o::o:");
		SetChar(18, ":oooo", "o::::", ":ooo:", "::::o", "oooo:");
		SetChar(19, "ooooo", "::o::", "::o::", "::o::", "::o::");
		SetChar(20, "o:::o", "o:::o", "o:::o", "o:::o", ":oooo");
		SetChar(21, "o:::o", "o:::o", ":o:o:", ":o:o:", "::o::");
		SetChar(22, "o:::o", "o:::o", "o:o:o", "o:o:o", ":o:o:");
		SetChar(23, "o:::o", ":o:o:", "::o::", ":o:o:", "o:::o");
		SetChar(24, "o:::o", "o:::o", ":oooo", "::::o", ":ooo:");
		SetChar(25, "ooooo", ":::o:", "::o::", ":o:::", "ooooo");
		SetChar(26, ":ooo:", "o::oo", "o:o:o", "oo::o", ":ooo:");
		SetChar(27, "::o::", ":oo::", "::o::", "::o::", ":ooo:");
		SetChar(28, ":ooo:", "o:::o", "::oo:", ":o:::", "ooooo");
		SetChar(29, "oooo:", "::::o", "::oo:", "::::o", "oooo:");
		SetChar(30, "o::::", "o::o:", "ooooo", ":::o:", ":::o:");
		SetChar(31, "ooooo", "o::::", "oooo:", "::::o", "oooo:");
		SetChar(32, ":oooo", "o::::", "oooo:", "o:::o", ":ooo:");
		SetChar(33, "ooooo", "::::o", ":::o:", "::o::", "::o::");
		SetChar(34, ":ooo:", "o:::o", ":ooo:", "o:::o", ":ooo:");
		SetChar(35, ":ooo:", "o:::o", ":oooo", "::::o", ":ooo:");
		SetChar(36, "::o::", "::o::", "::o::", ":::::", "::o::");
		SetChar(37, ":ooo:", "::::o", ":::o:", ":::::", "::o::");
		SetChar(38, ":::::", ":::::", "::o::", ":::::", "::o::");
		SetChar(39, ":::::", ":::::", ":ooo:", ":::::", ":ooo:");
		SetChar(40, ":::::", ":::::", ":::::", ":::o:", "::o::");
		SetChar(41, ":::::", ":::::", ":::::", ":::::", "::o::");
		SetChar(42, ":::::", ":::::", ":ooo:", ":::::", ":::::");
		SetChar(43, ":::o:", "::o::", "::o::", "::o::", ":::o:");
		SetChar(44, "::o::", ":::o:", ":::o:", ":::o:", "::o::");
		SetChar(45, ":::::", ":::::", ":::::", ":::::", ":::::");
		SetChar(46, "ooooo", "ooooo", "ooooo", "ooooo", "ooooo");
		SetChar(47, "::o::", "::o::", ":::::", ":::::", ":::::"); // Tnx Ferry
		SetChar(48, "o:o:o", ":ooo:", "ooooo", ":ooo:", "o:o:o");
		SetChar(49, "::::o", ":::o:", "::o::", ":o:::", "o::::");
		char c[] = "abcdefghijklmnopqrstuvwxyz0123456789!?:=,.-() #'*/";
		int i;
		for (i = 0; i < 256; i++) g_Translation[i] = 45;
		for (i = 0; i < 50; i++) g_Translation[(uchar)c[i]] = i;
	}
#pragma endregion


	/// Bounds
	void Bounds::Union(const Triangle& tri)
	{
		bmin = glm::min(bmin, tri.v0);
		bmin = glm::min(bmin, tri.v1);
		bmin = glm::min(bmin, tri.v2);

		bmax = glm::max(bmax, tri.v0);
		bmax = glm::max(bmax, tri.v1);
		bmax = glm::max(bmax, tri.v2);
	}

	void* BVHNode::operator new(size_t size)
	{
		return _aligned_malloc(size, ALIGNMENT);
	}

	void BVHNode::operator delete(void* ptr)
	{
		_aligned_free(ptr);
	}


	/// Mesh
	Mesh::Mesh(const char* objFile, const char* texFile)
	{
		// Bare-bones obj file loader; only supports very basic meshes
		m_Texture = std::make_unique<Surface>(texFile);

		FILE* file = nullptr;
		int ret = fopen_s(&file, objFile, "r");
		if (ret != 0)
		{
			Utility::Printf("File %s not found!", objFile);
			while (1) exit(-1);
		}

		m_Triangles.reset(new Triangle[NTri]);
		m_TrianglesEx.reset(new TriangleEx[NTri]);
		m_TriCount = 0;

		std::unique_ptr<float2[]> UV(new float2[N]);
		m_Normals.reset(new float3[N]);
		m_Positions.reset(new float3[N]);
		int UVs = 0, Ns = 0, Ps = 0, a, b, c, d, e, f, g, h, i;
		while (!feof(file))
		{
			char line[512] = { 0 };
			fgets(line, 511, file);
			if (line == strstr(line, "vt "))
				sscanf_s(line + 3, "%f %f", &UV[UVs].x, &UV[UVs].y), UVs++;
			else if (line == strstr(line, "vn "))
				sscanf_s(line + 3, "%f %f %f", &m_Normals[Ns].x, &m_Normals[Ns].y, &m_Normals[Ns].z), Ns++;
			else if (line[0] == 'v')
				sscanf_s(line + 2, "%f %f %f", &m_Positions[Ps].x, &m_Positions[Ps].y, &m_Positions[Ps].z), Ps++;
			if (line[0] != 'f')
				continue;
			else
				sscanf_s(line + 2, "%i/%i/%i %i/%i/%i %i/%i/%i", &a, &b, &c, &d, &e, &f, &g, &h, &i);

			auto& tri = m_Triangles[m_TriCount];
			auto& triEx = m_TrianglesEx[m_TriCount];
			tri.v0 = m_Positions[a - 1], triEx.uv0 = UV[b - 1], triEx.n0 = m_Normals[c - 1];
			tri.v1 = m_Positions[d - 1], triEx.uv1 = UV[e - 1], triEx.n1 = m_Normals[f - 1];
			tri.v2 = m_Positions[g - 1], triEx.uv2 = UV[h - 1], triEx.n2 = m_Normals[i - 1];

			++m_TriCount;
		}
		fclose(file);
	}

	// Basic constructor, for top-down TLAS construction
	Mesh::Mesh(uint primCount)
	{
		m_TriCount = primCount;
		m_Triangles.reset(new Triangle[primCount]);
		m_TrianglesEx.reset(new TriangleEx[primCount]);
	}

	void Mesh::Init()
	{
		m_BVH.reset(new BVH(this));
	}


	struct Bin
	{
		Bounds bounds;
		int triCount = 0;
	};
	static constexpr int s_Bins = 8;


	/// BVH
	BVH::BVH(Mesh* pMesh)
	{
		m_Mesh = pMesh;

		int triCount = pMesh->m_TriCount;
		m_BVHNodes.reset(new BVHNode[triCount * 2]);
		m_TriIndices.reset(new uint[triCount]);

		Build();
	}

	bool BVH::Intersect(Ray& ray, Intersection &isect, uint instanceIndex)
	{
		BVHNode* node = &m_BVHNodes[0], * stack[128];
		uint stackCount = 0;
		bool bIntersect = false;
		while (true)
		{
			if (node->IsLeaf())
			{
				for (uint i = node->leftFirst, imax = node->leftFirst + node->triCount; i < imax; ++i)
				{
					uint index = m_TriIndices[i];
					uint inst_prim = (instanceIndex << 20) | index;
					bIntersect |= IntersectTriangle(ray, isect, m_Mesh->m_Triangles[index], inst_prim);
				}
				if (stackCount == 0)
					break;
				else
					node = stack[--stackCount];
			}
			else
			{
				BVHNode* pChild0 = &m_BVHNodes[node->leftFirst];
				BVHNode* pChild1 = &m_BVHNodes[node->leftFirst + 1];
#if 1
				float d0 = IntersectAABB_SSE(ray, pChild0->bmin4, pChild0->bmax4);
				float d1 = IntersectAABB_SSE(ray, pChild1->bmin4, pChild1->bmax4);
#else
				float d0 = IntersectAABB(ray, pChild0->bmin, pChild0->bmax);
				float d1 = IntersectAABB(ray, pChild1->bmin, pChild1->bmax);
#endif

				if (d0 > d1)
				{
					std::swap(d0, d1);
					std::swap(pChild0, pChild1);
				}
				if (d0 == Ray::TMAX)
				{
					if (stackCount == 0)
						break;
					else
						node = stack[--stackCount];
				}
				else
				{
					node = pChild0;
					if (d1 != Ray::TMAX)
						stack[stackCount++] = pChild1;
				}
			}
		}

		return bIntersect;
	}

	void BVH::Build()
	{
		// Reset node pool
		m_NodesUsed = 2;
		// Populate triangle index array
		int triCount = m_Mesh->m_TriCount;
		for (int i = 0; i < triCount; ++i)
			m_TriIndices[i] = i;
		// Calculate triangle centroids for partitioning
		for (int i = 0; i < triCount; ++i)
		{
			auto& tri = m_Mesh->m_Triangles[i];
			tri.c = (tri.v0 + tri.v1 + tri.v2) * 0.3333f;
		}
		// Assign all triangles to root node
		constexpr int rootNodeIdx = 0;
		BVHNode& root = m_BVHNodes[rootNodeIdx];
		root.leftFirst = 0, root.triCount = triCount;
		UpdateNodeBounds(rootNodeIdx);
		// Subdivide recursively
		Subdivide(rootNodeIdx);
	}

	void BVH::UpdateNodeBounds(uint nodeIndex)
	{
		BVHNode& node = m_BVHNodes[nodeIndex];
		node.bmin = float3(g_Max);
		node.bmax = float3(g_Min);
		for (int i = node.leftFirst, imax = node.leftFirst + node.triCount; i < imax; ++i)
		{
			uint leafTriIdx = m_TriIndices[i];
			const Triangle& leafTri = m_Mesh->m_Triangles[leafTriIdx];
			node.bmin = glm::min(node.bmin, leafTri.v0); node.bmax = glm::max(node.bmax, leafTri.v0);
			node.bmin = glm::min(node.bmin, leafTri.v1); node.bmax = glm::max(node.bmax, leafTri.v1);
			node.bmin = glm::min(node.bmin, leafTri.v2); node.bmax = glm::max(node.bmax, leafTri.v2);
		}
	}

	void BVH::Subdivide(uint nodeIndex)
	{
		auto& node = m_BVHNodes[nodeIndex];
		if (node.triCount == 1)
			return;

		// Determine split axis using SAH
		int axis = 0;
		float splitPos;
		float splitCost = FindBestSplitPlane(node, axis, splitPos);
		float noSplitCost = node.CalculateNodeCost();
		// Checks if the best split cost is actually an improvement over not splitting
		if (splitCost >= noSplitCost)
			return;

		// In-place partition
		int i = node.leftFirst;
		int j = node.leftFirst + node.triCount - 1;
		while (i <= j)
		{
			if (m_Mesh->m_Triangles[m_TriIndices[i]].c[axis] < splitPos)
				++i;
			else
				std::swap(m_TriIndices[i], m_TriIndices[j--]);
		}

		// Abort split if one of the sides is empty
		// Theoretically, the split in the middle can yield an empty box on the left or the right size.
		// Not easy to come up with such a situation though.
		int leftCount = i - node.leftFirst;
		if (leftCount == 0 || leftCount == node.triCount)
			return;

		// Creat child nodes
		int lChildIdx = m_NodesUsed++;
		int rChildIdx = m_NodesUsed++;

		auto& lChildNode = m_BVHNodes[lChildIdx];
		lChildNode.leftFirst = node.leftFirst;
		lChildNode.triCount = leftCount;
		UpdateNodeBounds(lChildIdx);

		auto& rChildNode = m_BVHNodes[rChildIdx];
		rChildNode.leftFirst = i;
		rChildNode.triCount = node.triCount - leftCount;
		UpdateNodeBounds(rChildIdx);

		// Set the primitive count of the parent node to 0
		node.leftFirst = lChildIdx;
		node.triCount = 0;

		Subdivide(lChildIdx);
		Subdivide(rChildIdx);
	}

	float BVH::FindBestSplitPlane(BVHNode& node, int& axis, float& splitPos)
	{
		float bestCost = g_Max;
		for (int a = 0; a < 3; ++a)
		{
			// Centroid bounds
			// The first split plane candidate is on the first centroid, the last one on the last centroid
			float cmin = g_Max, cmax = g_Min;
			for (int i = node.leftFirst, imax = node.leftFirst+node.triCount; i < imax; ++i)
			{
				auto& tri = m_Mesh->m_Triangles[m_TriIndices[i]];
				cmin = std::min(cmin, tri.c[a]);
				cmax = std::max(cmax, tri.c[a]);
			}

			if (cmin == cmax)
				continue;

			// Populate the bins
			Bin bins[s_Bins];
			float scale = s_Bins / (cmax - cmin);
			for (int i = node.leftFirst, imax = node.leftFirst + node.triCount; i < imax; ++i)
			{
				auto& tri = m_Mesh->m_Triangles[m_TriIndices[i]];
				int binIdx = std::min((int)((tri.c[a] - cmin) * scale), s_Bins - 1);
				Bin& bin = bins[binIdx];
				++bin.triCount;
				bin.bounds.Union(tri.v0); bin.bounds.Union(tri.v1); bin.bounds.Union(tri.v2);
			}

			float areas0[s_Bins - 1], areas1[s_Bins - 1];
			float triCount0[s_Bins - 1], triCount1[s_Bins - 1];
			float n0 = 0, n1 = 0;
			Bounds b0, b1;
			for (int i = 0; i < s_Bins - 1; ++i)
			{
				const auto& bin0 = bins[i];
				b0.Union(bin0.bounds);
				n0 += bin0.triCount;
				areas0[i] = b0.Area(); triCount0[i] = n0;

				const auto& bin1 = bins[s_Bins - 1 - i];
				b1.Union(bin1.bounds);
				n1 += bin1.triCount;
				areas1[s_Bins - 2 - i] = b1.Area(); triCount1[s_Bins - 2 - i] = n1;
			}

			scale = 1.0f / scale;
			for (int i = 1; i < s_Bins - 1; ++i)
			{
				float cost = areas0[i] * triCount0[i] + areas1[i] * triCount1[i];
				if (cost < bestCost)
				{
					axis = a;
					splitPos = cmin + scale * (i + 1);
					bestCost = cost;
				}
			}
		}

		return bestCost;
	}

	void BVH::Refit()
	{
		for (int i= m_NodesUsed-1; i >= 0; --i)
		{
			auto& node = m_BVHNodes[i];
			if (!node.IsValid())
				continue;

			if (node.IsLeaf())
			{
				// Leaf node - adjust bounds to contained triangles
				UpdateNodeBounds(i);
			}
			else
			{
				// Interior node - adjust bounds to child node bounds
				auto& node0 = m_BVHNodes[node.leftFirst];
				auto& node1 = m_BVHNodes[node.leftFirst + 1];
				node.bmin = glm::min(node0.bmin, node1.bmin);
				node.bmax = glm::max(node0.bmax, node1.bmax);				
			}
		}
	}


	/// BVHInstance
	BVHInstance::BVHInstance(BVH* blas, uint index)
	{
		Init(blas, index);
	}

	void BVHInstance::Init(BVH* blas, uint index, const glm::mat4 &transform)
	{
		m_BVH = blas;
		m_Index = index;
		SetTransform(transform);
	}

	void BVHInstance::SetTransform(const glm::mat4& transform)
	{
		m_Transform = transform;
		m_InvTransform = glm::inverse(transform);
		// Calculate world-space bounds using the new matrix
		float3 bmin = m_BVH->AABB().bmin, bmax = m_BVH->AABB().bmax;
		m_Bounds.Reset();
		for (int i = 0; i < 8; ++i)
		{
			m_Bounds.Union(transform * float4(
				i & 1 ? bmax.x : bmin.x,
				i & 2 ? bmax.y : bmin.y,
				i & 4 ? bmax.z : bmin.z, 1.0f));
		}
	}

	bool BVHInstance::Intersect(Ray& ray, Intersection& isect)
	{
		// Backup ray and transform original
		Ray backupRay = ray;
		ray.ro = m_InvTransform * float4(ray.ro, 1.0f);
		ray.rd = m_InvTransform * float4(ray.rd, 0.0f);
		ray.rcpD = 1.0f / ray.rd;

		// Trace ray through the BVH
		bool bIntersect = m_BVH->Intersect(ray, isect, m_Index);

		// Restore ray origin and direction
		backupRay.tMax = ray.tMax;
		ray = backupRay;

		return bIntersect;
	}

	/// TLAS
	TLAS::TLAS(BVHInstance* bvhList, int N)
	{
		m_BLAS = bvhList;
		m_BLASCount = N;
		// Allocate TLAS nodes
		m_TLASNodes.reset(new TLASNode[2 * N]);
		m_NodeIndices.reset(new uint[N]);
		m_NodesUsed = 2;
	}

	void TLAS::Build()
	{
		// Assign a TLASLeaf node to each BLAS
		m_NodesUsed = 1;
		for (uint i = 0; i < m_BLASCount; ++i)
		{
			m_NodeIndices[i] = m_NodesUsed;
			auto& tlasNode = m_TLASNodes[m_NodesUsed];
			tlasNode.bmin = m_BLAS[i].m_Bounds.bmin;
			tlasNode.bmax = m_BLAS[i].m_Bounds.bmax;
			tlasNode.leftRight = 0;
			tlasNode.BLASIndex = i;
			++m_NodesUsed;
		}

		// Use agglomerative clustering to build the TLAS
		int nodeCount = m_BLASCount;
		int A = 0, B = FindBestMatch(nodeCount, A);
#if AS_LOG
		std::fstream f;
		f.open("Pairs.txt", f.out | f.in);
#endif

		while (nodeCount > 1)
		{
			int C = FindBestMatch(nodeCount, B);
			if (A == C)
			{
				// Found a pair, create a new TLAS interior node
				int nodeIdxA = m_NodeIndices[A], nodeIdxB = m_NodeIndices[B];
				TLASNode& nodeA = m_TLASNodes[nodeIdxA], & nodeB = m_TLASNodes[nodeIdxB];
				TLASNode& newNode = m_TLASNodes[m_NodesUsed];
				newNode.bmin = glm::min(nodeA.bmin, nodeB.bmin);
				newNode.bmax = glm::max(nodeA.bmax, nodeB.bmax);
				newNode.leftRight = (nodeIdxA | (nodeIdxB << 16));

				m_NodeIndices[A] = m_NodesUsed++;
				m_NodeIndices[B] = m_NodeIndices[nodeCount - 1];
#if AS_LOG
				if (f.is_open()) { f << nodeIdxA << " " << nodeIdxB << std::endl; }
#endif

				--nodeCount;
				B = FindBestMatch(nodeCount, A);
			}
			else
			{
				A = B; B = C;
			}
		}
#if AS_LOG
		f.close();
#endif
		// Copy last remaining node to the root node
		m_TLASNodes[0] = m_TLASNodes[m_NodeIndices[A]];

	}

	bool TLAS::Intersect(Ray& ray, Intersection& isect)
	{
		// Calculate reciprocal ray directions for fast AABB intersection
		ray.rcpD = float3(1.0f / ray.rd.x, 1.0f / ray.rd.y, 1.0f / ray.rd.z);
		// Use a local stack instead of a recursive function
		TLASNode* node = &m_TLASNodes[0], * stack[64];
		uint stackCount = 0;
		bool bIntersect = false;
		while (true)
		{
			if (node->IsLeaf())
			{
				// Current node is a leaf, intersect BLAS
				bIntersect |= m_BLAS[node->BLASIndex].Intersect(ray, isect);
				// Pop a node from the stack, terminate if none left
				if (stackCount == 0)
					break;
				else
					node = stack[--stackCount];
			}
			else
			{
				// Current node is a interior node, visit child nodes, ordered
				TLASNode* pChild0 = &m_TLASNodes[(node->leftRight & 0xFFFF)];
				TLASNode* pChild1 = &m_TLASNodes[(node->leftRight >> 16)];
				float d0 = IntersectAABB(ray, pChild0->bmin, pChild0->bmax);
				float d1 = IntersectAABB(ray, pChild1->bmin, pChild1->bmax);
				if (d0 > d1)
				{
					std::swap(d0, d1);
					std::swap(pChild0, pChild1);
				}
				if (d0 == g_Max)
				{
					// Missed both child nodes, pop a node from the stack
					if (stackCount == 0)
						break;
					else
						node = stack[--stackCount];
				}
				else
				{
					// Visit near node, push the far node if the ray intersects it
					node = pChild0;
					if (d1 < g_Max)
						stack[stackCount++] = pChild1;
				}
			}
		}

		return bIntersect;
	}

	int TLAS::FindBestMatch(int N, int A)
	{
		// Find BLAS that, when joined with A, forms the smallest AABB
		float smallest = g_Max;
		int bestB = -1;
		for (int B = 0; B < N; ++B)
		{
			if (B != A)
			{
				float3 bmin = glm::min(m_TLASNodes[m_NodeIndices[A]].bmin, m_TLASNodes[m_NodeIndices[B]].bmin);
				float3 bmax = glm::max(m_TLASNodes[m_NodeIndices[A]].bmax, m_TLASNodes[m_NodeIndices[B]].bmax);
				float3 ext = bmax - bmin;
				float surfaceArea = ext.x * ext.y + ext.y * ext.z + ext.z * ext.x;
				if (surfaceArea < smallest)
				{
					bestB = B;
					smallest = surfaceArea;
				}
			}
		}

		return bestB;
	}

	static KdTree* s_KdTree = nullptr;
	static Mesh* s_Mesh = nullptr;
	void TLAS::BuildQuick()
	{
#if 0
		// Single-threaded code, for reference
		// Assign a TLASLeaf node to each BLAS
		m_NodesUsed = 1;
		for (uint i = 0; i < m_BLASCount; ++i)
		{
			auto& tlasNode = m_TLASNodes[m_NodesUsed];
			tlasNode.bmin = m_BLAS[i].m_Bounds.bmin;
			tlasNode.bmax = m_BLAS[i].m_Bounds.bmax;
			tlasNode.leftRight = 0;
			tlasNode.BLASIndex = i; // makes it a leaf
			++m_NodesUsed;
		}

		// Build a Kd-Tree over the TLAS nodes
		
		if (s_KdTree == nullptr)
			s_KdTree = new KdTree(m_TLASNodes.get() + 1, m_NodesUsed - 1, 1 /*skip root*/);
		s_KdTree->Rebuild();
		// Use the kd-tree for fast agglomerative clustering
		float sa = g_Max;
		uint best = 0, workLeft = m_BLASCount;
		uint A = 1, B = s_KdTree->FindNearest(A, best, sa);
		while (true)
		{
			int C = s_KdTree->FindNearest(B, best = A, sa);
			if (A == C)
			{
				// Found a pair, create a new TLAS interior node
				TLASNode& newNode = m_TLASNodes[m_NodesUsed];
				newNode.bmin = glm::min(m_TLASNodes[A].bmin, m_TLASNodes[B].bmin);
				newNode.bmax = glm::max(m_TLASNodes[A].bmax, m_TLASNodes[B].bmax);
				newNode.leftRight = (A | (B << 16));
				if (workLeft-- == 2)
					break;
				s_KdTree->RemoveLeaf(A);
				s_KdTree->RemoveLeaf(B);
				s_KdTree->Add(A = m_NodesUsed);
				B = s_KdTree->FindNearest(A, best = 0, sa = g_Max);
				++m_NodesUsed;
			}
			else
			{
				A = B; B = C;
			}
		}

		// Copy last remaining node to the root node
		m_TLASNodes[0] = m_TLASNodes[m_NodesUsed];

#else
		// 2. building the TLAS top-down
		if (s_Mesh == nullptr)
			s_Mesh = new Mesh(m_BLASCount);
		for (uint i = 0; i < m_BLASCount; ++i)
		{
			s_Mesh->m_Triangles[i].v0 = m_BLAS[i].m_Bounds.bmin;
			s_Mesh->m_Triangles[i].v1 = m_BLAS[i].m_Bounds.bmax;
			s_Mesh->m_Triangles[i].v2 = (m_BLAS[i].m_Bounds.bmin + m_BLAS[i].m_Bounds.bmax) * 0.5f; // degenerate but with the correct AABB
		}
		if(s_Mesh->m_BVH == nullptr)
		{
			s_Mesh->m_BVH.reset(new BVH(s_Mesh));
		}
		s_Mesh->m_BVH->Build();
		for (uint i = 0, imax = s_Mesh->m_BVH->m_NodesUsed; i < imax; ++i)
		{
			const auto& bvhNode = s_Mesh->m_BVH->m_BVHNodes[i];
			auto& tlasNode = m_TLASNodes[i];
			if (bvhNode.IsLeaf())
			{
				tlasNode.BLASIndex = s_Mesh->m_BVH->m_TriIndices[bvhNode.leftFirst];
				tlasNode.leftRight = 0; // mark as leaf
			}
			else
			{
				tlasNode.leftRight = bvhNode.leftFirst | ((bvhNode.leftFirst + 1) << 16);
			}
		}

#endif

	}


#pragma region KdTree
	KdTree::KdNode::KdNode()
	{
		left = right = parax = 0; splitPos = 0;
		bmin4 = _mm_setzero_ps();
		bmax4 = _mm_setzero_ps();
		minSize4 = _mm_setzero_ps();
	}

	KdTree::KdNode& KdTree::KdNode::operator=(const KdNode& other)
	{
		left = other.left; right = other.right;
		parax = other.parax; splitPos = other.splitPos;

		bmin4 = other.bmin4;
		bmax4 = other.bmax4;
		minSize4 = other.minSize4;

		return *this;
	}

	void* KdTree::KdNode::operator new(size_t size)
	{
		return _aligned_malloc(size, ALIGNMENT);
	}
	void KdTree::KdNode::operator delete(void* ptr)
	{
		_aligned_free(ptr);
	}

	/// KdTree
	uint* KdTree::s_Leaf = nullptr;

	uint DominantAxis(const float2 &v)
	{
		float x = std::abs(v.x), y = std::abs(v.y);
		return x > y ? 0 : 1;
	}
	uint DominantAxis(const float3 v)
	{
		float x = std::abs(v.x), y = std::abs(v.y), z = std::abs(v.z);
		uint axis = 0;
		float m = x;
		if (m < y) { axis = 1; m = y; }
		if (m < z) { axis = 2; }
		return axis;
	}

	KdTree::KdTree(TLASNode* tlasNodes, uint N, uint O)
	{
		// Allocate space for nodes and indices
		m_TLAS = tlasNodes;		// copy of the original array of tlas nodes
		m_BLASCount = N;		// m_BLASCount remains constant
		m_TLASCount = N;		// m_TLASCount will grow during clustering
		m_Offset = O;			// index of the first TLAS node in the array

		if (s_Leaf == nullptr) s_Leaf = new uint[1000];

		m_Nodes.reset(new KdNode[2 * N]); // pre-allocate kdtree nodes, aligned
		m_TLASIndices.reset(new uint[2 * N + 64]); // tlas array indirection so we can store ranges of nodes in leaves
	}

	void KdTree::Rebuild()
	{
		// We'll assume we get the same number of TLAS nodes each time
		m_TLASCount = m_BLASCount;
		for (uint i = 0; i < m_BLASCount; ++i)
			m_TLASIndices[i] = i;

		// Subdivide root node
		auto& rootNode = m_Nodes[0];
		rootNode.first = 0; rootNode.count = m_BLASCount; rootNode.parax = 7;
		m_NodeCount = 1; // root = 0, so node 1 is the first node we can create

		Subdivide(rootNode); // recursively subdivide the root node

		// "Each node keeps it's cluster's minimum box sizes in each axis"
		for (int i = (int)m_NodeCount-1; i >= 0; --i)
		{
			auto& node = m_Nodes[i];
			if (node.IsLeaf())
			{
				node.minSize = float3(g_Max);
				for (uint j = node.first, jmax = node.first + node.count; j < jmax; ++j)
				{
					uint index = m_TLASIndices[j];
					s_Leaf[index + m_Offset] = i; // we can find m_TLAS[index] in leaf node[i]
					float3 size = (m_TLAS[index].bmax - m_TLAS[index].bmin) * 0.5f;
					node.minSize = glm::min(node.minSize, size);
				}
			}
			else
			{
				node.minSize = glm::min(m_Nodes[node.left].minSize, m_Nodes[node.right].minSize);
			}
		}
	}

	void KdTree::Subdivide(KdNode& node, uint depth)
	{
		// Update node bounds
		node.bmin = float3(g_Max), node.bmax = float3(g_Min);
		node.minSize = float3(g_Max);
		for (uint i = 0; i < node.count; ++i)
		{
			const TLASNode &tlasNode = m_TLAS[ m_TLASIndices[node.first + i]];
			float3 c = (tlasNode.bmin + tlasNode.bmax) * 0.5f;
			node.bmin = glm::min(node.bmin, c);
			node.bmax = glm::max(node.bmax, c);
			node.minSize = glm::min(node.minSize, 0.5f * (tlasNode.bmax - tlasNode.bmin));
		}

		// Terminate if we are down to 1 tlas
		if (node.count < 2)
			return;

		// Claim left and right child nodes
		uint axis = DominantAxis(node.bmax - node.bmin);
		float center = (node.bmin[axis] + node.bmax[axis]) * 0.5f;
#if 1
		// Try to balance (works quite well but doesn't seem to pay off)
		if (node.count > 150)
		{
			// Count how many would go to the left
			int leftCount = 0;
			for (uint i = 0; i < node.count; ++i)
			{
				const TLASNode& tlasNode = m_TLAS[m_TLASIndices[node.first + i]];
				float3 c = (tlasNode.bmin + tlasNode.bmax) * 0.5f;
				if (c[axis] <= center)
					++leftCount;
			}
			float ratio = std::max(0.15f, std::min(0.85f, (float)leftCount / node.count));
			center = ratio * node.bmin[axis] + (1.0f - ratio) * node.bmax[axis];
		}
#endif
		uint leftChildCount = Partition(node, axis, center);
		if (leftChildCount == 0 || leftChildCount == node.count) // split failed
			return;

		node.left  = m_NodeCount++;
		node.right = m_NodeCount++;
		node.parax = (node.parax & 0xFFFFFFF8) + axis;
		node.splitPos = center;

		Subdivide(m_Nodes[node.left] , depth + 1);
		Subdivide(m_Nodes[node.right], depth + 1);
	}

	uint KdTree::Partition(KdNode& node, uint axis, float splitPos)
	{
		int N = node.count, first = node.first, last = first + N;
		if (N < 3)
			last = first + 1;
		else
		{
			while (true)
			{
				const auto& tlasNode = m_TLAS[m_TLASIndices[first]];
				float c = (tlasNode.bmin[axis] + tlasNode.bmax[axis]) * 0.5f;
				if (c > splitPos)
					std::swap(m_TLASIndices[first], m_TLASIndices[--last]);
				else
					++first;

				if (first >= last) break;
			}
		}
		KdNode& lNode = m_Nodes[m_NodeCount];
		lNode.first = node.first; lNode.count = last - node.first;
		KdNode& rNode = m_Nodes[m_NodeCount + 1];
		rNode.first = last; rNode.count = node.count - lNode.count;
		lNode.parax = rNode.parax = (((uint)(&node - m_Nodes.get())) << 3) | 7;

		return lNode.count;
	}

	void KdTree::Add(uint index)
	{
		// TODO...
	}

	void KdTree::RemoveLeaf(uint index)
	{
		// TODO...
	}

	int KdTree::FindNearest(uint A, uint& startB, float& startSA)
	{
		// TODO...
		return 0;
	}

#pragma endregion

}
