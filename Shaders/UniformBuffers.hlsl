#ifndef UNIFORM_BUFFERS_INCLUDED
#define UNIFORM_BUFFERS_INCLUDED

struct FViewUniformBufferParameters
{
	float4x4 ViewMatrix;
	float4x4 ProjMatrix;
	float4x4 ViewProjMatrix;
	float4x4 PrevViewProjMatrix;
	float4x4 ReprojectMatrix;
	float4x4 InvViewMatrix;
	float4x4 InvProjMatrix;
	float4x4 ScreenToViewMatrix;
	float4x4 ScreenToWorldMatrix;
	float4 BufferSizeAndInvSize;
	float4 InvDeviceZToWorldZTransform;
	float4 DebugColor;
	float4 CamPos; // .w unused
	float ZMagic; // (zFar - zNear) / zNear
	float ZNear;
	float ZFar;
	uint FrameIndex;
};

ConstantBuffer<FViewUniformBufferParameters> _View 	: register(b0, space1);

#endif // UNIFORM_BUFFERS_INCLUDED
