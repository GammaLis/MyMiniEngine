#define HLSL
#include "../../ModelViewerRaytracing.h"

[shader("raygeneration")]
void RayGen()
{
	float3 origin, direction;
	GenerateCameraRay(DispatchRaysIndex().xy, origin, direction);

	RayDesc rayDesc = 
	{
		origin, 0.0f, direction, FLT_MAX
	};

	RayPayload payload;
	payload.bSkipShading = false;
	payload.RayHitT = FLT_MAX;
	TraceRay(g_Accel, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 1, 0, rayDesc, payload);
}
