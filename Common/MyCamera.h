//***************************************************************************************
// Camera.h by Frank Luna (C) 2011 All Rights Reserved.
//   
// Simple first person style camera class that lets the viewer explore the 3D scene.
//   -It keeps track of the camera coordinate system relative to the world space
//    so that the view matrix can be constructed.  
//   -It keeps track of the viewing frustum of the camera so that the projection
//    matrix can be obtained.
//***************************************************************************************
#pragma once

#include "pch.h"

using namespace DirectX;

namespace MyDirectX
{
	namespace Camera
	{
		class MyCamera
		{
		public:
			MyCamera(XMFLOAT3 position = XMFLOAT3(0.0f, 3.0f, -6.0f), XMFLOAT3 target = XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT3 worldUp = XMFLOAT3(0.0f, 1.0f, 0.0f),
				float fovY = XM_PIDIV4, float aspect = 1.0f, float nearClip = 0.1f, float farClip = 100.0f);

			MyCamera(float fovY, float aspect, float nearClip = 0.1f, float farClip = 100.0f);
			~MyCamera();

			// get/set world camera position
			XMVECTOR GetPositionXM() const
			{
				return XMLoadFloat3(&mPosition);
			}
			XMFLOAT3 GetPosition() const
			{
				return mPosition;
			}
			void SetPosition(float x, float y, float z)
			{
				mPosition = XMFLOAT3(x, y, z);
			}
			void SetPosition(const XMFLOAT3& pos)
			{
				mPosition = pos;
			}

			// get camera basis vectors
			XMVECTOR GetRightXM() const
			{
				return XMLoadFloat3(&mRight);
			}
			XMFLOAT3 GetRight() const
			{
				return mRight;
			}
			XMVECTOR GetUpXM() const
			{
				return XMLoadFloat3(&mUp);
			}
			XMFLOAT3 GetUp() const
			{
				return mUp;
			}
			XMVECTOR GetForwardXM() const
			{
				return XMLoadFloat3(&mForward);
			}
			XMFLOAT3 GetForward() const
			{
				return mForward;
			}

			// get frustum properties
			float GetNearClipPlane() const
			{
				return mNearClipPlane;
			}
			float GetFarClipPlane() const
			{
				return mFarClipPlane;
			}
			float GetAspect() const
			{
				return mAspect;
			}
			float GetFovY() const
			{
				return mFovY;
			}
			float GetFovX() const
			{
				float halfWidth = 0.5f * GetNearWindowWidth();
				return 2.0f* atan(halfWidth / mNearClipPlane);
			}

			// get near and far plane dimensions in view space coordinates
			float GetNearWindowWidth() const
			{
				return mAspect * mNearWindowHeight;
			}
			float GetNearWindowHeight() const
			{
				return mNearWindowHeight;
			}
			float GetFarWindowWidth() const
			{
				return mAspect * mFarWindowHeight;
			}
			float GetFarWindowHeight() const
			{
				return mFarWindowHeight;
			}

			// set frustum
			void SetFrustum(float fovY, float aspect, float nearClip, float farClip);

			// define camera space via LookAt paramters
			void LookAt(FXMVECTOR pos, FXMVECTOR target, FXMVECTOR worldUp);
			void LookAt(const XMFLOAT3 & pos, const XMFLOAT3 & target, const XMFLOAT3 & worldUp);

			// get view/proj matrices
			XMMATRIX GetViewMat() const
			{
				return XMLoadFloat4x4(&mViewMat);
			}
			XMMATRIX GetProjMat() const
			{
				return XMLoadFloat4x4(&mProjMat);
			}
			XMMATRIX GetViewProjMat() const
			{
				return XMMatrixMultiply(GetViewMat(), GetProjMat());
			}

			// strafe/walk the camera a distance d
			void Strafe(float d);
			void Walk(float d);

			// rotate the camera
			void Pitch(float angle);
			void RotateY(float angle);

			// after modifying camera position/orientation, call to rebuild the view matrx
			void UpdateViewMatrix();

		private:

			// camera coordinate system with coordinates relative to world space
			XMFLOAT3 mPosition;
			XMFLOAT3 mRight;
			XMFLOAT3 mUp;
			XMFLOAT3 mForward;

			// cache frustum properties
			float mNearClipPlane;
			float mFarClipPlane;
			float mAspect;
			float mFovY;
			float mNearWindowHeight;
			float mFarWindowHeight;

			// cache view/proj matrices
			XMFLOAT4X4 mViewMat;
			XMFLOAT4X4 mProjMat;

		};
	}

}

