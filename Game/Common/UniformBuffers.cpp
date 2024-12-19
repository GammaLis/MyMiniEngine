#include "UniformBuffers.h"
#include "Math.h"

namespace MyDirectX
{
	namespace UniformBuffers
	{
		FViewUniformBufferParameters g_ViewUniformBufferParameters;
		DynamicUploadBuffer g_ViewUniformBuffer;
		uint32_t g_ViewUniformBufferParamsRegister = 0;

		uint32_t g_UniformBufferParamsNum = 1;
		uint32_t g_UniformBufferParamsSpace = 1;
	}
}
