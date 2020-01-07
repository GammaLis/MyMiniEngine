#include "MyCamera.h"

using namespace MyDirectX::Camera;

MyCamera::MyCamera(XMFLOAT3 position, XMFLOAT3 target, XMFLOAT3 worldUp, float fovY, float aspect, float nearClip, float farClip)
{
	LookAt(position, target, worldUp);

	SetFrustum(fovY, aspect, nearClip, farClip);
}

MyCamera::MyCamera(float fovY, float aspect, float nearClip, float farClip)
	:MyCamera(XMFLOAT3(0.0f, 3.0f, -6.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), 
		fovY, aspect, nearClip, farClip)
{
}

MyCamera::~MyCamera()
{
}

void MyCamera::SetFrustum(float fovY, float aspect, float nearClip, float farClip)
{
	// cache properties
	mFovY = fovY;
	mAspect = aspect;
	mNearClipPlane = nearClip;
	mFarClipPlane = farClip;

	mNearWindowHeight = 2.0f * mNearClipPlane * tanf(0.5f * mFovY);
	mFarWindowHeight = 2.0f * mFarClipPlane * tanf(0.5f * mFovY);

	XMMATRIX perpectiveMat = XMMatrixPerspectiveFovLH(mFovY, mAspect, mNearClipPlane, mFarClipPlane);
	XMStoreFloat4x4(&mProjMat, perpectiveMat);
}

void MyCamera::LookAt(FXMVECTOR pos, FXMVECTOR target, FXMVECTOR worldUp)
{
	XMVECTOR forward = XMVector3Normalize(XMVectorSubtract(target, pos));
	XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, forward));
	XMVECTOR up = XMVector3Cross(forward, right);

	XMStoreFloat3(&mPosition, pos);
	XMStoreFloat3(&mForward, forward);
	XMStoreFloat3(&mRight, right);
	XMStoreFloat3(&mUp, up);
}

void MyCamera::LookAt(const XMFLOAT3& pos, const XMFLOAT3& target, const XMFLOAT3& worldUp)
{
	XMVECTOR vPos = XMLoadFloat3(&pos);
	XMVECTOR vTarget = XMLoadFloat3(&target);
	XMVECTOR vWorldUp = XMLoadFloat3(&worldUp);

	LookAt(vPos, vTarget, vWorldUp);
}

void MyCamera::Strafe(float d)
{
	// mPos += d * mRight;
	XMVECTOR strafe = XMVectorReplicate(d);
	XMVECTOR right = XMLoadFloat3(&mRight);
	XMVECTOR pos = XMLoadFloat3(&mPosition);
	XMStoreFloat3(&mPosition, XMVectorMultiplyAdd(strafe, right, pos));
}

void MyCamera::Walk(float d)
{
	// mPos += d * mForward;
	XMVECTOR walk = XMVectorReplicate(d);
	XMVECTOR forward = XMLoadFloat3(&mForward);
	XMVECTOR pos = XMLoadFloat3(&mPosition);
	XMStoreFloat3(&mPosition, XMVectorMultiplyAdd(walk, forward, pos));
}

void MyCamera::Pitch(float angle)
{
	// rotate up and look vector about the right vector

	XMMATRIX rotate = XMMatrixRotationAxis(XMLoadFloat3(&mRight), angle);

	XMStoreFloat3(&mUp, XMVector3TransformNormal(XMLoadFloat3(&mUp), rotate));
	XMStoreFloat3(&mForward, XMVector3TransformNormal(XMLoadFloat3(&mForward), rotate));
}

void MyCamera::RotateY(float angle)
{
	// rotate the basis vectors about the world y-axis

	XMMATRIX rotate = XMMatrixRotationY(angle);

	XMStoreFloat3(&mRight, XMVector3TransformNormal(XMLoadFloat3(&mRight), rotate));
	XMStoreFloat3(&mUp, XMVector3TransformNormal(XMLoadFloat3(&mUp), rotate));
	XMStoreFloat3(&mForward, XMVector3TransformNormal(XMLoadFloat3(&mForward), rotate));
}

void MyCamera::UpdateViewMatrix()
{
	XMVECTOR right = XMLoadFloat3(&mRight);
	XMVECTOR up = XMLoadFloat3(&mUp);
	XMVECTOR forward = XMLoadFloat3(&mForward);
	XMVECTOR pos = XMLoadFloat3(&mPosition);

	// keep camera's axes orthogonal to each other and of unit length
	forward = XMVector3Normalize(forward);
	up = XMVector3Normalize(XMVector3Cross(forward, right));

	// up, forward already orgho-normal, so no need to normalize cross product
	right = XMVector3Cross(up, forward);

	// fill in the view matrix entries
	float x = -XMVectorGetX(XMVector3Dot(pos, right));
	float y = -XMVectorGetX(XMVector3Dot(pos, up));
	float z = -XMVectorGetX(XMVector3Dot(pos, forward));

	XMStoreFloat3(&mRight, right);
	XMStoreFloat3(&mUp, up);
	XMStoreFloat3(&mForward, forward);

	mViewMat(0, 0) = mRight.x;
	mViewMat(1, 0) = mRight.y;
	mViewMat(2, 0) = mRight.z;
	mViewMat(3, 0) = x;

	mViewMat(0, 1) = mUp.x;
	mViewMat(1, 1) = mUp.y;
	mViewMat(2, 1) = mUp.z;
	mViewMat(3, 1) = y;

	mViewMat(0, 2) = mForward.x;
	mViewMat(1, 2) = mForward.y;
	mViewMat(2, 2) = mForward.z;
	mViewMat(3, 2) = z;

	mViewMat(0, 3) = 0.0f;
	mViewMat(1, 3) = 0.0f;
	mViewMat(2, 3) = 0.0f;
	mViewMat(3, 3) = 1.0f;
}



