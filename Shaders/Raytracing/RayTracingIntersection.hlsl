#ifndef RAYTRACING_INTERSECTION_INCLUDED
#define RAYTRACING_INTERSECTION_INCLUDED

// Ref: HDRP - RaytracingIntersection.hlsl

#include "RayTracingCommon.hlsl"

#ifndef ATTRIBUTES_NEED_TANGENT 
#define ATTRIBUTES_NEED_TANGENT 1
#endif 

#ifndef ATTRIBUTES_NEED_TEXCOORD0
#define ATTRIBUTES_NEED_TEXCOORD0 1
#endif

#ifndef ATTRIBUTES_NEED_TEXCOORD1
#define ATTRIBUTES_NEED_TEXCOORD1 0
#endif

#ifndef ATTRIBUTES_NEED_TEXCOORD2
#define ATTRIBUTES_NEED_TEXCOORD2 0
#endif

#ifndef ATTRIBUTES_NEED_TEXCOORD3
#define ATTRIBUTES_NEED_TEXCOORD3 0
#endif

#ifndef ATTRIBUTES_NEED_COLOR
#define ATTRIBUTES_NEED_COLOR 0
#endif

#ifndef USE_RAY_CONE_LOD
#define USE_RAY_CONE_LOD 0
#endif

// Macro that interpolate any attribute using barycentric coordinates
#define INTERPOLATE_RAYTRACING_ATTRIBUTE(A0, A1, A2, BARYCENTRIC_COORDINATES) \
	(A0 * BARYCENTRIC_COORDINATES.x + A1 * BARYCENTRIC_COORDINATES.y + A2 * BARYCENTRIC_COORDINATES.z)

// Structure to fill for intersecctions
struct FIntersectionVertex
{
	// Object space normal of the vertex
	float3 NormalOS;
	// Object space tangent of the vertex
	float3 TangentOS;
	// UV coordinates
	float4 UV0;

#if ATTRIBUTES_NEED_TEXCOORD1
	float4 UV1;
#endif
#if ATTRIBUTES_NEED_TEXCOORD2
	float4 UV2;
#endif
#if ATTRIBUTES_NEED_TEXCOORD3
	float4 UV3;
#endif
#if ATTRIBUTES_NEED_COLOR
	float4 Color;
#endif

#if USE_RAY_CONE_LOD
	// Value used for LOD sampling
	float TriangleArea;
	float UV0Area;
	float UV1Area;
	float UV2Area;
	float UV3Area;
#endif
};

// Ref: https://gamedev.stackexchange.com/questions/23743/whats-the-most-efficient-way-to-find-barycentric-coordinates
// From "Real-Time Collision Detection" by Christer Ericson
float3 BarycentricCoordinates(float3 pt, float3 v0, float3 v1, float3 v2)
{
	float3 e0 = v1 - v0;
	float3 e1 = v2 - v0;
	float3 e2 = pt - v0;
	float d00 = dot(e0, e0);
	float d01 = dot(e0, e1);
	float d11 = dot(e1, e1);
	float d20 = dot(e2, e0);
	float d21 = dot(e2, e1);
	float denom = 1.0 / (d00 * d11 - d01 * d01);
	float v = (d11 * d20 - d01 * d21) * denom;
	float w = (d00 * d21 - d01 * d20) * denom;
	float u = 1.0 - v - w;
	return float3(u, v, w);
}

uint3 Load3x16BitIndices(uint offsetBytes)
{
	const uint dwordAlignedOffset = offsetBytes & ~3;
	const uint2 four16BitIndices = _Indices.Load2(dwordAlignedOffset);

	uint3 indices;

	if (dwordAlignedOffset == offsetBytes)
	{
		indices.x = four16BitIndices.x & 0xFFFF;
		indices.y = (four16BitIndices.x >> 16) & 0xFFFF;
		indices.z = four16BitIndices.y & 0xFFFF;
	}
	else
	{
		indices.x = (four16BitIndices.x >> 16) & 0xFFFF;
		indices.y = four16BitIndices.y & 0xFFFF;
		indices.z = (four16BitIndices.y >> 16) & 0xFFFF;
	}

	return indices;
}

float3 GetPositionAttribute(uint vertexIndex, RayTraceMeshInfo info)
{
	return asfloat(_Attributes.Load3(info.PositionAttributeOffsetBytes + vertexIndex * info.AttributeStrideBytes));
}

float2 GetUVAttribute(uint vertexIndex, RayTraceMeshInfo info)
{
	return asfloat(_Attributes.Load2(info.UVAttributeOffsetBytes + vertexIndex *  info.AttributeStrideBytes));
}

float3 GetNormalAttribute(uint vertexIndex, RayTraceMeshInfo info)
{
	return asfloat(_Attributes.Load3(info.NormalAttributeOffsetBytes + vertexIndex * info.AttributeStrideBytes));
}

float3 GetTangentAttribute(uint vertexIndex, RayTraceMeshInfo info)
{
	return asfloat(_Attributes.Load3(info.TangentAttributeOffsetBytes + vertexIndex * info.AttributeStrideBytes));
}

float3 GetBitangentAttribute(uint vertexIndex, RayTraceMeshInfo info)
{
	return asfloat(_Attributes.Load3(info.BitangentAttributeOffsetBytes + vertexIndex * info.AttributeStrideBytes));
}

uint3 RayTracingFetchTriangleIndices(uint primitiveIndex, uint indexOffsetBytes)
{
	return Load3x16BitIndices(indexOffsetBytes + primitiveIndex * 3 * 2);
}

// Fetch the intersection vertex data for the target vertex
void FetchIntersectionVertex(uint vertexIndex, RayTraceMeshInfo info, out FIntersectionVertex outVertex)
{
	outVertex = (FIntersectionVertex) 0;

	outVertex.NormalOS = GetNormalAttribute(vertexIndex, info);

#if ATTRIBUTES_NEED_TANGENT
	outVertex.TangentOS = GetTangentAttribute(vertexIndex, info);
#else
	outVertex.TangentOS = 0.0;
#endif

#if ATTRIBUTES_NEED_TEXCOORD0
	outVertex.UV0 = GetUVAttribute(vertexIndex, info).xyxy;
#else
	outVertex.UV0 = 0.0;
#endif

#if ATTRIBUTES_NEED_TEXCOORD1
	outVertex.UV1 = GetUVAttribute1(vertexIndex, info);
// #else
// 	outVertex.UV1 = 0.0;
#endif

#if ATTRIBUTES_NEED_TEXCOORD2
	outVertex.UV2 = GetUVAttribute2(vertexIndex, info);
// #else
// 	outVertex.UV2 = 0.0;
#endif

#if ATTRIBUTES_NEED_TEXCOORD3
	outVertex.UV3 = GetUVAttribute3(vertexIndex, info);
// #else
// 	outVertex.UV3 = 0.0;
#endif

#if ATTRIBUTES_NEED_COLOR
    outVertex.Color = GetBitangentAttribute(vertexIndex, info).xyzz; // GetColorAttribute(vertexIndex, info);
// #else
//     outVertex.Color = 0.0;
#endif
}

void GetCurrentIntersectionVertex(RayTraceMeshInfo info, FAttributeData attributeData, out FIntersectionVertex outVertex)
{
	outVertex = (FIntersectionVertex) 0;

	// Fetch the indices of the current triangle
	uint3 triangleIndices = RayTracingFetchTriangleIndices(PrimitiveIndex(), info.IndexOffsetBytes);

	// Fetch the 3 vertices
	FIntersectionVertex v0, v1, v2;
	FetchIntersectionVertex(triangleIndices.x, info, v0);
	FetchIntersectionVertex(triangleIndices.y, info, v1);
	FetchIntersectionVertex(triangleIndices.z, info, v2);

	// Compute the full barycentric coordinates
	float3 barycentricCoordinates = float3(1.0 - attributeData.Barycentrics.x - attributeData.Barycentrics.y, 
		attributeData.Barycentrics.x, attributeData.Barycentrics.y);

	// Interpolate all the data
	outVertex.NormalOS = INTERPOLATE_RAYTRACING_ATTRIBUTE(v0.NormalOS, v1.NormalOS, v2.NormalOS, barycentricCoordinates);
	outVertex.NormalOS = SafeNormalize(outVertex.NormalOS);

#if ATTRIBUTES_NEED_TANGENT
	outVertex.TangentOS = INTERPOLATE_RAYTRACING_ATTRIBUTE(v0.TangentOS, v1.TangentOS, v2.TangentOS, barycentricCoordinates);
	outVertex.TangentOS = SafeNormalize(outVertex.TangentOS);
#else
	outVertex.TangentOS = 0.0;
#endif

#if ATTRIBUTES_NEED_TEXCOORD0
	outVertex.UV0 = INTERPOLATE_RAYTRACING_ATTRIBUTE(v0.UV0, v1.UV0, v2.UV0, barycentricCoordinates);
#else
	outVertex.UV0 = 0.0;
#endif

#if ATTRIBUTES_NEED_TEXCOORD1
	outVertex.UV1 = INTERPOLATE_RAYTRACING_ATTRIBUTE(v0.UV1, v1.UV1, v2.UV1, barycentricCoordinates);
// #else
// 	outVertex.UV1 = 0.0;
#endif

#if ATTRIBUTES_NEED_TEXCOORD2
	outVertex.UV2 = INTERPOLATE_RAYTRACING_ATTRIBUTE(v0.UV2, v1.UV2, v2.UV2, barycentricCoordinates);
// #else
// 	outVertex.UV2 = 0.0;
#endif

#if ATTRIBUTES_NEED_TEXCOORD3
	outVertex.UV3 = INTERPOLATE_RAYTRACING_ATTRIBUTE(v0.UV3, v1.UV3, v2.UV3, barycentricCoordinates);
// #else
// 	outVertex.UV3 = 0.0;
#endif

#if ATTRIBUTES_NEED_COLOR
	outVertex.Color = INTERPOLATE_RAYTRACING_ATTRIBUTE(v0.Color, v1.Color, v2.Color, barycentricCoordinates);
	outVertex.Color.xyz = SafeNormalize(outVertex.Color.xyz);
// #else
// 	outVertex.Color = 0.0;
#endif

#if USE_RAY_CONE_LOD
	// TODO...
#endif

}

// Compute the proper world space geometric normal from the intersected triangle
void GetCurrentIntersectionGeometricNormal(FAttributeData attributeData, RayTraceMeshInfo info, out float3 geomNormalWS)
{
	uint3 triangleIndices = RayTracingFetchTriangleIndices(PrimitiveIndex(), info.IndexOffsetBytes);
	float3 p0 = GetPositionAttribute(triangleIndices.x, info);
	float3 p1 = GetPositionAttribute(triangleIndices.y, info);
	float3 p2 = GetPositionAttribute(triangleIndices.z, info);

	// TODO: This follows Unity style currently
	geomNormalWS = SafeNormalize(mul(cross(p2 - p0, p1 - p0), (float3x3)ObjectToWorld3x4())); // WorldToObject3x4
}

void GetCurrentIntersectionVertexNormal(FAttributeData attributeData, RayTraceMeshInfo info, out float3 vertexNormalWS)
{
	uint3 triangleIndices = RayTracingFetchTriangleIndices(PrimitiveIndex(), info.IndexOffsetBytes);
	float3 normal0 = GetNormalAttribute(triangleIndices.x, info);
	float3 normal1 = GetNormalAttribute(triangleIndices.y, info);
	float3 normal2 = GetNormalAttribute(triangleIndices.z, info);

	float3 barycentricCoordinates = float3(1.0 - attributeData.Barycentrics.x - attributeData.Barycentrics.y, 
		attributeData.Barycentrics.x, attributeData.Barycentrics.y);
	vertexNormalWS = INTERPOLATE_RAYTRACING_ATTRIBUTE(normal0, normal1, normal2, barycentricCoordinates);
	vertexNormalWS = SafeNormalize(mul(vertexNormalWS, (float3x3)WorldToObject3x4()));
}

#endif // RAYTRACING_INTERSECTION_INCLUDED
