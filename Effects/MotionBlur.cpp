#include "MotionBlur.h"
#include "Camera.h"
#include "Graphics.h"
#include "GfxCommon.h"
#include "CommandContext.h"
#include "ColorBuffer.h"

// compiled shaders
#include "CameraVelocityCS.h"

using namespace MyDirectX;
using namespace Math;

struct alignas(16) CSConstants
{
	Matrix4 _CurToPrevXForm;
};

void MotionBlur::Init(ID3D12Device* pDevice)
{
	// root signature
	{
		m_RootSignature.Reset(4, 1);
		m_RootSignature[0].InitAsConstants(0, 4);
		m_RootSignature[1].InitAsConstantBuffer(1);
		m_RootSignature[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 8);
		m_RootSignature[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 8);
		m_RootSignature.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearBorderDesc);
		m_RootSignature.Finalize(pDevice, L"Motion Blur");
	}

	// PSOs
	{
#define CreatePSO(pso, shaderByteCode) \
	pso.SetRootSignature(m_RootSignature); \
	pso.SetComputeShader(shaderByteCode, sizeof(shaderByteCode)); \
	pso.Finalize(pDevice);

		CreatePSO(m_CameraVelocityCS[0], CameraVelocityCS);
		CreatePSO(m_CameraVelocityCS[1], CameraVelocityCS);
#undef CreatePSO
	}
}

void MotionBlur::Shutdown()
{
}

/**
	Linear Z ends up being faster since we haven't officially decompressed the depth buffer.
You would think that it might be slower to use linear Z because we have to convert it back to
hyperbolic Z for the reprojection. Nevertheless, the reduced bandwidth and decompress eliminate
make Linear Z the better choice. (The choice also lets you evict the depth buffer from ESRAM.)
*/ 
void MotionBlur::GenerateCameraVelocityBuffer(CommandContext& context, const Math::Camera& camera, uint64_t frameIndex, bool bUseLinearZ)
{
	GenerateCameraVelocityBuffer(context, camera.GetReprojectionMatrix(), camera.GetNearClip(), camera.GetFarClip(),
		frameIndex, bUseLinearZ);
}

// reprojectionMatrix - Inverse(ViewProjMat) * Prev_ViewProjMat
// HS -> WorldSpace, WorldSpace -> Prev HS (Prev_ViewProjMat) 
void MotionBlur::GenerateCameraVelocityBuffer(CommandContext& context, const Math::Matrix4& reprojectionMatrix, 
	float nearClip, float farClip, uint64_t frameIndex, bool bUseLinearZ)
{
	// Generate Camera Velocity 
	ComputeContext& computeContext = context.GetComputeContext();

	computeContext.SetRootSignature(m_RootSignature);
	computeContext.SetPipelineState(m_CameraVelocityCS[bUseLinearZ ? 1 : 0]);

	auto& colorBuffer = Graphics::s_BufferManager.m_SceneColorBuffer;
	auto& depthBuffer = Graphics::s_BufferManager.m_SceneDepthBuffer;
	auto& linearDepth = Graphics::s_BufferManager.m_LinearDepth[frameIndex % 2];
	auto& velocityBuffer = Graphics::s_BufferManager.m_VelocityBuffer;

	uint32_t width = colorBuffer.GetWidth();
	uint32_t height = colorBuffer.GetHeight();

	float rcpHalfDimX = 2.0f / width;
	float rcpHalfDimY = 2.0f / height;
	float rcpZMagic = nearClip / (farClip - nearClip);

	// ScreenSpace -> NDC
	// Note Z coordinate
	Matrix4 preMult = Matrix4(
		Vector4(rcpHalfDimX, 0.0f, 0.0f, 0.0f),
		Vector4(0.0f, -rcpHalfDimY, 0.0f, 0.0f),
		Vector4(0.0f, 0.0f, bUseLinearZ ? rcpZMagic : 1.0f, 0.0f),
		Vector4(-1.0f, 1.0f, bUseLinearZ ? -rcpZMagic : 0.0f, 1.0f)
	);
	// NDC -> ScreenSpace
	Matrix4 postMult = Matrix4(
		Vector4(1.0f / rcpHalfDimX, 0.0f, 0.0f, 0.0f),
		Vector4(0.0f, -1.0f / rcpHalfDimY, 0.0f, 0.0f),
		Vector4(0.0f, 0.0f, 1.0f, 0.0f),
		Vector4(1.0f / rcpHalfDimX, 1.0f / rcpHalfDimY, 0.0f, 1.0f)
	);
	// Note: 'Matrix4' mul order is not the same as 'DirectX::XMMATRIX'
	Matrix4 curToPrevXForm = postMult * reprojectionMatrix * preMult;	// Matrix4 mul is right mul, Matrix4.operator*(M')=> M'*mat

	CSConstants csConstants;
	csConstants._CurToPrevXForm = Transpose(curToPrevXForm);
	computeContext.SetDynamicConstantBufferView(1, sizeof(csConstants), &csConstants);

	D3D12_CPU_DESCRIPTOR_HANDLE depthSRV;
	if (bUseLinearZ)
	{
		computeContext.TransitionResource(linearDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		depthSRV = linearDepth.GetSRV();
	}
	else
	{
		computeContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		depthSRV = depthBuffer.GetDepthSRV();
	}
	computeContext.SetDynamicDescriptor(2, 0, depthSRV);

	computeContext.SetDynamicDescriptor(3, 0, velocityBuffer.GetUAV());

	computeContext.Dispatch2D(width, height);

}
