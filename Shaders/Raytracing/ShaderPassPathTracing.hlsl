#ifndef SHADERPASS_PATHTRACING_INCLUDED
#define SHADERPASS_PATHTRACING_INCLUDED

#include "RayTracingIntersection.hlsl"

// Generic function that handles one scattering event (a vertex along the full path), can be either:
// - surface scattering
// - volume scattering
[shader("closesthit")]
void Hit(inout FHitInfo paylod, FAttributeData attributeData )
{
	// Always set the new t value
	paylod.T = RayTCurrent();
	const float3 posWS = WorldRayOrigin() + paylod.T * WorldRayDirection();

	// At closest hit, we first load material and geometry ID packed into InstanceID
	uint geometryID = _MaterialID;
	const RayTraceMeshInfo meshInfo = _MeshInfo[geometryID];
#if 0
	uint materialID;
	UnpackInstanceID(InstanceID(), materialID, geometryID);
#endif

	// Read hit point properties (position, normal, UVs, ...) from vertex buffer
	const float3 barycentrics = float3(1.0f - attributeData.Barycentrics.x - attributeData.Barycentrics.y,
		attributeData.Barycentrics.x, attributeData.Barycentrics.y);

	FIntersectionVertex vertex;
	GetCurrentIntersectionVertex(meshInfo, attributeData, vertex);

	// float3 normalWS = mul(vertex.NormalOS, (float3x3)ObjectToWorld3x4());
	
	float3 geomNormalWS = vertex.NormalOS;
	GetCurrentIntersectionGeometricNormal(attributeData, meshInfo, geomNormalWS);

	// Load material properties at the hit point
	const float3 diffuseColor = _LocalTexture.SampleLevel(_S0, vertex.UV0.xy, 0).rgb;
	float3 N = vertex.NormalOS;
	{
		float3 normalSample = _LocalNormal.SampleLevel(_S0, vertex.UV0.xy, 0).xyz * 2.0 - 1.0;
		normalSample = normalize(normalSample);
		float3x3 tbn = float3x3(vertex.TangentOS, vertex.Color.xyz, vertex.NormalOS); // bitangent in color var atm
		N = normalize(mul(normalSample, tbn));
	}

	// Encode hit point properties into payload
	paylod.encodedNormals = float4(EncodeNormalOctahedron(geomNormalWS), EncodeNormalOctahedron(N));
	paylod.hitPosition = posWS;
	paylod.uvs = f32tof16(vertex.UV0.x) | (f32tof16(vertex.UV0.y) << 16);

#if 0
	float shadow = 1.0f;
	if (_UseShadowRays)
	{
		float3 shadowDirection = _SunDirection.xyz;
		float3 shadowOrigin = posWS;
		RayDesc shadowRay =
		{
			shadowOrigin, 0.1f, shadowDirection, FLT_MAX
		};

		FHitInfo shadowPayload = (FHitInfo)0;
		shadowPayload.T = 1;
		TraceRay(
			g_Accel,
			RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
			0xFF,
			0, 1, 0,
			shadowRay,
			shadowPayload
		);

		if (shadowPayload.T > 0 && shadowPayload.T < FLT_MAX)
			shadow = 0.0;
	}
#endif

	paylod.color = diffuseColor;

	// TODO...
}

[shader("anyhit")]
void AnyHit(inout FHitInfo paylod, FAttributeData attributeData)
{
	uint geometryID = _MaterialID;
	const RayTraceMeshInfo meshInfo = _MeshInfo[geometryID];

	// Read hit point properties (position, normal, UVs, ...) from vertex buffer
	const float3 barycentrics = float3(1.0f - attributeData.Barycentrics.x - attributeData.Barycentrics.y,
		attributeData.Barycentrics.x, attributeData.Barycentrics.y);

	// Fetch the indices of the current triangle
	uint3 triangleIndices = RayTracingFetchTriangleIndices(PrimitiveIndex(), meshInfo.IndexOffsetBytes);

	float2 uv00 = GetUVAttribute(triangleIndices.x, meshInfo);
	float2 uv01 = GetUVAttribute(triangleIndices.y, meshInfo);
	float2 uv02 = GetUVAttribute(triangleIndices.y, meshInfo);
	float2 uv0 = INTERPOLATE_RAYTRACING_ATTRIBUTE(uv00, uv01, uv02, barycentrics);

	// Load material properties at the hit point
	const float alpha = _LocalTexture.SampleLevel(_S0, uv0, 0).a;
	if (alpha < 0.5f)
	{
		IgnoreHit();
	}
}

#endif

/**
 * DirectX-Specs
 * https://microsoft.github.io/DirectX-Specs/d3d/Raytracing
 * 
 * >> Primitive/object system values
 * These system values are available once a primitive has been selected for intersection. They enable identifying
 * what is being intersected by the ray, the object space ray origin and direction, and the transformation between
 * object and world space.
 * > InstanceIndex
 * 	The autogenerated index of the current instance in the top-level structure
 * > InstanceID
 * 	The use-provided InstanceID on the BLAS instance within the TLAS
 * > GeometryIndex
 * 	The autogenerated index of the current geometry in the BLAS. The MultiplierForGeometryContributionToHitGroupIndex 
 * parameter to TraceRay() can be set to 0 to stop the geometry index from contribution to shader table indexing
 * if the shader just wants to rely on GeometryIndex() to distinguish geometries.
 * > PrimitiveIndex
 * 	The autogenerated index of the primitive within the geometry insdie the BLAS instance.
 */

/**
 * Any hit shader
 * 	A shader that is invoked when ray intersections are not opaque.
 * 	Any hit shaders must declare a payload parameter followed by an attributes parameter. Each of these parameters
 * must be a user-defined structure type matching types used for *TraceRay* and *ReportHit* respectively,
 * or the `Intersection Attributes Structure` when fixed function triangle intersection is used.
 * 	Any hit shaders may perform the following kinds of operations:
 * * Read and modify the ray payload: (inout payload_t rayPayload)
 * * Read the intersection attributes: (in attr_t attributes)
 * * Call *AcceptHitAndEndSearch*, which accepts the current hit, ends the `any hit shader`, ends the `intersection shader`
 * if one is present, and executes the *closest hit shader* on the closest hit so far if it is active.
 * * Call *IgnoreHit*, which ends the any hit shader and tells the system to continue searching for this, including
 * returning control to an intersection shader, if it is currently executing, returning false from the *ReportHit* call site.
 * * Return without calling either of these intrinsics, which accepts the current hit and tells the system to continue 
 * searching for hits, including returning control to the intersection shader if there is one, returning true at 
 * the *ReportHit* call site to indicate that the hit was accepted.
 *
 * Even if any hit shader invocation is ended by *IgnoreHit* or *AcceptHitAndEndSearch*, any modifications made to 
 * the ray payload so far must still be retained.
 *
 * !! NOTE::The any-hit shader will be ignored for acceleration structures created with the 
 * **D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE** flag.
 * There is no guarantee on the order that any-hit shaders are executed when multiple intersections are found.
 * This means the first invocation may not be the closest intersection to the origin, and the number of times 
 * the shader is invoked for a specific ray may vary!
 */
