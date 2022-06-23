#include "BVHApp.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "Math/Random.h"
#include "Core/Utility.h"

// #define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"

using namespace MyDirectX;
using namespace glm;

glm::vec3 Cast(Math::Vector3 v)
{
	return glm::vec3{ v.GetX(), v.GetY(), v.GetZ() };
}

glm::vec4 Cast(Math::Vector4 v)
{
	return glm::vec4{ v.GetX(), v.GetY(), v.GetZ(), v.GetW() };
}

uint Color(glm::vec3 c)
{
	uint ic = 0;
	ic += (uint(c.x * 255.9f) & 0xFF);
	ic += (uint(c.y * 255.9f) & 0xFF) << 8;
	ic += (uint(c.z * 255.9f) & 0xFF) << 16;

	return ic;
}

uint Color(glm::vec4 c)
{
	uint ic = 0;
	ic += (uint(c.x * 255.9f) & 0xFF);
	ic += (uint(c.y * 255.9f) & 0xFF) << 8;
	ic += (uint(c.z * 255.9f) & 0xFF) << 16;
	ic += (uint(c.w * 255.9f) & 0xFF) << 24;

	return ic;
}

bool IntersectTriangle(Ray& ray, const Triangle& tri)
{
	const vec3 edge1 = tri.v1 - tri.v0;
	const vec3 edge2 = tri.v2 - tri.v0;
	const vec3 h = glm::cross(ray.rd, edge2);
	const float a = glm::dot(edge1, h);
	if (a > -0.0001f && a < 0.0001f) // ray parallel to 
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
	if (t > 0.0001f)
		ray.tmax = std::min(ray.tmax, t);

	return ray.tmax == t;
}

// dot( o + t * dir - C, o + t * dir - C ) = R^2 
float IntersectSphere(const Ray &ray, const vec3& center, float radius)
{
	vec3 oc = ray.ro - center;
	float a = glm::dot(ray.rd, ray.rd);
	float b = 2.0f * glm::dot(oc, ray.rd);
	float c = glm::dot(oc, oc) - radius * radius;
	float discriminant = b * b - 4.0f * a * c;

	if (discriminant < 0.0f)
		return -1.0f;
	else
		return (-b - glm::sqrt(discriminant)) / (2.0f * a);
}

bool IntersectAABB(const Ray &ray, const Bounds &bounds)
{
	float tx1 = (bounds.bmin.x - ray.ro.x) / ray.rd.x, tx2 = (bounds.bmax.x - ray.ro.x) / ray.rd.x;
	float tmin = std::min(tx1, tx2), tmax = std::max(tx1, tx2);

	float ty1 = (bounds.bmin.y - ray.ro.y) / ray.rd.y, ty2 = (bounds.bmax.y - ray.ro.y) / ray.rd.y;
	tmin = std::max(tmin, std::min(ty1, ty2));
	tmax = std::min(tmax, std::max(ty1, ty2));

	float tz1 = (bounds.bmin.z - ray.ro.z) / ray.rd.z, tz2 = (bounds.bmax.z - ray.ro.z) / ray.rd.z;
	tmin = std::max(tmin, std::min(tz1, tz2));
	tmax = std::min(tmax, std::max(tz1, tz2));

	return tmin <= tmax && tmin < ray.tmax && tmax > 0;
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


/// Surface
char MyDirectX::g_Font[51][5][6];
bool MyDirectX::g_bFontInited = false;
int MyDirectX::g_Translation[256];

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
	m_Surface = std::make_unique<Surface>(width / 2, height / 2);
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
	}
	// GameObjects
	{
		Math::RandomNumberGenerator rng;
		rng.SetSeed(GetCurrentTime());
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

		BuildBVH();

		{
			m_TestTriangle.v0 = vec3(-1.0f, -1.0f, 0.0f);
			m_TestTriangle.v1 = vec3( 0.0f, +1.0f, 0.0f);
			m_TestTriangle.v2 = vec3(+1.0f, -1.0f, 0.0f);
		}
	}
}

void BVHApp::CleanCustom()
{
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
		for (int y = 0; y < H; ++y)
		{
			float v = (y + 0.5f) / H;
			for (int x = 0; x < W; ++x)
			{
				float u = (x + 0.5f) / W;

				Ray ray{ ro, rd, Ray::TMIN, Ray::TMAX };
				ray.rd = glm::normalize(lowerLeftCorner + u * horizontal + v * vertical);

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
					m_Surface->Plot(x, y, 0xFFFFFFFF);
			}
		}
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
	m_NodesUsed = 1;

	// subdivide recursively
	Subdivide(m_RootNodeIdx);
}

void BVHApp::UpdateNodeBounds(uint nodeIdx)
{
	BVHNode& node = m_BVHNode[nodeIdx];
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

	return bIntersect;
}
