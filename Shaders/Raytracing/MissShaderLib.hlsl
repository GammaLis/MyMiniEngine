#define HLSL
#include "../../Game/ModelViewerRaytracing.h"

[shader("miss")]
void Miss(inout RayPayload payload)
{
	if (!payload.bSkipShading && !_IsReflection)
	{
		g_ScreenOutput[DispatchRaysIndex().xy] = _Dynamics.backgroundColor;
	}
}
