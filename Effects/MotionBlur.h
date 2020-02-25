#pragma once
#include "pch.h"
#include "RootSignature.h"
#include "PipelineState.h"

namespace Math
{
	class Matrix4;
	class Camera;
}

namespace MyDirectX
{
	class CommandContext;

	class MotionBlur
	{
	public:
		void Init(ID3D12Device* pDevice);

		void Shutdown();

		void GenerateCameraVelocityBuffer(CommandContext& context, const Math::Camera& camera, uint64_t frameIndex, bool bUseLinearZ = true);
		void GenerateCameraVelocityBuffer(CommandContext& context, const Math::Matrix4& reprojectionMatrix,
			float nearClip, float farClip, uint64_t frameIndex, bool bUseLinearZ = true);

		bool m_Enabled = true;

	private:

		// root signature
		RootSignature m_RootSignature;
		// PSOs
		ComputePSO m_CameraVelocityCS[2];
	};

}