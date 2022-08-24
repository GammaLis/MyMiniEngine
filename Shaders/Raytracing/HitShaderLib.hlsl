#define HLSL
#include "../../ModelViewerRaytracing.h"

[shader("closesthit")]
void Hit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attrib)
{
	payload.RayHitT = RayTCurrent();
	if (!payload.bSkipShading)
	{
		g_ScreenOutput[DispatchRaysIndex().xy] = float4(attrib.barycentrics, 1.0f - attrib.barycentrics.x - attrib.barycentrics.y, 1.0f);
	}
}
