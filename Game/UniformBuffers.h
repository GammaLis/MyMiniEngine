#pragma once

#include "pch.h"
#include "DynamicUploadBuffer.h"

namespace MyDirectX
{
	namespace UniformBuffers
	{
		struct alignas(16) FViewUniformBufferParameters
		{
			Math::Matrix4 ViewMatrix;
			Math::Matrix4 ProjMatrix;
			Math::Matrix4 ViewProjMatrix;
			Math::Matrix4 PrevViewProjMatrix;
			Math::Matrix4 ReprojectMatrix;
			Math::Matrix4 InvViewMatrix;
			Math::Matrix4 InvProjMatrix;
			Math::Matrix4 ScreenToViewMatrix;
			Math::Matrix4 ScreenToWorldMatrix;
			Math::Vector4 BufferSizeAndInvSize;
			Math::Vector4 InvDeviceZToWorldZTransform;
			Math::Vector4 DebugColor;
			Math::Vector3 CamPos;
			float ZMagic;
			float ZNear;
			float ZFar;
			uint32_t FrameIndex;
		};
		extern FViewUniformBufferParameters g_ViewUniformBufferParameters;
		extern DynamicUploadBuffer g_ViewUniformBuffer;
		extern uint32_t g_ViewUniformBufferParamsRegister;
		
		extern uint32_t g_UniformBufferParamsNum;
		extern uint32_t g_UniformBufferParamsSpace;
	}
}
