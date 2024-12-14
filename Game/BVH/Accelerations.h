#pragma once

#include "CoreMinimal.h"
#include "Math/GLMath.h"

// Ref: typebvh

namespace rtrt
{
	// Basic types
	using uint8 = uint8_t;
	using uint32 = uint32_t;
	using uint16 = uint16_t;
	using uint64 = uint64_t;

	using int8 = int8_t;
	using int16 = int16_t;
	using int32 = int32_t;
	using int64 = int64_t;

	using float2 = glm::vec2;
	using float3 = glm::vec3;
	using float4 = glm::vec4;

	constexpr float g_Min = -1e10f;
	constexpr float g_Max = +1e10f;

	constexpr float kBVHFar = 1e30f;	// actual valid ieee range: 3.40282347E+38
	constexpr double kBVHFarD = 1e300;	// actual valid ieee range: 1.797693134862315E+308

	struct Triangle;
	class Mesh;

	// 32-bit surface container
	class Surface
	{
		enum { OWNER = 1 };
	public:
		Surface() = default;
		Surface(int w, int h, uint32* buffer = nullptr);
		Surface(const char* file);
		~Surface();

		void InitCharset();
		void SetChar(int c, const char* c1, const char* c2, const char* c3, const char* c4, const char* c5);
		void Print(const char* s, int x, int y, uint32 c);
		void Clear(uint32 c);
		void Line(float x0, float y0, float x1, float y1, uint32 c);
		void Plot(int x, int y, uint32 c);
		void LoadTexture(const char* file);
		void CopyTo(Surface* dst, int x, int y);
		void Box(int x0, int y0, int x1, int y1, uint32 color);
		void Bar(int x0, int y0, int x1, int y1, uint32 color);

		// Attributes
		uint32* pixels = nullptr;
		int width = 1, height = 1;
		bool ownBuffer = false;
		bool flipY = false;
	};

	struct alignas(16) Ray
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
	/**
	 * An intersection result is designed to fit in no more than four 32-bit values. This allows efficient storage of a result
	 * in GPU code.
	 * Using this data and the original triangle data, all other info for shading (such as normal, texture color, etc.) can
	 * be reconstructed.
	 */
	struct Intersection
	{
		float t;		// intersection distance along ray
		float u, v;		// barycentric coordinates of the intersection
		uint32 inst_prim; // instance index (12 bit) and primitive index (20 bit)
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
			struct { float3 bmin; uint32 leftFirst; };
			__m128 bmin4;
		};
		union 
		{
			struct { float3 bmax; uint32 triCount; };
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
		bool Intersect(Ray& ray, Intersection &isect, uint32 instanceIndex);
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
		void Subdivide(uint32 nodeIndex);
		void UpdateNodeBounds(uint32 nodeIndex);
		float FindBestSplitPlane(BVHNode& node, int& axis, float& splitPos);

		Mesh* m_Mesh = nullptr;

	public:
		std::unique_ptr<BVHNode[]> m_BVHNodes;
		uint32 m_NodesUsed = 0;
		std::unique_ptr<uint32[]> m_TriIndices;

	};

	// Minimalist mesh class
	class Mesh
	{
	public:
		static constexpr int NTri = 19500;
		static constexpr int N = 11042;

		Mesh() = default;
		Mesh(const char* objFile, const char* texFile);
		Mesh(uint32 primCount);

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
		BVHInstance(BVH* blas, uint32 index);

		void Init(BVH* blas, uint32 index, const glm::mat4& transform = glm::mat4());
		void SetTransform(const glm::mat4& transform);
		const glm::mat4& GetTransform() const { return m_Transform; }

		bool Intersect(Ray& ray, Intersection &isect);

		Bounds m_Bounds; // in world space

	private:
		glm::mat4 m_Transform;
		glm::mat4 m_InvTransform;

		BVH* m_BVH = nullptr;
		uint32 m_Index = 0;
	};

	struct alignas(32) TLASNode
	{
		TLASNode()
		{
			bmin = float3(g_Max); leftRight = 0;
			bmax = float3(g_Min); BLASIndex = 0;
		}

		float3 bmin;
		union 
		{
			uint32 leftRight;
			struct { uint16 left, right; };
		};
		float3 bmax; uint32 BLASIndex;

		bool IsLeaf() const { return leftRight == 0; }
	};

	// Top-level BVH class
	class KdTree;
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
		std::unique_ptr<uint32[]> m_NodeIndices;
		BVHInstance* m_BLAS = nullptr;
		uint32 m_NodesUsed = 0, m_BLASCount = 0;

		void BuildQuick();
	};

	extern bool IntersectTriangle(Ray& ray, Intersection& isect, const Triangle& tri, const uint32 inst_prim);

#pragma region KdTree
	// Custom Kd-Tree, used for quick TLAS construction
	class KdTree
	{
	public:
		struct KdNode
		{
			KdNode();
			KdNode& operator=(const KdNode& other);

			union
			{
				struct { uint32 left, right, parax; float splitPos;  }; // for an interior node
				struct { uint32 first, count, dummy0, dummy1; };	// for a leaf node, 16 bytes
			};

			union 
			{
				struct { float3 bmin; float w0; };
				__m128 bmin4;
			};
			union 
			{
				struct { float3 bmax; float w1; };
				__m128 bmax4;
			};
			union 
			{
				struct { float3 minSize; float w2; };
				__m128 minSize4;
			};

			void* operator new(size_t size);
			void operator delete(void* ptr);

			bool IsLeaf() const { return (parax & 7) > 3; }
		};

		static uint32* s_Leaf;

		KdTree() = default;
		KdTree(TLASNode* tlasNodes, uint32 N, uint32 O = 0);

		void Rebuild();
		void RecursiveRefit(uint32 index);
		void Subdivide(KdNode& node, uint32 depth = 0);
		// Return left child node count
		uint32 Partition(KdNode& node, uint32 axis, float splitPos);
		void Add(uint32 index);
		void RemoveLeaf(uint32 index);
		int FindNearest(uint32 A, uint32& startB, float& startSA);

		std::unique_ptr<KdNode[]> m_Nodes;
		TLASNode* m_TLAS = nullptr;
		std::unique_ptr<uint32[]> m_TLASIndices;
		uint32 m_NodeCount = 0, m_TLASCount = 0, m_BLASCount = 0, m_Offset = 0, m_Freed[2] = { 0, 0 };

	};
#pragma  endregion

	// Ref: tinybvh.h
	namespace tiny
	{
		// Strided slice of float4
		struct Float4Slice
		{
			const uint8* data {nullptr};
			uint32 count{0}, stride{0};
			
			Float4Slice() = default;
			/**
			 * @param data pointer to the first element
			 * @param count number of 'float4' in the slice, not 'bytes'
			 * @param stride byte stride between each 'float4' element
			 */
			Float4Slice(const float4* data, uint32 count, uint32 stride = sizeof(float4))
				: data( reinterpret_cast<const uint8*>(data) ), count(count), stride(stride) {}

			operator bool() const { return data != nullptr; }
			const float4& operator[](uint32 index) const
			{
				return *reinterpret_cast<const float4*>( data + index * stride );	
			}
		};
		
		struct BVHBase
		{
		public:
			/**
			 * A fragment stores the bounds of an input primitive. The name 'fragment' is from 'Parallel Spatial Splits in
			 * Bounding Volume Hierarchies', 2016, Fuetterling et al., and refers to the potential splitting of these boxes
			 * for SBVH construction.
			 */
			struct Fragment
			{
				float3 bmin;	// AABB min x,y,z
				uint32 primIndex;	// index of the original primitive
				float3 bmax;	// AABB max x,y,z
				uint32 clipped = 0;	// fragment is the result of clipping if > 0
				bool isValidBox() const { return bmin.x < kBVHFar; }
			};

			// BVH flags
			bool bRebuildable = true;	// rebuilds are safe only if a tree has not been converted
			bool bRefittable = true;	// refits are safe only if the tree has no spatial splits
			bool bFragMinFlipped = false;	// AVX builders flip aabb min
			bool bMayHaveHolds = false;	// threads builds and MergeLeafs produce BVHs with unused nodes
			bool bBVHOverAABB = false;;	// a BVH or AABBs is useful for e.g. TLAS traversal

			// Keep track of allocated buffer size to avoid repeated allocation during layout conversion
			uint32 allocatedNodes = 0;	// number of allocated for the BVh
			uint32 usedNodes = 0;		// number of nodes used for the BVH
			uint32 triCount = 0;		// number of primitives in the BVH
			uint32 idxCount = 0;		// number of primitive indices; can exceed triCount for SBVH

			// copy flags from one BVH to another
			void CopyBasePropertiesFrom(const BVHBase &other);

		protected:
			void IntersectTri(Ray &r, const uint32 triIdx) const;
			static float Intersect(const Ray &ray, const float3 &bmin, const float3 &bmax);
			static void PrecomputeTri(uint32 triIdx);
			static float SA(const float3 &bmin, const float3 &bmax);

			static void* AlignedAlloc(size_t size);
			static void AlignedFree(void* ptr);
		};

		struct BLASInstance;
		struct BVH_Verbose;
	
		struct BVH : public BVHBase
		{
			enum EBuildFlag : uint32
			{
				None = 0,	// default building behavior (binned, SAH-driven)
				FullSplit,	// split as far as possible, even when SAH doesn't agree
			};
			
			struct BVHNode
			{
				// 'Traditional' 32-byte BVH node layout, as proposed by Ingo Wald.
				// When aligned to a cache line boundary, two of these fit together.
				float3 bmin; uint32 leftFirst;	// 16 bytes
				float3 bmax; uint32 triCount;	// 16 bytes, total 32 bytes
				// Empty BVH leaves do not exist
				bool IsLeaf() const { return triCount > 0; }
				float Intersect(const Ray &ray) const { return IntersectAABB(ray, bmin, bmax); }
				float SurfaceArea() const { return SA(bmin, bmax); }
			};
			
			BVH() = default;
			
			float SAHCost(uint32 nodeIdx = 0) const;
			uint32 NodeCount() const;
			uint32 PrimCount() const;
			void Compact();

			void BuildDefault(const float4 *vertices, uint32 primCount)
			{
				BuildDefault({ vertices, primCount * 3 });
			}
			void BuildDefault(const Float4Slice &vertices);
			void BuildQuick(const float4 *vertices, uint32 primCount);
			void BuildQuick(const Float4Slice &vertices);
			void Build(const float4 *vertices, uint32 primCount);
			void Build(const Float4Slice &vertices);
			void BuildHQ(const float4 *vertices, uint32 primCount);
			void BuildHQ(const Float4Slice &vertices);
			
			void Intersect(Ray &ray) const;
			void IntersectTLAS(Ray &ray) const;
			bool IsOccluded(const Ray &ray) const;

			// Basic BVH data
			Float4Slice vertices{};		// pointer to input primitive array: 3x16 byte per tri
			uint32 *indices{nullptr};	// primitive index array
			BVHNode *bvhNodes{nullptr};	// BVH node pool, 32-byte format. Root is always in node 0.
			Fragment *fragments{nullptr};	// input primitive bounding boxes
			EBuildFlag flag = EBuildFlag::None;	// hint to the builder
			
		};

		struct BVH8_CWBVH : public BVHBase
		{
		public:
			
		};
	}
}
