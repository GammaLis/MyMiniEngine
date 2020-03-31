#pragma once
#include "pch.h"
#define GLM_FORCE_CTOR_INIT
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

namespace MFalcor
{
	using Vector2 = glm::vec2;
	using Vector3 = glm::vec3;
	using Vector4 = glm::vec4;
	using Matrix3x3 = glm::mat3x3;
	using Matrix4x4 = glm::mat4x4;

	// anix-aligned bounding box
	struct BoundingBox
	{
		Vector3 vMin;
		Vector3 vMax;

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
			Vector3 xMin = std::min(xa, xb);
			Vector3 xMax = std::max(xa, xb);

			Vector3 ya = Vector3(mat[1] * vMin.y);
			Vector3 yb = Vector3(mat[1] * vMax.y);
			Vector3 yMin = std::min(ya, yb);
			Vector3 yMax = std::max(ya, yb);

			Vector3 za = Vector3(mat[2] * vMin.z);
			Vector3 zb = Vector3(mat[2] * vMax.z);
			Vector3 zMin = std::min(za, zb);
			Vector3 zMax = std::max(za, zb);

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
			Vector3 xMin = std::min(xa, xb);
			Vector3 xMax = std::max(xa, xb);

			Vector3 ya = Vector3(mat[1] * vMin.y);
			Vector3 yb = Vector3(mat[1] * vMax.y);
			Vector3 yMin = std::min(ya, yb);
			Vector3 yMax = std::max(ya, yb);

			Vector3 za = Vector3(mat[2] * vMin.z);
			Vector3 zb = Vector3(mat[2] * vMax.z);
			Vector3 zMin = std::min(za, zb);
			Vector3 zMax = std::max(za, zb);

			Vector3 newMin = xMin + yMin + zMin + Vector3(mat[3]);
			Vector3 newMax = xMax + yMax + zMax + Vector3(mat[3]);

			return BoundingBox(newMin, newMax);
		}

		const BoundingBox& Union(const BoundingBox other)
		{
			vMin = std::min(vMin, other.vMin);
			vMax = std::max(vMax, other.vMax);
			return *this;
		}
		BoundingBox Union(const BoundingBox& other) const
		{
			return BoundingBox(std::min(vMin, other.vMin), std::max(vMax, other.vMax));
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

	BoundingBox operator*(const Matrix4x4& mat, const BoundingBox& bbox)
	{
		return bbox.Transform(mat);
	}
}
