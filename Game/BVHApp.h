#pragma once
#include "IGameApp.h"
#include "Camera.h"
#include "CameraController.h"
#include "ColorBuffer.h"
#include "Math/GLMath.h"
#include "Scenes/DebugPass.h"

// How to build BVH
// twitter: @j_bikker

#ifndef USE_MODELS
#define USE_MODELS 0
#endif

#ifndef AS_FLAG
#define AS_FLAG 2
#endif

#include "Accelerations.h"

namespace MyDirectX
{
	// Aligned memory allocations
#ifdef _MSC_VER
#define ALIGN(x) __declspec(align(x))
#define MALLOC64(x) ((x) == 0 ? 0 : _aligned_malloc((x), 64))
#define FREE64(x) _aligned_free(x)

#else
	// TODO
#endif

	// Basic types
	using uchar = unsigned char;
	using uint = unsigned int;
	using ushort = unsigned short;

	struct Triangle;

	struct Ray
	{
		static constexpr float TMAX = 1e5f;
		static constexpr float TMIN = 1e-3f;

		Ray(glm::vec3 o, glm::vec3 d) : ro(o), rd(d), rcpD(1.0f/d) {  }

		glm::vec3 ro, rd, rcpD;
		float tmin = TMIN, tmax = TMAX;
	};

	struct Bounds
	{
		glm::vec3 bmin = glm::vec3(1e5f);
		glm::vec3 bmax = glm::vec3(-1e5f);

		Bounds() = default;

		Bounds(glm::vec3 c0, glm::vec3 c1)
		{
			if (c0.x > c1.x) std::swap(c0.x, c1.x);
			if (c0.y > c1.y) std::swap(c0.y, c1.y);
			if (c0.z > c1.z) std::swap(c0.z, c1.z);

			bmin = c0; bmax = c1;
		}

		float Area() const
		{
			const glm::vec3 extent = glm::max(bmax - bmin, glm::vec3(0.0f));
			return (extent.x * extent.y + extent.y * extent.z + extent.x * extent.z) * 2.0f;
		}

		void Union(const Bounds& other)
		{
			bmin = glm::min(bmin, other.bmin);
			bmax = glm::max(bmax, other.bmax);
		}

		void Union(const glm::vec3 &point)
		{
			bmin = glm::min(bmin, point);
			bmax = glm::max(bmax, point);
		}

		void Union(const Triangle& tri);

		bool Valid() const { return (bmin.x < bmax.x) && (bmin.y < bmax.y) && (bmin.z < bmax.z); }

		glm::vec3 Center() const { return (bmin + bmax) * 0.5f; }
		glm::vec3 Extent() const
		{
			return glm::max(bmax - bmin, glm::vec3(0.0f));
		}

		void Reset()
		{
			bmin = glm::vec3(1e5f);
			bmax = glm::vec3(-1e5f);
		}

		static Bounds Union(const Bounds& a, const Bounds& b)
		{
			Bounds bounds{ a };
			bounds.Union(b);
			return bounds;
		}
	};

	struct Triangle
	{
		glm::vec3 v0, v1, v2;
		glm::vec3 c; // centroid

		Bounds BoundingBox() const
		{
			Bounds b;
			b.Union(v0); b.Union(v1); b.Union(v2);
			return b;
		}
	};

	struct alignas(32) BVHNode
	{
		uint leftFirst, triCount;
		Bounds bounds;

		bool isValid() const { return bounds.Valid(); }
		bool isLeaf() const { return triCount > 0; }
		float CalculateNodeCost() const { return bounds.Area() * triCount; }
	};

	class BVH
	{
	public:
		BVH() = default;
		BVH(const char* fileName, int N);
		~BVH();

		void Build();
		void Refit();
		bool Intersect(Ray& ray);
		const Bounds& AABB() const { return m_BVHNode[m_RootNodeIdx].bounds; }

	private:
		void Subdivide(uint nodeIdx);
		void UpdateNodeBounds(uint nodeIdx);
		float FindBestSplitPlane(BVHNode& node, int& axis, float& splitPos);

		BVHNode* m_BVHNode = nullptr;
		Triangle* m_Tris = nullptr;
		uint* m_TriIndices = nullptr;
		uint m_RootNodeIdx = 0;
		uint m_NodesUsed = 0, m_TriCount = 0;
	};

	// Instance of a BVH, with transform and world bounds
	class BVHInstance
	{
	public:
		BVHInstance() = default;
		BVHInstance(BVH* blas) : m_BVH(blas) {  }

		void SetInstance(BVH* blas) { m_BVH = blas; }
		void SetTransform(const glm::mat4& transform);
		bool Intersect(Ray& ray);

		Bounds m_Bounds; // in world space

	private:
		BVH* m_BVH = nullptr;
		glm::mat4 m_InvTransform;
	};

	/***
	 * TLAS
	 * It starts with the notion that a pair of BVHs can be combined into a single BVH, by simply adding one new node,
	 * that has the pair of BVHs as child nodes. We now have a new, valid BVH that we can traverse as if it were a
	 * regular BVH. The nodes we use to combine a set of BVHs into a single BVH are referred to as the top level
	 * acceleration structure, or TLAS.
	 * In the TLAS, we will never have more than one BLAS in a leaf node, so indirection is not needed.
	 */
	struct TLASNode
	{
		glm::vec3 bmin;
		uint leftRight; // 2x16 bits
		glm::vec3 bmax;

		uint BLAS; // stores the index of a BLAS, otherwise it is unused
		Bounds bounds;

		bool isLeaf() const { return leftRight == 0; }
	};

	class TLAS
	{
	public:
		TLAS() = default;
		TLAS(BVHInstance* bvhList, int N);
		~TLAS();

		void Build();
		bool Intersect(Ray & ray);

	private:
		int FindBestMatch(int* list, int N, int A);

		TLASNode* m_TLASNodes = nullptr;
		BVHInstance* m_BLASList = nullptr;
		uint m_NodesUsed = 0, m_BLASCount = 0;
	};

	extern char g_Font[51][5][6];
	extern bool g_bFontInited;
	extern int g_Translation[256];
	// Ref: BVHDemo
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

	class BVHApp : public IGameApp
	{
	public:
		using Super = IGameApp;
#if USE_MODELS
		static constexpr int s_Num = 12582; // Hardcoded for the Unity vehicle mesh
#else
		static constexpr int s_Num = 12; // 64
		static constexpr int s_Instances = 3;
#endif

		BVHApp(HINSTANCE hInstance, const wchar_t* title = L"BVH App", UINT width = SCR_WIDTH, UINT height = SCR_HEIGHT);

		void Update(float deltaTime) override;
		void Render() override;

	private:
		void InitCustom() override;
		void CleanCustom() override;

		void BuildBVH();
		void RefitBVH();
		void UpdateNodeBounds(uint nodeIdx);
		void Subdivide(uint nodeIdx);
		bool IntersectBVH(Ray& ray, uint nodeIdx);
		float FindBestSplitPlane(BVHNode& node, int& axis, float& splitPos);

#if AS_FLAG >= 2
		rtrt::float3 Trace(rtrt::Ray& ray, rtrt::Intersection &isect, int rayDepth = 0);
#endif
		rtrt::float3 SampleSky(const rtrt::float3& direction);

		/// Pipeline
		DebugPass m_DebugPass;

		/// Resources
		ColorBuffer m_TraceResult;
		
		std::unique_ptr<Surface> m_Surface;

		/// Scenes
		// Camera & camera controller
		Math::Camera m_Camera;
		std::unique_ptr<CameraController> m_CameraController;
		Math::Matrix4 m_ViewProjMatrix;
		Math::Vector3 m_LowerLeftCorner;
		Math::Vector3 m_imgHorizontal;
		Math::Vector3 m_imgVertical;

		// GameObjects
		Triangle m_TestTriangle;
		Triangle m_Tris[s_Num];
		uint m_TriIndices[s_Num];
		BVHNode *m_BVHNode = nullptr;
		// BVHNode m_BVHNode[s_Num * 2]; // s_Num = 10000+, Stack overflow
		uint m_RootNodeIdx = 0, m_NodesUsed = 1;

#if AS_FLAG == 2
		std::shared_ptr<rtrt::Mesh> m_Mesh;
		std::shared_ptr<rtrt::BVHInstance[]> m_BVHInstance;
#elif AS_FLAG == 1
		std::unique_ptr<BVH> m_BVH;
		std::unique_ptr<BVHInstance[]> m_BVHInstances;
		std::unique_ptr<TLAS> m_TLAS;
		glm::vec3 m_Translations[4];
#endif

		std::unique_ptr<rtrt::float3[]> m_Accumulator;
		float* m_SkyPixels = nullptr;
		int m_SkyWidth = 1, m_SkyHeight = 1, m_SkyBpp = 3;
	};
}
