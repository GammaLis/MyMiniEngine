#pragma once
#include "Accelerations.h"
#include "Accelerations.h"
#include "pch.h"
#include "Math/GLMath.h"

namespace rtrt
{
	// Basic types
	using uchar = unsigned char;
	using uint = unsigned int;
	using ushort = unsigned short;

	using float2 = glm::vec2;
	using float3 = glm::vec3;
	using float4 = glm::vec4;

	constexpr float g_Min = -1e10f;
	constexpr float g_Max = +1e10f;

	struct Triangle;
	class Mesh;

	// 32-bit surface container
	class Surface
	{
		enum { OWNER = 1 };
	public:
		Surface() = default;
		Surface(int w, int h, uint* buffer = nullptr);
		Surface(const char* file);
		~Surface();

		void InitCharset();
		void SetChar(int c, const char* c1, const char* c2, const char* c3, const char* c4, const char* c5);
		void Print(const char* s, int x, int y, uint c);
		void Clear(uint c);
		void Line(float x0, float y0, float x1, float y1, uint c);
		void Plot(int x, int y, uint c);
		void LoadTexture(const char* file);
		void CopyTo(Surface* dst, int x, int y);
		void Box(int x0, int y0, int x1, int y1, uint color);
		void Bar(int x0, int y0, int x1, int y1, uint color);

		// Attributes
		uint* pixels = nullptr;
		int width = 1, height = 1;
		bool ownBuffer = false;
		bool flipY = false;
	};

	struct Ray
	{
		static constexpr float TMAX = 1e5f;
		static constexpr float TMIN = 1e-3f;

		Ray()
		{
			o4 = d4 = rd4 = _mm_set1_ps(1);
		}
		Ray(float3 o, float3 d) : ro(o), rd(d), rcpD(1.0f / d) {  }
		Ray(const Ray &other) : o4(other.o4), d4(other.d4), rd4(other.rd4) {  }

		Ray& operator=(const Ray &other)
		{
			o4	= other.o4;
			d4	= other.d4;
			rd4 = other.rd4;

			return *this;
		}

		// float3 ro, rd, rcpD;
		union
		{
			struct { float3 ro; float dummy; };
			__m128 o4;
		};
		union 
		{
			struct { float3 rd; float dummy; };
			__m128 d4;
		};
		union 
		{
			struct { float3 rcpD; float dummy; };
			__m128 rd4;
		};
		float tMin = TMIN, tMax = TMAX;
	};

	struct Bounds
	{
		float3 bmin = float3(g_Max);
		float3 bmax = float3(g_Min);

		Bounds() = default;
		Bounds(float3 c0, float3 c1)
		{
			if (c0.x > c1.x) std::swap(c0.x, c1.x);
			if (c0.y > c1.y) std::swap(c0.y, c1.y);
			if (c0.z > c1.z) std::swap(c0.z, c1.z);

			bmin = c0; bmax = c1;
		}

		float Area() const
		{
			const float3 extent = glm::max(bmax - bmin, float3(0.0f));
			return (extent.x * extent.y + extent.y * extent.z + extent.x * extent.z) * 2.0f;
		}

		void Union(const Bounds & other)
		{
			bmin = glm::min(bmin, other.bmin);
			bmax = glm::max(bmax, other.bmax);
		}

		void Union(const float3 &point)
		{
			bmin = glm::min(bmin, point);
			bmax = glm::max(bmax, point);
		}

		void Union(const Triangle &tri);

		bool Valid() const { return (bmin.x < bmax.x) && (bmin.y < bmax.y) && (bmin.z < bmax.z); }

		float3 Center() const { return (bmin + bmax) * 0.5f; }
		float3 Extent() const
		{
			return glm::max(bmax - bmin, float3(0.0f));
		}

		void Reset()
		{
			bmin = float3(g_Max);
			bmax = float3(g_Min);
		}

		static Bounds Union(const Bounds & a, const Bounds & b)
		{
			Bounds bounds{ a };
			bounds.Union(b);
			return bounds;
		}
	};

	// Minimalist triangle struct
	struct Triangle
	{
		float3 v0, v1, v2;
		float3 c; // centroid

		Bounds AABB() const
		{
			Bounds b;
			b.Union(v0); b.Union(v1); b.Union(v2);
			return b;
		}
	};
	// Additional triangle data, for texturing and shading
	struct TriangleEx
	{
		float2 uv0, uv1, uv2;
		float3 n0, n1, n2;
	};

	// Intersection record, carefully tuned to be 16 bytes in size
	struct Intersection
	{
		float t;		// Intersection distance along ray
		float u, v;		// barycentric coordinates of the intersection
		uint inst_prim; // instance index (12 bit) and primitive index (20 bit)
	};

	// 32-bytes BVH node struct
	struct BVHNode
	{
		BVHNode()
		{
			bmin = float3(g_Max); leftFirst = 0;
			bmax = float3(g_Min); triCount = 0;
		}

		void* operator new (size_t size);
		void operator delete(void* ptr);

		union 
		{
			struct { float3 bmin; uint leftFirst; };
			__m128 bmin4;
		};
		union 
		{
			struct { float3 bmax; uint triCount; };
			__m128 bmax4;
		};
		bool IsLeaf() const { return triCount > 0; }
		bool IsValid() const { return bmin.x < bmax.x && bmin.y < bmax.y && bmin.z < bmax.z; }
		float CalculateNodeCost() const
		{
			float3 e = bmax - bmin;
			return (e.x * e.y + e.x * e.z + e.y * e.z) * 2.0f * triCount;
		}
	};

	// Bounding volume hierarchy, to be used as BLAS
	class BVH
	{
	public:
		BVH() = default;
		BVH(Mesh* pMesh);

		void Build();
		void Refit();
		bool Intersect(Ray& ray, Intersection &isect, uint instanceIndex);
		Bounds AABB() const
		{
			Bounds bounds;
			if (m_BVHNodes != nullptr)
			{
				bounds.bmin = m_BVHNodes[0].bmin;
				bounds.bmax = m_BVHNodes[0].bmax;
			}

			return bounds;
		}

	private:
		void Subdivide(uint nodeIndex);
		void UpdateNodeBounds(uint nodeIndex);
		float FindBestSplitPlane(BVHNode& node, int& axis, float& splitPos);

		Mesh* m_Mesh = nullptr;

	public:
		std::unique_ptr<BVHNode[]> m_BVHNodes;
		uint m_NodesUsed = 0;
		std::unique_ptr<uint[]> m_TriIndices;

	};

	// Minimalist mesh class
	class Mesh
	{
	public:
		static constexpr int NTri = 19500;
		static constexpr int N = 11042;

		Mesh() = default;
		Mesh(const char* objFile, const char* texFile);

		void Init();

		std::unique_ptr<Triangle[]> m_Triangles;	// triangle data for intersection
		std::unique_ptr<TriangleEx[]> m_TrianglesEx;// triangle data for shading
		int m_TriCount = 0;
		std::unique_ptr<float3[]> m_Positions;
		std::unique_ptr<float3[]> m_Normals;
		std::unique_ptr<BVH> m_BVH;
		std::unique_ptr<Surface> m_Texture;
	};

	// Instance of a BVH, with transform and world bounds
	class BVHInstance
	{
	public:
		BVHInstance() = default;
		BVHInstance(BVH* blas, uint index);

		void SetTransform(const glm::mat4& transform);

		bool Intersect(Ray& ray, Intersection &isect);

		Bounds m_Bounds; // in world space

	private:
		glm::mat4 m_Transform;
		glm::mat4 m_InvTransform;

		BVH* m_BVH = nullptr;
		uint m_Index = 0;
	};

	struct TLASNode
	{
		union 
		{
			uint leftRight;
			struct { ushort left, right; };
		};
		float3 bmin, bmax;

		bool IsLeaf() const { return leftRight == 0; }
	};

	// Top-level BVH class
	class TLAS
	{
	public:
		TLAS() = default;
		TLAS(BVHInstance* bvhList, int N);
		void Build();
		bool Intersect(Ray& ray, Intersection& isect);

	private:
		int FindBestMatch(int N, int A);

	public:
		std::unique_ptr<TLASNode[]> m_TLASNodes;
		std::unique_ptr<uint[]> m_NodeIndices;
		BVHInstance* m_BLAS = nullptr;
		uint m_NodesUsed = 0, m_BLASCount = 0;

		// Fast agglomerative clustering functionality
		struct SortItem
		{
			float pos; uint blasIndex;
		};

		void BuildQuick();
		void SortAndSplit(uint first, uint last, uint level);
		void CreateParent(uint index, uint left, uint right);
		void QuickSort(SortItem items[], int first, int last);
		// Data for fast agglomerative clustering
		// KdTree
		uint m_TreeSize[16];
		std::unique_ptr<SortItem[]> m_Items;
		uint m_TreeIndex = 0;
	};

	extern bool IntersectTriangle(Ray& ray, Intersection& isect, const Triangle& tri, const uint inst_prim);

}
