#include "BVHApp.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "Math/Random.h"
#include "Core/Utility.h"

#include <ppl.h>
#define PARALLEL_IMPL 1

// #define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"

using namespace MyDirectX;
using namespace glm;

glm::vec3 Cast(const Math::Vector3 &v)
{
	return glm::vec3{ v.GetX(), v.GetY(), v.GetZ() };
}

glm::vec4 Cast(const Math::Vector4 &v)
{
	return glm::vec4{ v.GetX(), v.GetY(), v.GetZ(), v.GetW() };
}

uint Color(const glm::vec3 &c)
{
	uint ic = 0;
	ic |= (uint(c.x * 255.9f) & 0xFF);
	ic |= (uint(c.y * 255.9f) & 0xFF) << 8;
	ic |= (uint(c.z * 255.9f) & 0xFF) << 16;

	return ic;
}

uint Color(const glm::vec4 &c)
{
	uint ic = 0;
	ic |= (uint(c.x * 255.9f) & 0xFF);
	ic |= (uint(c.y * 255.9f) & 0xFF) << 8;
	ic |= (uint(c.z * 255.9f) & 0xFF) << 16;
	ic |= (uint(c.w * 255.9f) & 0xFF) << 24;

	return ic;
}

bool IntersectTriangle(Ray& ray, const Triangle& tri)
{
	const vec3 edge1 = tri.v1 - tri.v0;
	const vec3 edge2 = tri.v2 - tri.v0;
	const vec3 h = glm::cross(ray.rd, edge2);
	const float a = glm::dot(edge1, h);
	if (a > -0.0001f && a < 0.0001f) // ray parallel to the triangle
		return false;

	const float f = 1.f / a;
	const vec3 s = ray.ro - tri.v0;
	const float u = f * glm::dot(s, h);
	if (u < 0 || u > 1)
		return false;

	const vec3 q = glm::cross(s, edge1);
	const float v = f * glm::dot(ray.rd, q);
	if (v < 0 || u + v > 1)
		return false;

	const float t = f * glm::dot(edge2, q);
	if (t > 0.0001f && t < ray.tmax)
	{
		ray.tmax = t;
		return true;
	}

	return false;
}

// dot( o + t * dir - C, o + t * dir - C ) = R^2 
bool IntersectSphere(Ray &ray, const vec3& center, float radius)
{
	vec3 oc = ray.ro - center;
	float a = glm::dot(ray.rd, ray.rd);
	float b = 2.0f * glm::dot(oc, ray.rd);
	float c = glm::dot(oc, oc) - radius * radius;
	float discriminant = b * b - 4.0f * a * c;

	bool bIntersect = false;
	if (discriminant >= 0)
	{
		float t = (-b - std::sqrt(discriminant)) / (2.0f * a);
		ray.tmax = std::min(ray.tmax, t);
		bIntersect = ray.tmax == t;
	}

	return bIntersect;
}

// Divisions are expensive for processors
float IntersectAABB(const Ray &ray, const Bounds &bounds)
{
#if 0
	float tx1 = (bounds.bmin.x - ray.ro.x) / ray.rd.x, tx2 = (bounds.bmax.x - ray.ro.x) / ray.rd.x;
	float tmin = std::min(tx1, tx2), tmax = std::max(tx1, tx2);

	float ty1 = (bounds.bmin.y - ray.ro.y) / ray.rd.y, ty2 = (bounds.bmax.y - ray.ro.y) / ray.rd.y;
	tmin = std::max(tmin, std::min(ty1, ty2));
	tmax = std::min(tmax, std::max(ty1, ty2));

	float tz1 = (bounds.bmin.z - ray.ro.z) / ray.rd.z, tz2 = (bounds.bmax.z - ray.ro.z) / ray.rd.z;
	tmin = std::max(tmin, std::min(tz1, tz2));
	tmax = std::min(tmax, std::max(tz1, tz2));
#else
	// But this optimization has a limited effect on my machine
	float tx1 = (bounds.bmin.x - ray.ro.x) * ray.rcpD.x, tx2 = (bounds.bmax.x - ray.ro.x) * ray.rcpD.x;
	float tmin = std::min(tx1, tx2), tmax = std::max(tx1, tx2);

	float ty1 = (bounds.bmin.y - ray.ro.y) * ray.rcpD.y, ty2 = (bounds.bmax.y - ray.ro.y) * ray.rcpD.y;
	tmin = std::max(tmin, std::min(ty1, ty2));
	tmax = std::min(tmax, std::max(ty1, ty2));

	float tz1 = (bounds.bmin.z - ray.ro.z) * ray.rcpD.z, tz2 = (bounds.bmax.z - ray.ro.z) * ray.rcpD.z;
	tmin = std::max(tmin, std::min(tz1, tz2));
	tmax = std::min(tmax, std::max(tz1, tz2));
#endif

	bool bIntersect = tmin <= tmax && tmin < ray.tmax && tmax > 0;
	return bIntersect ? tmin : Ray::TMAX;
}


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

struct Bin
{
	Bounds bounds;
	int triCount = 0;
};
static constexpr int s_Bins = 8;


/// BVH
BVH::BVH(const char* fileName, int N)
{
	FILE* file;
	const auto& error = fopen_s(&file, fileName, "r");
	if (error != 0)
	{
		Utility::Printf("Load model at %s failed!", fileName);
		while (1) exit(-1);
	}

	m_TriCount = N;
	m_Tris = new Triangle[N];
	m_TriIndices = new uint[N];
	for (int i = 0; i < N; ++i)
	{
		auto& tri = m_Tris[i];
		fscanf_s(file, "%f %f %f %f %f %f %f %f %f\n",
			&tri.v0.x, &tri.v0.y, &tri.v0.z,
			&tri.v1.x, &tri.v1.y, &tri.v1.z,
			&tri.v2.x, &tri.v2.y, &tri.v2.z);
	}
	fclose(file);

	m_BVHNode = (BVHNode*)_aligned_malloc(sizeof(BVHNode) * N * 2, 64);
	Build();
}

BVH::~BVH()
{
	delete[] m_Tris;
	delete[] m_TriIndices;

	_aligned_free(m_BVHNode);
}

void BVH::Build()
{
	// Reset node pool
	m_NodesUsed = 2;
	// Populate triangle index array
	for (uint i = 0; i < m_TriCount; ++i)
		m_TriIndices[i] = i;
	// Calculate triangle centroids for partitioning
	for (uint i = 0; i < m_TriCount; ++i)
	{
		auto& tri = m_Tris[i];
		tri.c = (tri.v0 + tri.v1 + tri.v2) / 3.0f;
	}
	// Assign all triangles to root node
	BVHNode& root = m_BVHNode[m_RootNodeIdx];
	root.leftFirst = 0, root.triCount = m_TriCount;
	UpdateNodeBounds(m_RootNodeIdx);
	// Subdivide recursively
	Subdivide(m_RootNodeIdx);
}

void BVH::Refit()
{
	for (int i = m_NodesUsed - 1; i >= 0; --i)
	{
		auto& node = m_BVHNode[i];
		if (!node.isValid())
			continue;

		if (node.isLeaf())
		{
			// Leaf node - adjust bounds to contained triangles
			UpdateNodeBounds(i);
		}
		else
		{
			// Interior node - adjust bounds to child node bounds
			auto& node0 = m_BVHNode[node.leftFirst];
			auto& node1 = m_BVHNode[node.leftFirst + 1];
			auto& bounds = node.bounds;
			bounds.Reset();
			bounds.Union(node0.bounds);
			bounds.Union(node1.bounds);
		}
	}
}

bool BVH::Intersect(Ray& ray)
{
	BVHNode* node = &m_BVHNode[m_RootNodeIdx], * stack[128];
	uint stackCount = 0;
	bool bIntersect = false;
	while (true)
	{
		if (node->isLeaf())
		{
			for (uint i = node->leftFirst, imax = node->leftFirst + node->triCount; i < imax; ++i)
			{
				bIntersect |= IntersectTriangle(ray, m_Tris[m_TriIndices[i]]);
			}
			if (stackCount == 0)
				break;
			else
				node = stack[--stackCount];
		}
		else
		{
			BVHNode* pChild0 = &m_BVHNode[node->leftFirst];
			BVHNode* pChild1 = &m_BVHNode[node->leftFirst + 1];
			float d0 = IntersectAABB(ray, pChild0->bounds);
			float d1 = IntersectAABB(ray, pChild1->bounds);
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

void BVH::Subdivide(uint nodeIdx)
{
	BVHNode& node = m_BVHNode[nodeIdx];

	// Determine split axis using SAH
	int axis = 0;
	float splitPos;
	float splitCost = FindBestSplitPlane(node, axis, splitPos);
	float noSplitCost = node.CalculateNodeCost();
	// Checks if the best split cost is actually an improvement over not splitting.
	if (splitCost >= noSplitCost)
		return;

	// in-place partition
	int i = node.leftFirst;
	int j = i + node.triCount - 1;
	while (i <= j)
	{
		if (m_Tris[m_TriIndices[i]].c[axis] < splitPos)
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

	// Create child nodes
	int leftChildIdx = m_NodesUsed++;
	int rightChildIdx = m_NodesUsed++;

	auto& leftChildNode = m_BVHNode[leftChildIdx];
	leftChildNode.leftFirst = node.leftFirst;
	leftChildNode.triCount = leftCount;
	UpdateNodeBounds(leftChildIdx);

	auto& rightChildNode = m_BVHNode[rightChildIdx];
	rightChildNode.leftFirst = i;
	rightChildNode.triCount = node.triCount - leftCount;
	UpdateNodeBounds(rightChildIdx);

	// Set the primitive count of the parent node to 0.
	node.leftFirst = leftChildIdx;
	node.triCount = 0;

	Subdivide(leftChildIdx);
	Subdivide(rightChildIdx);
}

void BVH::UpdateNodeBounds(uint nodeIdx)
{
	BVHNode& node = m_BVHNode[nodeIdx];
	node.bounds.Reset(); // malloc, not initialized
	for (int i = node.leftFirst, imax = node.leftFirst + node.triCount; i < imax; ++i)
	{
		uint leafTriIdx = m_TriIndices[i];
		Triangle& leafTri = m_Tris[leafTriIdx];
		node.bounds.Union(leafTri);
	}
}

float BVH::FindBestSplitPlane(BVHNode& node, int& axis, float& splitPos)
{
	float bestCost = 1e10f;
	for (int a = 0; a < 3; ++a)
	{
		// Centroid bounds
		// The first split plane candidate is on the first centroid, the last one on the last centroid.
		float cmin = 1e5f, cmax = -1e5f;
		for (int i = node.leftFirst, imax = node.leftFirst + node.triCount; i < imax; ++i)
		{
			Triangle& tri = m_Tris[m_TriIndices[i]];
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
			Triangle& tri = m_Tris[m_TriIndices[i]];
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


/// BVHInstance
void BVHInstance::SetTransform(const glm::mat4& transform)
{
	m_InvTransform = glm::inverse(transform);
	// Calculate world-space bounds using the new matrix
	vec3 bmin = m_BVH->AABB().bmin, bmax = m_BVH->AABB().bmax;
	m_Bounds.Reset();
	for (int i = 0; i < 8; ++i)
	{
		m_Bounds.Union(transform * vec4(
			i & 1 ? bmax.x : bmin.x,
			i & 2 ? bmax.y : bmin.y,
			i & 4 ? bmax.z : bmin.z, 1.0f));
	}
}

bool BVHInstance::Intersect(Ray& ray)
{
	// Backup ray and transform original
	Ray backupRay = ray;
	ray.ro = m_InvTransform * vec4(ray.ro, 1.0f);
	ray.rd = m_InvTransform * vec4(ray.rd, 0.0f);
	ray.rcpD = 1.0f / ray.rd;

	// Trace ray through the BVH
	bool bIntersect = m_BVH->Intersect(ray);

	// Restore ray origin and direction
	backupRay.tmax = ray.tmax;
	ray = backupRay;

	return bIntersect;
}


/// TLAS
TLAS::TLAS(BVHInstance* bvhList, int N)
{
	// Copy a pointer to the array of bottom level acceleration structures
	m_BLASList = bvhList;
	m_BLASCount = N;
	// Alloclate TLAS nodes
	m_TLASNodes = (TLASNode*)_aligned_malloc(sizeof(TLASNode) * 2 * N, 64);
	m_NodesUsed = 2;
}

TLAS::~TLAS()
{
	if (m_TLASNodes != nullptr)
		_aligned_free(m_TLASNodes);
}

void TLAS::Build()
{
	// Assign a TLAS leaf node to each BLAS
	// Rather than directly storing the TLASNode instances, we store indices of elements of the m_TLASNodes array,
	// After each generated pair, it will decrement by 1 until only 1 node is left, this will be our root node.
	int nodeIndices[256], nNode = m_BLASCount;
	m_NodesUsed = 1;
	for (uint i = 0; i < m_BLASCount; ++i)
	{
		nodeIndices[i] = m_NodesUsed;
		auto& node = m_TLASNodes[m_NodesUsed];
		node.bounds = m_BLASList[i].m_Bounds;
		node.BLAS = i;
		node.leftRight = 0; // leaf node

		++m_NodesUsed;
	}
	// Use agglomerative clustering to build the TLAS
	int A = 0, B = FindBestMatch(nodeIndices, nNode, A);
	while (nNode > 1)
	{
		int C = FindBestMatch(nodeIndices, nNode, B);
		if (A == C)
		{
			int nodeIdxA = nodeIndices[A], nodeIdxB = nodeIndices[B];
			const auto& nodeA = m_TLASNodes[nodeIdxA];
			const auto& nodeB = m_TLASNodes[nodeIdxB];

			auto& newNode = m_TLASNodes[m_NodesUsed];
			newNode.leftRight = nodeIdxA | (nodeIdxB << 16);
			newNode.bounds.Reset();
			newNode.bounds.Union(nodeA.bounds);
			newNode.bounds.Union(nodeB.bounds);

			nodeIndices[A] = m_NodesUsed;
			nodeIndices[B] = nodeIndices[nNode-1]; // last node
			++m_NodesUsed;
			--nNode;

			B = FindBestMatch(nodeIndices, nNode, A);
		}
		else
		{
			A = B; B = C;
		}
	}
	m_TLASNodes[0] = m_TLASNodes[nodeIndices[A]];
}

int TLAS::FindBestMatch(int* indices, int N, int A)
{
	// Find BLAS B that, when joined with A, forms the smallest AABB
	// It simply checks all remaining nodes in the list. For each node, the combined AABB is determined.
	// If the area of this AABB (skipping the factor 2) is smaller than what we found before, we take note that.
	// After checking all nodes we return our best find.
	float smallest = 1e10f;
	int bestB = -1;
	for (int B = 0; B < N; ++B)
	{
		if (B != A)
		{
			const auto &nodeA = m_TLASNodes[indices[A]];
			const auto &nodeB = m_TLASNodes[indices[B]];
			vec3 bmax = glm::max(nodeA.bounds.bmax, nodeB.bounds.bmax);
			vec3 bmin = glm::min(nodeA.bounds.bmin, nodeB.bounds.bmin);
			vec3 e = bmax - bmin;
			float surfaceArea = e.x * e.y + e.x * e.z + e.y * e.z;
			if (surfaceArea < smallest)
			{
				smallest = surfaceArea;
				bestB = B;
			}
		}
	}

	return bestB;
}

bool TLAS::Intersect(Ray& ray)
{
	TLASNode* node = &m_TLASNodes[0], *stack[64];
	uint stackCount = 0;
	bool bIntersect = false;
	while (true)
	{
		if (node->isLeaf())
		{
			bIntersect |= m_BLASList[node->BLAS].Intersect(ray);
			if (stackCount == 0)
				break;
			else
				node = stack[--stackCount];
		}
		else
		{
			TLASNode* pChild0 = &m_TLASNodes[ (node->leftRight & 0xFFFF) ];
			TLASNode* pChild1 = &m_TLASNodes[ (node->leftRight >> 16) ];
			float d0 = IntersectAABB(ray, pChild0->bounds);
			float d1 = IntersectAABB(ray, pChild1->bounds);
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


/// Surface
char MyDirectX::g_Font[51][5][6];
bool MyDirectX::g_bFontInited = false;
int  MyDirectX::g_Translation[256];

Surface::Surface(int w, int h, uint* buffer) : pixels(buffer), width(w), height(h)
{
	if (buffer == nullptr)
	{
		pixels = (uint*)MALLOC64(w * h * sizeof(uint));
		ownBuffer = true; // Needs to be deleted in destructor
	}
}

Surface::Surface(const char* file) : pixels(nullptr), width(1), height(1)
{
#if 1
	FILE* f;
	fopen_s(&f, file, "rb");
#else
	// 'strcpy': This function or variable may be unsafe.Consider using strcpy_s instead.
	// To disable deprecation, use _CRT_SECURE_NO_WARNINGS.
	f = fopen(file, "rb");
#endif
	if (!f)
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
		pixels = (uint*)MALLOC64(width * height * sizeof(uint));
		ownBuffer = true;
		const int s = width * height;
		if (n == 1) // Greyscale
		{
			for (int i = 0; i < s; ++i)
			{
				const uchar p = data[i];
				pixels[i] = p + (p << 8) + (p << 16);
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
		FREE64(pixels); // Free only if we allocated the buffer ourselves
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
	int index = flipY ? (x + (height-1 - y) * width) : (x + y * width);
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
	SetChar(0 , ":ooo:", "o:::o", "ooooo", "o:::o", "o:::o");
	SetChar(1 , "oooo:", "o:::o", "oooo:", "o:::o", "oooo:");
	SetChar(2 , ":oooo", "o::::", "o::::", "o::::", ":oooo");
	SetChar(3 , "oooo:", "o:::o", "o:::o", "o:::o", "oooo:");
	SetChar(4 , "ooooo", "o::::", "oooo:", "o::::", "ooooo");
	SetChar(5 , "ooooo", "o::::", "ooo::", "o::::", "o::::");
	SetChar(6 , ":oooo", "o::::", "o:ooo", "o:::o", ":ooo:");
	SetChar(7 , "o:::o", "o:::o", "ooooo", "o:::o", "o:::o");
	SetChar(8 , "::o::", "::o::", "::o::", "::o::", "::o::");
	SetChar(9 , ":::o:", ":::o:", ":::o:", ":::o:", "ooo::");
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


/// BVHApp
BVHApp::BVHApp(HINSTANCE hInstance, const wchar_t* title, UINT width, UINT height)
	: IGameApp(hInstance, title, width, height)
{
	m_Surface = std::make_unique<Surface>(width, height); // / 2
	m_Surface->flipY = true;
}

void BVHApp::InitCustom()
{
	// Pipeline
	{
		m_DebugPass.Init();
	}
	// Resources
	{
		m_TraceResult.Create(Graphics::s_Device, L"TracedResult", m_Surface->width, m_Surface->height, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
	}

	// Camera
	{
		const Math::Vector3 camPos{ 0.0f, 0.0f, -10.0f }, forward{ 0.0f, 0.0f, 1.0f }, up{ Math::kYUnitVector };
		m_Camera.SetEyeAtUp(camPos, camPos + forward, Math::Vector3(Math::kYUnitVector));
		m_Camera.SetZRange(0.3f, 300.0f);
		m_CameraController.reset((new CameraController(m_Camera, up, *m_Input)));
		m_CameraController->SetMoveSpeed(200.0f);
		m_CameraController->SetStrafeSpeed(200.0f);
	}
	// GameObjects
	{
#if USE_MODELS
		const char* fileName = "Models/BVHAssets/unity.tri"; // bigben
		FILE* file;
		const auto &error = fopen_s(&file, fileName, "r");
		if (error != 0)
		{
			Utility::Printf("Load model at %s failed!", fileName);
			while (1) exit(-1);
		}
		for (int i = 0; i < s_Num; ++i)
		{
			auto& tri = m_Tris[i];
			fscanf_s(file, "%f %f %f %f %f %f %f %f %f\n",
				&tri.v0.x, &tri.v0.y, &tri.v0.z,
				&tri.v1.x, &tri.v1.y, &tri.v1.z,
				&tri.v2.x, &tri.v2.y, &tri.v2.z);
		}
		fclose(file);
#else
		Math::RandomNumberGenerator rng;
		rng.SetSeed(GetCurrentTime());
#if 0
		for (int i = 0; i < s_Num; ++i)
		{
			vec3 v0{ rng.NextFloat(), rng.NextFloat(), rng.NextFloat() };
			vec3 v1{ rng.NextFloat(), rng.NextFloat(), rng.NextFloat() };
			vec3 v2{ rng.NextFloat(), rng.NextFloat(), rng.NextFloat() };

			auto& tri = m_Tris[i];
			tri.v0 = v0 * 9.0f - vec3(5.0f);
			tri.v1 = tri.v0 + v1;
			tri.v2 = tri.v0 + v2;
		}

#else
		// Test cube
		const int w = 1, h = 1, d = 1;
		// Front
		m_Tris[0].v0 = vec3(-w, -h, -d);
		m_Tris[0].v1 = vec3(-w, +h, -d);
		m_Tris[0].v2 = vec3(+w, +h, -d);

		m_Tris[1].v0 = vec3(-w, -h, -d);
		m_Tris[1].v1 = vec3(+w, +h, -d);
		m_Tris[1].v2 = vec3(+w, -h, -d);

		// Back
		m_Tris[2].v0 = vec3(-w, -h, +d);
		m_Tris[2].v1 = vec3(+w, -h, +d);
		m_Tris[2].v2 = vec3(+w, +h, +d);

		m_Tris[3].v0 = vec3(-w, -h, +d);
		m_Tris[3].v1 = vec3(+w, +h, +d);
		m_Tris[3].v2 = vec3(-w, +h, +d);

		// Top
		m_Tris[4].v0 = vec3(-w, +h, -d);
		m_Tris[4].v1 = vec3(-w, +h, +d);
		m_Tris[4].v2 = vec3(+w, +h, +d);

		m_Tris[5].v0 = vec3(-w, +h, -d);
		m_Tris[5].v1 = vec3(+w, +h, +d);
		m_Tris[5].v2 = vec3(+w, +h, -d);

		// Bottom
		m_Tris[6].v0 = vec3(-w, -h, -d);
		m_Tris[6].v1 = vec3(+w, -h, -d);
		m_Tris[6].v2 = vec3(+w, -h, +d);

		m_Tris[7].v0 = vec3(-w, -h, -d);
		m_Tris[7].v1 = vec3(+w, -h, +d);
		m_Tris[7].v2 = vec3(-w, -h, +d);

		// Left
		m_Tris[8].v0 = vec3(-w, -h, +d);
		m_Tris[8].v1 = vec3(-w, +h, +d);
		m_Tris[8].v2 = vec3(-w, +h, -d);

		m_Tris[9].v0 = vec3(-w, -h, +d);
		m_Tris[9].v1 = vec3(-w, +h, -d);
		m_Tris[9].v2 = vec3(-w, -h, -d);

		// Right
		m_Tris[10].v0 = vec3(+w, -h, -d);
		m_Tris[10].v1 = vec3(+w, +h, -d);
		m_Tris[10].v2 = vec3(+w, +h, +d);

		m_Tris[11].v0 = vec3(+w, -h, -d);
		m_Tris[11].v1 = vec3(+w, +h, +d);
		m_Tris[11].v2 = vec3(+w, -h, +d);
#endif
			
#endif

		m_BVHNode = (BVHNode*)_aligned_malloc(sizeof(BVHNode) * s_Num * 2, 64);
		BuildBVH();

#if AS_FLAG == 2
		m_Mesh = std::make_shared<rtrt::Mesh>("Models/BVHAssets/teapot.obj", "Models/BVHAssets/bricks.png");
		m_Mesh->Init();

		m_BVHInstance = std::make_shared<rtrt::BVHInstance>(m_Mesh->m_BVH.get(), 0);
	
#elif AS_FLAG == 1
		m_BVH.reset(new BVH("Models/BVHAssets/armadillo.tri", 30000));

		m_BVHInstances.reset(new BVHInstance[s_Instances]);
		for (int i = 0; i < s_Instances; ++i)
		{
			m_Translations[i] = (vec3(rng.NextFloat(), rng.NextFloat(), rng.NextFloat()) - 0.5f) * 4.0f;

			m_BVHInstances[i].SetInstance(m_BVH.get());
			m_BVHInstances[i].SetTransform(glm::translate(m_Translations[i]));
		}

		m_TLAS.reset(new TLAS(m_BVHInstances.get(), s_Instances));
		m_TLAS->Build();

#endif
		// Test
		{
			m_TestTriangle.v0 = vec3(-1.0f, -1.0f, 0.0f);
			m_TestTriangle.v1 = vec3( 0.0f, +1.0f, 0.0f);
			m_TestTriangle.v2 = vec3(+1.0f, -1.0f, 0.0f);
		}
	}
}

void BVHApp::CleanCustom()
{
	_aligned_free(m_BVHNode);
	m_BVHNode = nullptr;

	m_TraceResult.Destroy();
	m_DebugPass.Cleanup();
}

void BVHApp::Update(float deltaTime)
{
	IGameApp::Update(deltaTime);

	// Inputs
	{
		m_CameraController->Update(deltaTime);
		m_ViewProjMatrix = m_Camera.GetViewProjMatrix();

		Math::Vector3 w = m_Camera.GetForwardVec(), u = m_Camera.GetRightVec(), v = m_Camera.GetUpVec();
		float vfov = m_Camera.GetFOV();
		float aspect = m_Camera.GetAspect();
		float focusDist = 1.0f;
		float halfHeight = std::tanf(vfov / 2.0f);
		float halfWidth = halfHeight / aspect;
		m_LowerLeftCorner = Math::Vector3{ m_Camera.GetPosition() + focusDist * (w - halfWidth * u - halfHeight * v) };
		m_imgHorizontal = 2.0f * focusDist * halfWidth * u;
		m_imgVertical = 2.0f * focusDist * halfHeight * v;
	}

	// GameObjects
	{
		vec3 ro{ Cast((m_Camera.GetPosition())) };
		vec3 rd{ Cast(m_Camera.GetForwardVec()) };
		vec3 lowerLeftCorner{ Cast(m_LowerLeftCorner - m_Camera.GetPosition()) };
		vec3 horizontal{ Cast(m_imgHorizontal) }, vertical{ Cast(m_imgVertical) };
		
		m_Surface->Clear(0);
		vec3 sphereCenter{ 0.0f, 0.0f, -1.0f };
		float radius = 1.0f;

		const int W = m_Surface->width, H = m_Surface->height;
		const float invW = 1.0f / W, invH = 1.0f / H;
#if PARALLEL_IMPL
		const int tileSize = 16;
		const int nXTiles = (W + tileSize - 1) / tileSize;
		const int nYTiles = (H + tileSize - 1) / tileSize;
		const int nTiles = nXTiles * nYTiles;
		concurrency::parallel_for(0, nTiles, [&](int tileIndex)
			{
				int tidy = tileIndex / nXTiles;
				int tidx = tileIndex % nXTiles;

				int x0 = tidx * tileSize;
				int y0 = tidy * tileSize;
				int x1 = std::min(x0 + tileSize, W);
				int y1 = std::min(y0 + tileSize, H);
				bool bIntersect = false;
				for (int y = y0; y < y1; ++y)
				{
					float v = (y + 0.5f) / H;
					for (int x = x0; x < x1; ++x)
					{
						float u = (x + 0.5f) / W;

						uint c = 0;
#if AS_FLAG != 2
						Ray ray{ ro, rd };
						ray.rd = glm::normalize(lowerLeftCorner + u * horizontal + v * vertical);
						ray.rcpD = 1.0f / ray.rd;
#endif

#if  AS_FLAG == 2
						rtrt::Ray ray{ ro, rd };
						ray.rd = glm::normalize(lowerLeftCorner + u * horizontal + v * vertical);
						ray.rcpD = 1.0f / ray.rd;

						rtrt::Intersection isect;

						bIntersect = m_BVHInstance->Intersect(ray, isect);
#elif AS_FLAG == 1
						// bIntersect = m_BVH->Intersect(ray);
						bIntersect = m_TLAS->Intersect(ray);
#else
						bIntersect = IntersectBVH(ray, m_RootNodeIdx);
						/*for (int i = 0; i < s_Num; ++i)
						{
							bIntersect |= IntersectTriangle(ray, m_Tris[i]);
						}*/
#endif
						if (bIntersect)
						{
#if AS_FLAG == 2
							c = Color(vec3(isect.u, isect.v, 1.0f-isect.u - isect.v));
#elif (USE_MODELS || (AS_FLAG == 1))
							c = 500 - (int)(ray.tmax * 42);
							c *= 0x10101;
#else
							c = 0xFFFFFFFF;
#endif
							m_Surface->Plot(x, y, c);
						}
					}
				}
			});
#else
		/**
		 * Improving data locality: To make better use of the caches, we should ensure that data we use is
		 * similar to we have recently seen. This is known as `temporal data locality`. In a ray tracer
		 * this can be achieved by rendering the image in tiles. The pixels in a tile of e.g. 4x4 pixels often
		 * find the same triangles, typically after traversing the same BVH nodes.
		 */
		for (int y = 0; y < H; y += 4)
		{
			for (int x = 0; x < W; x += 4)
			{
				for (int t = 0; t < 4; ++t)
				{
					for (int s = 0; s < 4; ++s)
					{
						float v = (y + t + 0.5f) / H;
						float u = (x + s + 0.5f) / W;

						Ray ray{ ro, rd };
						ray.rd = glm::normalize(lowerLeftCorner + u * horizontal + v * vertical);
						ray.rcpD = 1.0f / ray.rd;

						bool bIntersect = false;
#if 0
						for (int i = 0; i < s_Num; ++i)
						{
							if (bIntersect = IntersectTriangle(ray, m_Tris[i]))
								break;
						}
#else
						bIntersect = IntersectBVH(ray, m_RootNodeIdx);
#endif

						if (bIntersect)
							m_Surface->Plot(x + s, y + t, 0xFFFFFFFF);
					}
				}				
			}
		}
#endif
	}
}

void BVHApp::Render()
{
	GraphicsContext& gfx = GraphicsContext::Begin(L"Composite");

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;

	// Copy texture to GPU
	{
		D3D12_SUBRESOURCE_DATA subresource;
		subresource.RowPitch = (sizeof(uint) * m_Surface->width);
		subresource.SlicePitch = subresource.RowPitch * m_Surface->height;
		subresource.pData = m_Surface->pixels;
		gfx.InitializeTexture(m_TraceResult, 1, &subresource);
	}
	// 
	{
		gfx.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);

		gfx.ClearColor(colorBuffer);
		gfx.SetRenderTarget(colorBuffer.GetRTV());
		gfx.SetViewportAndScissor(m_MainViewport, m_MainScissor);

		m_DebugPass.Render(gfx, m_TraceResult.GetSRV());
	}

	gfx.Finish();
}

void BVHApp::BuildBVH()
{
	for (int i = 0; i < s_Num; ++i)
	{
		m_TriIndices[i] = i;
	}

	for (int i = 0; i < s_Num; ++i)
	{
		auto& tri = m_Tris[i];
		tri.c = (tri.v0 + tri.v1 + tri.v2) / 3.0f;
	}

	// Init root node
	// Assign all triangles to root node
	BVHNode& root = m_BVHNode[m_RootNodeIdx];
	root.leftFirst = 0; root.triCount = s_Num;
	UpdateNodeBounds(m_RootNodeIdx);
	m_NodesUsed = 2;

	// subdivide recursively
	Subdivide(m_RootNodeIdx);
}

void BVHApp::UpdateNodeBounds(uint nodeIdx)
{
	BVHNode& node = m_BVHNode[nodeIdx];
	node.bounds.Reset(); // malloc, not initialized
	for (int i = node.leftFirst, imax = node.leftFirst+node.triCount; i < imax; ++i)
	{
		uint leafTriIdx = m_TriIndices[i];
		Triangle& leafTri = m_Tris[leafTriIdx];
		node.bounds.Union(leafTri);
	}
}

void BVHApp::Subdivide(uint nodeIdx)
{
	BVHNode& node = m_BVHNode[nodeIdx];

#if 0
	// 2 triangles quite often form a quad in real-world scenes. If this quad is axis-aligned, there is
	// no way we can split it (with an axis-aligned plane) in 2 non-empty halves.
	if (node.triCount < 2)
		return;

	// determine split axis and position
	vec3 extent = node.bounds.Extent();
	int axis = 0;
	if (extent.y > extent.x) axis = 1;
	if (extent.z > extent[axis]) axis = 2;

	float splitPos = node.bounds.bmin[axis] + extent[axis] * 0.5f;

#else
	// Determine split axis using SAH
	int axis = 0;
	float splitPos;
	float splitCost = FindBestSplitPlane(node, axis, splitPos);
	float noSplitCost = node.CalculateNodeCost();
	// Checks if the best split cost is actually an improvement over not splitting.
	if (splitCost >= noSplitCost)
		return;

#endif

	// in-place partition
	int i = node.leftFirst;
	int j = i + node.triCount - 1;
	while (i <= j)
	{
		if (m_Tris[m_TriIndices[i]].c[axis] < splitPos)
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

	// Create child nodes
	int leftChildIdx = m_NodesUsed++;
	int rightChildIdx = m_NodesUsed++;

	auto& leftChildNode = m_BVHNode[leftChildIdx];
	leftChildNode.leftFirst = node.leftFirst;
	leftChildNode.triCount = leftCount;
	UpdateNodeBounds(leftChildIdx);

	auto& rightChildNode = m_BVHNode[rightChildIdx];
	rightChildNode.leftFirst = i;
	rightChildNode.triCount = node.triCount - leftCount;
	UpdateNodeBounds(rightChildIdx);

	// Set the primitive count of the parent node to 0.
	node.leftFirst = leftChildIdx;
	node.triCount = 0;

	Subdivide(leftChildIdx);
	Subdivide(rightChildIdx);
}

// 1. Terminate if the ray misses the AABB of the node
// 2. If the node nis a leaf, intersect the ray with the triangles in the leaf
// 3. Otherwise, recurse into the left and right child
bool BVHApp::IntersectBVH(Ray& ray, uint nodeIdx)
{
#if 0
	const auto& node = m_BVHNode[nodeIdx];
	bool bIntersect = IntersectAABB(ray, node.bounds);
	if (bIntersect)
	{
		if (node.isLeaf())
		{
			for (int i = node.leftFirst, imax = node.leftFirst+node.triCount; i < imax; ++i)
			{
				bIntersect = IntersectTriangle(ray, m_Tris[m_TriIndices[i]]);
			}
		}
		else
		{
			bIntersect = IntersectBVH(ray, node.leftFirst) || IntersectBVH(ray, node.leftFirst+1);
		}
	}
#else
	BVHNode* node = &m_BVHNode[m_RootNodeIdx], * stack[128];
	uint stackCount = 0;
	bool bIntersect = false;
	while (true)
	{
		if (node->isLeaf())
		{
			for (uint i = node->leftFirst, imax = node->leftFirst + node->triCount; i < imax; ++i)
			{
				bIntersect |= IntersectTriangle(ray, m_Tris[m_TriIndices[i]]);
			}
			if (stackCount == 0)
				break;
			else
				node = stack[--stackCount];
		}
		else
		{
			BVHNode* pChild0 = &m_BVHNode[node->leftFirst];
			BVHNode* pChild1 = &m_BVHNode[node->leftFirst + 1];
			float d0 = IntersectAABB(ray, pChild0->bounds);
			float d1 = IntersectAABB(ray, pChild1->bounds);
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
#endif

	return bIntersect;
}

float BVHApp::FindBestSplitPlane(BVHNode& node, int& axis, float& splitPos)
{
	float bestCost = 1e10f;
	for (int a = 0; a < 3; ++a)
	{
		// Centroid bounds
		// The first split plane candidate is on the first centroid, the last one on the last centriod.
		float cmin = 1e5f, cmax = -1e5f;
		for (int i = node.leftFirst, imax = node.leftFirst +node.triCount; i < imax; ++i)
		{
			Triangle& tri = m_Tris[m_TriIndices[i]];
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
			Triangle& tri = m_Tris[m_TriIndices[i]];
			int binIdx = std::min( (int)((tri.c[a] - cmin)* scale), s_Bins-1);
			Bin& bin = bins[binIdx];
			++bin.triCount;
			bin.bounds.Union(tri.v0); bin.bounds.Union(tri.v1); bin.bounds.Union(tri.v2);
		}

#if 1
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

#else
		float cost[s_Bins];
		for (int i = 0; i < s_Bins-1; ++i)
		{
			Bounds b0, b1;
			int count0 = 0, count1 = 0;
			for (int j = 0; j <= i; ++j)
			{
				b0.Union(bins[j].bounds);
				count0 += bins[j].triCount;
			}
			for (int j = i+1; j < s_Bins; ++j)
			{
				b1.Union(bins[j].bounds);
				count1 += bins[j].triCount;
			}
			// cost[i] = 1 + (count0 * b0.Area() + count1 * b1.Area()) / node.bounds.Area();
			cost[i] = (count0 * b0.Area() + count1 * b1.Area());
		}
		float minCost = cost[0];
		int minCostSplitBin = 0;

		for (int i = 1; i < s_Bins-1; ++i)
		{
			if (cost[i] < minCost)
			{
				minCost = cost[i];
				minCostSplitBin = i;
			}
		}
		if (minCost < bestCost)
		{
			scale = 1.0f / scale;
			bestCost = minCost;
			axis = a;
			splitPos = cmin + scale * (minCostSplitBin + 1);
		}
#endif

	}

	return bestCost;
}

/**
 *	The technique where we reuse a BVH for an animated mesh is called `refitting`. The process
 * starts at the leaf nodes, which contain the (now changed) triangles. Each leaf node gets an updated
 * bounding box. The update may have consequences for the parent nodes of the leaf nodes, so
 * we adjust their bounds as well, by making them tightly fit their child nodes. We proceed this way until
 * we reach the root of the tree.
 *	When we created the BVH, we used node 0 for the root. After that, every child node got allocated after
 * its parent; we thus know that the index of a child is always greater than the index of its parent.
 * We can exploit this by visiting all nodes starting at the end of the list, working our way back to
 * the first node with outdated child nodes.
 *	NOTE:: This refitting requires the animation frames have the same number of triangles. It also requires that
 * the structure of the animation frames is roughly equal.
 *	In general, refitting is applied when we have subtle animation: trees waving in the wind, objects changing
 * position, perhaps in some cases a walking character. In those cases, we get the BVH update almost for free,
 * often at the expense of (some) tree quality. In other cases, a rebuild will be the better option. We can
 * also combine rebuilding and refitting: in that case, we refit some subsequent frames, and after a few refits,
 * we rebuild, to ensure that the BVH doesn't deteriorate too much.
 */
void BVHApp::RefitBVH()
{
	for (int i = m_NodesUsed-1; i >= 0; --i)
	{
		auto& node = m_BVHNode[i];
		if (!node.isValid())
			continue;

		if (node.isLeaf())
		{
			// Leaf node - adjust bounds to contained triangles
			UpdateNodeBounds(i);
		}
		else
		{
			// Interior node - adjust bounds to child node bounds
			auto& node0 = m_BVHNode[node.leftFirst];
			auto& node1 = m_BVHNode[node.leftFirst + 1];
			auto& bounds = node.bounds;
			bounds.Reset();
			bounds.Union(node0.bounds);
			bounds.Union(node1.bounds);
		}
	}
}

/**
 * An optimal BVH minimizes the total number of intersections. Applied to the local problem of picking a good split plane:
 * since the number of bounding boxes after the split is constant, the optimal split plane is the one that minimizes
 * the number of intersections between a ray and the primitives in both child nodes.
 * ...
 * The chance that a random ray hits a random box is proportional to the surface area of the box.
 * Surface Area Heuristic: C_SAH = N_left * A_left + N_right * A_right
 * In words: the `cost` of a split is proportional to the summed cost of intersecting the 2 resulting boxes, including
 * the triangles they store. The cost of a box with triangles is proportional to the number of triangles, times
 * the surface area of the box.
 */
