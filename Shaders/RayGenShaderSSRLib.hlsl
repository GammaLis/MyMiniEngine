#define HLSL
#include "../ModelViewerRaytracing.h"

#define VALIDATE_NORMAL 1

Texture2D<float>  _DepthTex	: register(t12);
Texture2D<float4> _NormalTex: register(t13);

[shader("raygeneration")]
void RayGen()
{
	uint2 dtid = DispatchRaysIndex().xy;
	float2 xy = dtid.xy + 0.5;

	// Screen position for the ray
	float2 screenPos = xy / _Dynamics.resolution * 2.0 - 1.0;

	// Invert Y for DirectX-style coordinates
	screenPos.y = -screenPos.y;

	float2 readGBufferAt = xy;

	// Read the depth and normal
	float sceneDepth = _DepthTex.Load(int3(readGBufferAt, 0));
	float4 sceneNormal = _NormalTex.Load(int3(readGBufferAt, 0));
	// !Modified
	if (sceneNormal.w != 0)
		return;

#ifdef VALIDATE_NORMAL
	// Check if normal is real and non-zero
	float lenSq = dot(sceneNormal.xyz, sceneNormal.xyz);
	if (!isfinite(lenSq) || lenSq < 1e-6)
		return;
	float3 normal = sceneNormal.xyz * rsqrt(lenSq);
#else 
	float3 normal = sceneNormal.xyz;
#endif

	// Unproject into the world position using depth
#if 0
	float4 unprojected = mul(_Dynamics.cameraToWorld, float4(screenPos, sceneDepth, 1));
#else
	float4 unprojected = mul(float4(screenPos, sceneDepth, 1), _Dynamics.cameraToWorld);
#endif
	float3 world = unprojected.xyz / unprojected.w;

	float3 primaryRayDirection = normalize(_Dynamics.worldCameraPosition.xyz - world);

	// R
	float3 direction = normalize(-primaryRayDirection + 2 * dot(primaryRayDirection, normal) * normal);
	float3 origin = world - primaryRayDirection * 0.1f; // Lift off the surface a bit

	RayDesc rayDesc =
	{
		origin, 0.0f, direction, FLT_MAX
	};

	RayPayload payload;
    payload.bSkipShading = false;
    payload.RayHitT = FLT_MAX;
	TraceRay(g_Accel, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, rayDesc, payload);

}
