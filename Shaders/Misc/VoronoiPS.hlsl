#include "VoronoiRS.hlsli"

cbuffer CBConstants	: register(b0)
{
	uint2 _Size;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	float2 pos : POSITION;
	float2 center	: CENTROID;
	float3 color 	: COLOR;
};

[RootSignature(RootSig_VoronoiTexture)]
float4 main(VSOutput i, out float depth : SV_Depth) : SV_TARGET
{
	// depth = length(i.pos - i.center);
	depth = length(i.pos - i.center) / max(_Size.x, _Size.y);	// depth [0, 1], clearValue = 1.0f, ZTest = EqualLess

	float4 color = 0.0f;
	color.rgb = i.color;

	if (depth < 0.01f)
	{
		color = 1.0f;
	}
	
	return color;
}

/**
 *	Pixel shaders can only write to parameters with the SV_Depth and SV_Target 
 *system-value semantics.
 * 
 */

/**
 * 	GPU Accelerated Voronoi Textures and Filters
	https://weigert.vsos.ethz.ch/2020/08/01/gpu-accelerated-voronoi/
 */