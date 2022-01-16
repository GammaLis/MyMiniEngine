#ifndef HLSL_COMPAT_INCLUDED
#define HLSL_COMPAT_INCLUDED

#include "pch.h"

#define OUTPARAM(type, name) type& name
#define INOUTPARAM(type, name) type& name

struct float2
{
	float x, y;
};

struct float3
{
	float x, y, z;
};

struct float4
{
	float x, y, z, w;
};

using uint = uint32_t;
using uint2 = uint[2];

struct alignas(16) uint4
{
	uint x, y, z, w;
};

struct alignas(16) float4x4
{
	float mat[16];
};

inline float3 operator+ (const float3& a, const float3& b)
{
	return float3{ a.x + b.x, a.y + b.y, a.z + b.z };
}

inline float3 operator- (const float3& a, const float3& b)
{
	return float3{ a.x - b.x, a.y - b.y, a.z - b.z };
}

inline float3 operator* (const float3& a, const float3& b)
{
	return float3{ a.x * b.x, a.y * b.y, a.z * b.z };
}

inline float3 abs(const float3& a)
{
	return float3{ std::abs(a.x), std::abs(a.y), std::abs(a.z) };
}

inline float min(float a, float b)
{
	return std::min(a, b);
}

inline float3 min(const float3& a, const float3& b)
{
	return float3{ std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z) };
}

inline float max(float a, float b)
{
	return std::max(a, b);
}

inline float3 max(const float3& a, const float3& b)
{
	return float3{ std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z) };
}

inline float sign(float v)
{
	if (v < 0)
		return -1;
	return 1;
}

inline float dot(const float3& a, const float3& b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline float3 cross(const float3& a, const float3& b)
{
	return float3
	{
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x
	};
}

#endif // HLSL_COMPAT_INCLUDED
