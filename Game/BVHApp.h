#pragma once
#include "IGameApp.h"
#include "Camera.h"
#include "CameraController.h"
#include "ColorBuffer.h"
#include "Math/GLMath.h"
#include "Scenes/DebugPass.h"

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
		glm::vec3 ro, rd;
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
			if (!Valid())
				return glm::vec3(-1.0f);
			return (bmax - bmin);
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
		bool isLeaf() const { return triCount > 0; }
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
		static constexpr int s_Num = 32;

		BVHApp(HINSTANCE hInstance, const wchar_t* title = L"BVH App", UINT width = SCR_WIDTH, UINT height = SCR_HEIGHT);

		void Update(float deltaTime) override;
		void Render() override;

	private:
		void InitCustom() override;
		void CleanCustom() override;

		void BuildBVH();
		void UpdateNodeBounds(uint nodeIdx);
		void Subdivide(uint nodeIdx);
		bool IntersectBVH(Ray& ray, uint nodeIdx);

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
		BVHNode m_BVHNode[2 * s_Num];
		uint m_RootNodeIdx = 0, m_NodesUsed = 1;
	};
}
