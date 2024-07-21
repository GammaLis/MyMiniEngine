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
#include "Frustum.h"
#include "Camera.h"

using namespace Math;

void Frustum::ConstructPerspectiveFrustum( float HTan, float VTan, float NearClip, float FarClip )
{
    const float NearX = HTan * NearClip;
    const float NearY = VTan * NearClip;
    const float FarX  = HTan * FarClip;
    const float FarY  = VTan * FarClip;

    // Define the frustum corners
    m_FrustumCorners[ kNearLowerLeft  ] = Vector3(-NearX, -NearY, -NearClip);    // Near lower left
    m_FrustumCorners[ kNearUpperLeft  ] = Vector3(-NearX,  NearY, -NearClip);    // Near upper left
    m_FrustumCorners[ kNearLowerRight ] = Vector3( NearX, -NearY, -NearClip);    // Near lower right
    m_FrustumCorners[ kNearUpperRight ] = Vector3( NearX,  NearY, -NearClip);    // Near upper right
    m_FrustumCorners[ kFarLowerLeft   ] = Vector3( -FarX,  -FarY,  -FarClip);    // Far lower left
    m_FrustumCorners[ kFarUpperLeft   ] = Vector3( -FarX,   FarY,  -FarClip);    // Far upper left
    m_FrustumCorners[ kFarLowerRight  ] = Vector3(  FarX,  -FarY,  -FarClip);    // Far lower right
    m_FrustumCorners[ kFarUpperRight  ] = Vector3(  FarX,   FarY,  -FarClip);    // Far upper right

    const float NHx = RecipSqrt( 1.0f + HTan * HTan );
    const float NHz = -NHx * HTan;
    const float NVy = RecipSqrt( 1.0f + VTan * VTan );
    const float NVz = -NVy * VTan;

    // Define the bounding planes
    m_FrustumPlanes[kNearPlane  ] = BoundingPlane( 0.0f, 0.0f, -1.0f, -NearClip );
    m_FrustumPlanes[kFarPlane   ] = BoundingPlane( 0.0f, 0.0f,  1.0f,   FarClip );
    m_FrustumPlanes[kLeftPlane  ] = BoundingPlane(  NHx, 0.0f,   NHz,      0.0f );
    m_FrustumPlanes[kRightPlane ] = BoundingPlane( -NHx, 0.0f,   NHz,      0.0f );
    m_FrustumPlanes[kTopPlane   ] = BoundingPlane( 0.0f, -NVy,   NVz,      0.0f );
    m_FrustumPlanes[kBottomPlane] = BoundingPlane( 0.0f,  NVy,   NVz,      0.0f );
}

void Frustum::ConstructOrthographicFrustum( float Left, float Right, float Top, float Bottom, float Front, float Back )
{
    // Define the frustum corners
    m_FrustumCorners[ kNearLowerLeft  ] = Vector3(Left,   Bottom,   -Front);    // Near lower left
    m_FrustumCorners[ kNearUpperLeft  ] = Vector3(Left,   Top,      -Front);    // Near upper left
    m_FrustumCorners[ kNearLowerRight ] = Vector3(Right,  Bottom,   -Front);    // Near lower right
    m_FrustumCorners[ kNearUpperRight ] = Vector3(Right,  Top,      -Front);    // Near upper right
    m_FrustumCorners[ kFarLowerLeft   ] = Vector3(Left,   Bottom,   -Back );    // Far lower left
    m_FrustumCorners[ kFarUpperLeft   ] = Vector3(Left,   Top,      -Back );    // Far upper left
    m_FrustumCorners[ kFarLowerRight  ] = Vector3(Right,  Bottom,   -Back );    // Far lower right
    m_FrustumCorners[ kFarUpperRight  ] = Vector3(Right,  Top,      -Back );    // Far upper right

    // Define the bounding planes
    m_FrustumPlanes[kNearPlane  ] = BoundingPlane(  0.0f,  0.0f, -1.0f, -Front );
    m_FrustumPlanes[kFarPlane   ] = BoundingPlane(  0.0f,  0.0f,  1.0f,   Back );
    m_FrustumPlanes[kLeftPlane  ] = BoundingPlane(  1.0f,  0.0f,  0.0f,  -Left );
    m_FrustumPlanes[kRightPlane ] = BoundingPlane( -1.0f,  0.0f,  0.0f,  Right );
    m_FrustumPlanes[kTopPlane   ] = BoundingPlane(  0.0f, -1.0f,  0.0f,    Top ); // FIX: Bottom -> Top
    m_FrustumPlanes[kBottomPlane] = BoundingPlane(  0.0f,  1.0f,  0.0f,-Bottom ); // FIX: Top -> Bottom
}

Frustum::Frustum( const Matrix4& ProjMat )
{
    const float* ProjMatF = (const float*)&ProjMat;

    const float RcpXX = 1.0f / ProjMatF[ 0];
    const float RcpYY = 1.0f / ProjMatF[ 5];
    const float RcpZZ = 1.0f / ProjMatF[10];

    // Identify if the projection is perspective or orthographic by looking at the 4th row.
    if (ProjMatF[3] == 0.0f && ProjMatF[7] == 0.0f && ProjMatF[11] == 0.0f && ProjMatF[15] == 1.0f)
    {
        // Orthographic
        float Left      = (-1.0f - ProjMatF[12]) * RcpXX;
        float Right     = ( 1.0f - ProjMatF[12]) * RcpXX;
        float Top       = ( 1.0f - ProjMatF[13]) * RcpYY;
        float Bottom    = (-1.0f - ProjMatF[13]) * RcpYY;
        float Front     = ( 0.0f - ProjMatF[14]) * RcpZZ;
        float Back      = ( 1.0f - ProjMatF[14]) * RcpZZ;

        // Check for reverse Z here.  The bounding planes need to point into the frustum.
        if (Front < Back)
            ConstructOrthographicFrustum( Left, Right, Top, Bottom, Front, Back );
        else
            ConstructOrthographicFrustum( Left, Right, Top, Bottom, Back, Front );
    }
    else
    {
        // Perspective
        float NearClip, FarClip;

        if (RcpZZ > 0.0f)    // Reverse Z
        {
            FarClip = ProjMatF[14] * RcpZZ;
            NearClip = FarClip / (RcpZZ + 1.0f);
        }
        else
        {
            NearClip = ProjMatF[14] * RcpZZ;
            FarClip = NearClip / (RcpZZ + 1.0f);
        }

        ConstructPerspectiveFrustum( RcpXX, RcpYY, NearClip, FarClip );
    }
}

bool Frustum::IntersectSphere(BoundingSphere sphere) const
{
    float radius = sphere.GetRadius();
    for (int i = 0; i < 6; ++i)
    {
        if (m_FrustumPlanes[i].DistanceFromPoint(sphere.GetCenter()) + radius < 0.0f)
            return false;
    }
    return true;
}

bool Frustum::IntersectBoundingBox(const Vector3 minBound, const Vector3 maxBound) const
{
    for (int i = 0; i < 6; ++i)
    {
        BoundingPlane p = m_FrustumPlanes[i];
        Vector3 farCorner = Select(minBound, maxBound, p.GetNormal() > Vector3(kZero));
        if (p.DistanceFromPoint(farCorner) < 0.0f)
            return false;
    }

    return true;
}

namespace Math
{

Frustum operator* (const OrthogonalTransform& xform, const Frustum& frustum)
{
    Frustum result;

    for (int i = 0; i < 8; ++i)
        result.m_FrustumCorners[i] = xform * frustum.m_FrustumCorners[i];

for (int i = 0; i < 6; ++i)
        result.m_FrustumPlanes[i] = xform * frustum.m_FrustumPlanes[i];

    return result;
}

Frustum operator* (const AffineTransform& xform, const Frustum& frustum)
{
    Frustum result;

    for (int i = 0; i < 8; ++i)
        result.m_FrustumCorners[i] = xform * frustum.m_FrustumCorners[i];

    Matrix4 XForm = Transpose(Invert(Matrix4(xform)));

    for (int i = 0; i < 6; ++i)
        result.m_FrustumPlanes[i] = BoundingPlane(XForm * Vector4(frustum.m_FrustumPlanes[i]));

    return result;
}

inline Frustum operator* (const Matrix4& mtx, const Frustum& frustum)
{
    Frustum result;

    for (int i = 0; i < 8; ++i)
        result.m_FrustumCorners[i] = Vector3(mtx * frustum.m_FrustumCorners[i]);

    Matrix4 XForm = Transpose(Invert(mtx));

    for (int i = 0; i < 6; ++i)
        result.m_FrustumPlanes[i] = BoundingPlane(XForm * Vector4(frustum.m_FrustumPlanes[i]));

    return result;
}

}
