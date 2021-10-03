#include "TemporalRS.hlsli"
// #include "ShaderUtility.hlsli"

#define BORDER_SIZE 	1
#define GROUP_SIZE_X 	8
#define GROUP_SIZE_Y 	8
#define GROUP_SIZE 	(GROUP_SIZE_X * GROUP_SIZE_Y)
#define TILE_SIZE_X (GROUP_SIZE_X + 2 * BORDER_SIZE)
#define TILE_SIZE_Y (GROUP_SIZE_Y + 2 * BORDER_SIZE)
#define TILE_PIXEL_COUNT (TILE_SIZE_X * TILE_SIZE_Y)

cbuffer InlineConstants	: register(b0)
{
	float _WA, _WB, _IsPreMultipliedAlpha;	// IntelTAA not PreMultipliedAlpha
};

Texture2D<float4> _TemproalColor: register(t0);

RWTexture2D<float3> OutColor	: register(u0);

SamplerState s_LinearSampler	: register(s0);
SamplerState s_PointSampler		: register(s1);

groupshared float sh_R[TILE_PIXEL_COUNT];
groupshared float sh_G[TILE_PIXEL_COUNT];
groupshared float sh_B[TILE_PIXEL_COUNT];
groupshared float sh_W[TILE_PIXEL_COUNT];	// weight

float3 LoadSample(uint ldsIndex)
{
	return float3(sh_R[ldsIndex], sh_G[ldsIndex], sh_B[ldsIndex]);
}

// Intel TAA
float LuminanceRec709( float3 inRGB )
{
    return dot( inRGB, float3( 0.2126f, 0.7152f, 0.0722f ) );
}

float3 InverseReinhard( float3 inRGB )
{
    return inRGB / ( 1.f - LuminanceRec709( inRGB ) );
}

[RootSignature(Temporal_RootSig)]
[numthreads(GROUP_SIZE_X, GROUP_SIZE_Y, 1)]
void main( 
	uint3 dispatchThreadId 	: SV_DispatchThreadID,
	uint3 groupId 			: SV_GroupID,
	uint3 groupThreadId		: SV_GroupThreadID,
	uint groupThreadIndex 	: SV_GroupIndex)
{
	int2 topleft = groupId.xy * uint2(GROUP_SIZE_X, GROUP_SIZE_Y) - BORDER_SIZE;
	for (uint i = groupThreadIndex; i < TILE_PIXEL_COUNT; i += GROUP_SIZE)
	{
		int2 st = topleft + int2(i % TILE_SIZE_X, i / TILE_SIZE_X);
		float4 temporalColor = _TemproalColor[st];
		const float3 colorNoAlpha = _IsPreMultipliedAlpha > 0 ? temporalColor.rgb / max(temporalColor.w, 1e-6) : // premultiplied color
			InverseReinhard(temporalColor.rgb);
		temporalColor.rgb = log2(1.0 + colorNoAlpha);
		sh_R[i] = temporalColor.r;
		sh_G[i] = temporalColor.g;
		sh_B[i] = temporalColor.b;
		sh_W[i] = temporalColor.w;
	}

	GroupMemoryBarrierWithGroupSync();

	uint ldsIndex = (groupThreadId.x + BORDER_SIZE) + (groupThreadId.y + BORDER_SIZE) * TILE_SIZE_X;

	float3 center = LoadSample(ldsIndex);
	float3 neighbors = LoadSample(ldsIndex - 1) +	// left
		LoadSample(ldsIndex + 1) +					// right
		LoadSample(ldsIndex - TILE_SIZE_X) +		// up 
		LoadSample(ldsIndex + TILE_SIZE_X);			// down

	/**
	 * 	if the temporal weight is less than 0.5, it might be a brand new pixel.
	 * Brand new pixels have not been antialiased at all and can be jarring.
	 * Here we change the weights to actually blur rather than sharpen those pixels
	 */
	float temporalWeight = sh_W[ldsIndex];
	float centerWeight = temporalWeight <= 0.5 ? 0.5 : _WA;
	float lateralWeight = temporalWeight <= 0.5 ? 0.5 : _WB;

	OutColor[dispatchThreadId.xy] = exp2(max(0, _WA * center - _WB * neighbors)) - 1.0;
}
