#pragma once
#include "pch.h"
#define GLM_FORCE_CTOR_INIT
#define GLM_FORCE_XYZW_ONLY
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

namespace MFalcor
{
	using Vector2 = glm::vec2;
	using Vector3 = glm::vec3;
	using Vector4 = glm::vec4;
	using UVector2 = glm::uvec2;
	using UVector3 = glm::uvec3;
	using UVector4 = glm::uvec4;
	using Quaternion = glm::quat;
	using Matrix3x3 = glm::mat3x3;
	using Matrix4x4 = glm::mat4x4;
	namespace MMATH = glm;

	// col major to row major
	inline Math::Vector3 Cast(const Vector3& glvec)
	{
		return Math::Vector3(glvec.x, glvec.y, glvec.z);
	}
	inline Math::Vector4 Cast(const Vector4& glvec)
	{
		return Math::Vector4(glvec.x, glvec.y, glvec.z, glvec.w);
	}
	inline Math::Matrix3 Cast(const Matrix3x3& glmat)
	{
		return Math::Matrix3(
			Math::Vector3(glmat[0][0], glmat[1][0], glmat[2][0]),
			Math::Vector3(glmat[0][1], glmat[1][1], glmat[2][1]),
			Math::Vector3(glmat[0][2], glmat[1][2], glmat[2][2]));
	}
	inline Math::Matrix4 Cast(const Matrix4x4 &glmat)
	{
		return Math::Matrix4(
			Math::Vector4(glmat[0][0], glmat[1][0], glmat[2][0], glmat[3][0]),
			Math::Vector4(glmat[0][1], glmat[1][1], glmat[2][1], glmat[3][1]),
			Math::Vector4(glmat[0][2], glmat[1][2], glmat[2][2], glmat[3][2]), 
			Math::Vector4(glmat[0][3], glmat[1][3], glmat[2][3], glmat[3][3]));
	}

	// row major to col major 
	inline Vector3 Cast(const Math::Vector3 &vec)
	{
		return Vector3((float)vec.GetX(), (float)vec.GetY(), (float)vec.GetZ());
	}
	inline Vector4 Cast(const Math::Vector4 &vec)
	{
		return Vector4((float)vec.GetX(), (float)vec.GetY(), (float)vec.GetZ(), (float)vec.GetW());
	}
	inline Matrix3x3 Cast(const Math::Matrix3 &mat)
	{
		const auto& v0 = mat.GetX(), v1 = mat.GetY(), v2 = mat.GetZ();
		return Matrix3x3(
			(float)v0.GetX(), (float)v1.GetX(), (float)v2.GetX(),
			(float)v0.GetY(), (float)v1.GetY(), (float)v2.GetY(),
			(float)v0.GetZ(), (float)v1.GetZ(), (float)v2.GetZ());
	}
	inline Matrix4x4 Cast(const Math::Matrix4& mat)
	{
		const auto& v0 = mat.GetX(), v1 = mat.GetY(), v2 = mat.GetZ(), v3 = mat.GetW();
		return Matrix4x4(
			(float)v0.GetX(), (float)v1.GetX(), (float)v2.GetX(), (float)v3.GetX(),
			(float)v0.GetY(), (float)v1.GetY(), (float)v2.GetY(), (float)v3.GetY(),
			(float)v0.GetZ(), (float)v1.GetZ(), (float)v2.GetZ(), (float)v3.GetZ(),
			(float)v0.GetW(), (float)v1.GetW(), (float)v2.GetW(), (float)v3.GetW());
	}
	
	// axis-aligned bounding box
	struct BoundingBox
	{
		Vector3 vMin;
		Vector3 vMax;

		BoundingBox() = default;

		BoundingBox(const Vector3 &minVal, const Vector3 &maxVal)
		{
			ASSERT(minVal.x <= maxVal.x && minVal.y <= maxVal.y && minVal.z <= maxVal.z);

			vMin = minVal;
			vMax = maxVal;			
		}

		// transform the bounding box transformed by a matrix
		const BoundingBox &Transform(const Matrix4x4& mat)
		{
			Vector3 xa = Vector3(mat[0] * vMin.x);
			Vector3 xb = Vector3(mat[0] * vMax.x);
			Vector3 xMin = MMATH::min(xa, xb);
			Vector3 xMax = MMATH::max(xa, xb);

			Vector3 ya = Vector3(mat[1] * vMin.y);
			Vector3 yb = Vector3(mat[1] * vMax.y);
			Vector3 yMin = MMATH::min(ya, yb);
			Vector3 yMax = MMATH::max(ya, yb);

			Vector3 za = Vector3(mat[2] * vMin.z);
			Vector3 zb = Vector3(mat[2] * vMax.z);
			Vector3 zMin = MMATH::min(za, zb);
			Vector3 zMax = MMATH::max(za, zb);

			Vector3 newMin = xMin + yMin + zMin + Vector3(mat[3]);
			Vector3 newMax = xMax + yMax + zMax + Vector3(mat[3]);

			vMin = newMin;
			vMax = newMax;

			return *this;
		}
		BoundingBox Transform(const Matrix4x4 &mat) const
		{
			Vector3 xa = Vector3(mat[0] * vMin.x);
			Vector3 xb = Vector3(mat[0] * vMax.x);
			Vector3 xMin = MMATH::min(xa, xb);
			Vector3 xMax = MMATH::max(xa, xb);

			Vector3 ya = Vector3(mat[1] * vMin.y);
			Vector3 yb = Vector3(mat[1] * vMax.y);
			Vector3 yMin = MMATH::min(ya, yb);
			Vector3 yMax = MMATH::max(ya, yb);

			Vector3 za = Vector3(mat[2] * vMin.z);
			Vector3 zb = Vector3(mat[2] * vMax.z);
			Vector3 zMin = MMATH::min(za, zb);
			Vector3 zMax = MMATH::max(za, zb);

			Vector3 newMin = xMin + yMin + zMin + Vector3(mat[3]);
			Vector3 newMax = xMax + yMax + zMax + Vector3(mat[3]);

			return BoundingBox(newMin, newMax);
		}

		const BoundingBox& Union(const BoundingBox other)
		{
			vMin = MMATH::min(vMin, other.vMin);
			vMax = MMATH::max(vMax, other.vMax);
			return *this;
		}
		BoundingBox Union(const BoundingBox& other) const
		{
			return BoundingBox(MMATH::min(vMin, other.vMin), MMATH::max(vMax, other.vMax));
		}

		Vector3 GetCenter() const
		{
			return (vMin + vMax) * 0.5f;
		}
		Vector3 GetExtent() const
		{
			return vMax - vMin;
		}
	};

	inline BoundingBox operator*(const Matrix4x4& mat, const BoundingBox& bbox)
	{
		return bbox.Transform(mat);
	}
}
