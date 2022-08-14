#define HLSL
#include "../ModelViewerRaytracing.h"

[shader("miss")]
void Miss(inout RayPayload payload)
{
	if (!payload.bSkipShading)
	{
		g_ScreenOutput[DispatchRaysIndex().xy] = float4(0, 0, 0, 1);
	}
}
