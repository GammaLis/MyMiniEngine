#include "VoronoiRS.hlsli"

Texture2D<float4> _TexVoronoi	: register(t0);

RWTexture2D<float4> Output	: register(u0);

[numthreads(8, 8, 1)]
void main( uint3 dtid : SV_DispatchThreadID )
{
	float4 c = _TexVoronoi[dtid.xy];
	uint2 coord = UnpackCoord(c);
	float len = length(float2(dtid.xy) - float2(coord.xy));

	float4 color = 0.0f;
	if (len < 5.0f)
	{
		color = 1.0f;
	}
	else
	{
		color.rgb = CoordColor(coord);
	}
	Output[dtid.xy] = color;
}
