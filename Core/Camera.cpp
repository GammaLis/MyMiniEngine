//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard 
//

#include "pch.h"
#include "Camera.h"
#include <cmath>

using namespace Math;

void BaseCamera::SetLookDirection( Vector3 forward, Vector3 up )
{
    // Given, but ensure normalization
    Scalar forwardLenSq = LengthSquare(forward);
    forward = Select(forward * RecipSqrt(forwardLenSq), -Vector3(kZUnitVector), forwardLenSq < Scalar(0.000001f));

    // Deduce a valid, orthogonal right vector
    Vector3 right = Cross(forward, up);
    Scalar rightLenSq = LengthSquare(right);
    right = Select(right * RecipSqrt(rightLenSq), Quaternion(Vector3(kYUnitVector), -XM_PIDIV2) * forward, rightLenSq < Scalar(0.000001f));

    // Compute actual up vector
    up = Cross(right, forward);

    // Finish constructing basis
    m_Basis = Matrix3(right, up, -forward);
    m_CameraToWorld.SetRotation(Quaternion(m_Basis));
}

void BaseCamera::Update()
{
    m_PreviousViewProjMatrix = m_ViewProjMatrix;

    m_ViewMatrix = Matrix4(~m_CameraToWorld);
    m_ViewProjMatrix = m_ProjMatrix * m_ViewMatrix;
    m_ReprojectMatrix = m_PreviousViewProjMatrix * Invert(GetViewProjMatrix());

    m_FrustumVS = Frustum( m_ProjMatrix );
    m_FrustumWS = m_CameraToWorld * m_FrustumVS;
}


void Camera::UpdateProjMatrix( void )
{
    float Y = 1.0f / std::tanf( m_VerticalFOV * 0.5f );
    float X = Y * m_AspectRatio;

    float Q1, Q2;

    // ReverseZ puts far plane at Z=0 and near plane at Z=1.  This is never a bad idea, and it's
    // actually a great idea with F32 depth buffers to redistribute precision more evenly across
    // the entire range.  It requires clearing Z to 0.0f and using a GREATER variant depth test.
    // Some care must also be done to properly reconstruct linear W in a pixel shader from hyperbolic Z.
    if (m_ReverseZ)
    {
        Q1 = m_NearClip / (m_FarClip - m_NearClip);
        Q2 = Q1 * m_FarClip;
    }
    else
    {
        Q1 = m_FarClip / (m_NearClip - m_FarClip);
        Q2 = Q1 * m_NearClip;
    }

    SetProjMatrix( Matrix4(
        Vector4( X, 0.0f, 0.0f, 0.0f ),
        Vector4( 0.0f, Y, 0.0f, 0.0f ),
        Vector4( 0.0f, 0.0f, Q1, -1.0f ),
        Vector4( 0.0f, 0.0f, Q2, 0.0f )
        ) );
}

Vector4 Math::CreateInvDeviceZToWorldZTransform(const Matrix4 &ProjMatrix)
{
    // The perspective depth projection comes from the the following projection matrix:
    //
    // | 1  0  0  0 |
    // | 0  1  0  0 |
    // | 0  0  A  1 |
    // | 0  0  B  0 |
    //
    // Z' = (Z * A + B) / Z
    // Z' = A + B / Z
    //
    // So to get Z from Z' is just:
    // Z = B / (Z' - A)
    //
    // Note a reversed Z projection matrix will have A=0.
    //
    // Done in shader as:
    // Z = 1 / (Z' * C1 - C2)   --- Where C1 = 1/B, C2 = A/B
    //
    // -Z ==> Z' = (Z * A + B) / -Z; Z' = -A - B/Z
    // ==> -Z = B / (Z'+ A)
    // ==> -Z = 1 / (Z' * 1/B + A/B)

    float depthMul = ProjMatrix.GetZ().GetZ();
    float depthAdd = ProjMatrix.GetW().GetZ();
    if (depthAdd == 0.0f)
    {
	    // Avoid dividing by 0 in this case
        depthAdd = 0.00000001f;
    }

    // perspective
    // SceneDepth = 1.0f / (DeviceZ / ProjMatrix.M[3][2] - ProjMatrix.M[2][2] / ProjMatrix.M[3][2])

    // ortho
    // SceneDepth = DeviceZ / ProjMatrix.M[2][2] - ProjMatrix.M[3][2] / ProjMatrix.M[2][2];

    // combined equation in shader to handle either
    // SceneDepth = DeviceZ * View.InvDeviceZToWorldZTransform[0] + View.InvDeviceZToWorldZTransform[1] + 1.0f / (DeviceZ * View.InvDeviceZToWorldZTransform[2] - View.InvDeviceZToWorldZTransform[3]);

    // therefore perspective needs
    // View.InvDeviceZToWorldZTransform[0] = 0.0f
    // View.InvDeviceZToWorldZTransform[1] = 0.0f
    // View.InvDeviceZToWorldZTransform[2] = 1.0f / ProjMatrix.M[3][2]
    // View.InvDeviceZToWorldZTransform[3] = ProjMatrix.M[2][2] / ProjMatrix.M[3][2]

    // and ortho needs
    // View.InvDeviceZToWorldZTransform[0] = 1.0f / ProjMatrix.M[2][2]
    // View.InvDeviceZToWorldZTransform[1] = -ProjMatrix.M[3][2] / ProjMatrix.M[2][2] + 1.0f
    // View.InvDeviceZToWorldZTransform[2] = 0.0f
    // View.InvDeviceZToWorldZTransform[3] = 1.0f
    
    if (bool bIsPerspectiveProjection = ProjMatrix.GetW().GetW() < 1.0f)
    {
        float subtractValue = depthMul / depthAdd;

        // Subtract a tiny number to avoid divide by 0 errors in the shader when a very far distance is decided from the depth buffer.
        // This fixes fog not being applied to the black background in the editor.
        subtractValue -= 0.00000001f;

        return { 0.0f, 0.0f, 1.0f / depthAdd, subtractValue };
    }
    else
    {
        return { 1.0f / depthMul, -depthAdd / depthMul + 1.0f, 0.0f, 1.0f };
    }
}
