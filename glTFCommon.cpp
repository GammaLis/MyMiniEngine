#include "glTFCommon.h"
#include <d3d12.h>

namespace glTF
{
	DXGI_FORMAT GetDXFormat(glDataType componentType, glType type, bool normalized)
	{
		if (componentType == glDataType::BYTE)
		{
			if (type == glType::SCALAR)
			{
				if (normalized)
					return DXGI_FORMAT_R8_SNORM;
				else
					return DXGI_FORMAT_R8_SINT;
			}
			else if (type == glType::VEC2)
			{
				if (normalized)
					return DXGI_FORMAT_R8G8_SNORM;
				else
					return DXGI_FORMAT_R8G8_SINT;
			}
			else if (type == glType::VEC4)
			{
				if (normalized)
					return DXGI_FORMAT_R8G8B8A8_SNORM;
				else
					return DXGI_FORMAT_R8G8B8A8_SINT;
			}
		}
		else if (componentType == glDataType::UNSIGNED_BYTE)
		{
			if (type == glType::SCALAR)
			{
				if (normalized)
					return DXGI_FORMAT_R8_UNORM;
				else
					return DXGI_FORMAT_R8_UINT;
			}
			else if (type == glType::VEC2)
			{
				if (normalized)
					return DXGI_FORMAT_R8G8_UNORM;
				else
					return DXGI_FORMAT_R8G8_UINT;
			}
			else if (type == glType::VEC4)
			{
				if (normalized)
					return DXGI_FORMAT_R8G8B8A8_UNORM;
				else
					return DXGI_FORMAT_R8G8B8A8_UINT;
			}
		}
		else if (componentType == glDataType::SHORT)
		{
			if (type == glType::SCALAR)
			{
				if (normalized)
					return DXGI_FORMAT_R16_SNORM;
				else
					return DXGI_FORMAT_R16_SINT;
			}
			else if (type == glType::VEC2)
			{
				if (normalized)
					return DXGI_FORMAT_R16G16_SNORM;
				else
					return DXGI_FORMAT_R16G16_SINT;
			}
			else if (type == glType::VEC4)
			{
				if (normalized)
					return DXGI_FORMAT_R16G16B16A16_SNORM;
				else
					return DXGI_FORMAT_R16G16B16A16_UINT;
			}
		}
		else if (componentType == glDataType::UNSIGNED_SHORT)
		{
			if (type == glType::SCALAR)
			{
				if (normalized)
					return DXGI_FORMAT_R16_UNORM;
				else
					return DXGI_FORMAT_R16_UINT;
			}
			else if (type == glType::VEC2)
			{
				if (normalized)
					return DXGI_FORMAT_R16G16_UNORM;
				else
					return DXGI_FORMAT_R16G16_UINT;
			}
			else if (type == glType::VEC4)
			{
				if (normalized)
					return DXGI_FORMAT_R16G16B16A16_UNORM;
				else
					return DXGI_FORMAT_R16G16B16A16_UINT;
			}
		}
		else if (componentType == glDataType::INT)	// 很少出现
		{
			if (type == glType::SCALAR)
				return DXGI_FORMAT_R32_SINT;
			else if (type == glType::VEC2)
				return DXGI_FORMAT_R32G32_SINT;
			else if (type == glType::VEC3)
				return DXGI_FORMAT_R32G32B32_SINT;
			else if (type == glType::VEC4)
				return DXGI_FORMAT_R32G32B32A32_SINT;
		}
		else if (componentType == glDataType::UNSIGNED_INT)	// 很少出现
		{
			if (type == glType::SCALAR)
				return DXGI_FORMAT_R32_UINT;
			else if (type == glType::VEC2)
				return DXGI_FORMAT_R32G32_UINT;
			else if (type == glType::VEC3)
				return DXGI_FORMAT_R32G32B32_UINT;
			else if (type == glType::VEC4)
				return DXGI_FORMAT_R32G32B32A32_UINT;
		}
		else if (componentType == glDataType::FLOAT)
		{
			if (type == glType::SCALAR)
				return DXGI_FORMAT_R32_FLOAT;
			else if (type == glType::VEC2)
				return DXGI_FORMAT_R32G32_FLOAT;
			else if (type == glType::VEC3)
				return DXGI_FORMAT_R32G32B32_FLOAT;
			else if (type == glType::VEC4)
				return DXGI_FORMAT_R32G32B32A32_FLOAT;
		}

		return DXGI_FORMAT_UNKNOWN;
	}
}
