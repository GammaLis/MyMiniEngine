#define HLSL
#include "../../ModelViewerRaytracing.h"

Texture2D<float> _DepthTex :	register(t12);

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

	// Read depth
	float sceneDepth = _DepthTex.Load(int3(readGBufferAt, 0));

	// Unprojected into the world position using depth
#if 0
	float4 unprojected = mul(_Dynamics.cameraToWorld, float4(screenPos, sceneDepth, 1));
#else
	float4 unprojected = mul(float4(screenPos, sceneDepth, 1), _Dynamics.cameraToWorld);
#endif
	float3 world = unprojected.xyz / unprojected.w;

	float3 direction = _SunDirection.xyz;
	float3 origin = world;

	RayDesc rayDesc =
	{
		origin, 0.1f, direction, FLT_MAX
	};
	RayPayload payload = { true, FLT_MAX };
	TraceRay(g_Accel, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, ~0, 0, 1, 0, rayDesc, payload);

	if (payload.RayHitT < FLT_MAX)
	{
		g_ScreenOutput[dtid] = float4(0, 0, 0, 1);
	}
	else
	{
		g_ScreenOutput[dtid] = float4(1, 1, 1, 1);
	}
}
