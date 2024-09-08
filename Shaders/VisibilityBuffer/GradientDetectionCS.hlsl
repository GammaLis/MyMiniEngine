#define SHADER_CS

#include "VisibilityBufferCommon.hlsli"

#define GROUP_SIZE 8

/// VRS

// Image based gradients
#define VRS_IB_0	(0x0)	// no grad
#define VRS_IB_DH	(0x1)	// dir hor
#define VRS_IB_DV	(0x2)	// dir ver
#define VRS_IB_All	(0x3)	// all dirs
#define VRS_IB_Mask	(VRS_IB_All) 
#define VRS_IB_Count 4

static const float kVisibilityQualityThreshold = 1.0f;

cbuffer CBGradient	: register(b0) 
{
	float4 _RTSize;
};

Texture2D<float> _LumaTexture	: register(s0);

RWTexture2D<float> RWGradientTexture	: register(u0);

float LoadLuma(int2 coord, int2 offset = int2(0, 0))
{
	return _LumaTexture.Load(int3(coord, 0), offset).r;
}

static const int2 kOffsets[] =
{
	int2(-1,  0), int2(+1,  0), int2( 0, -1), int2( 0, +1),
	int2(-1, -1), int2(+1, -1), int2(-1, +1), int2(+1, +1)
};

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main( uint2 dtid : SV_DispatchThreadID )
{
	const uint2 RTSize = uint2(_RTSize.xy);
	if (any(dtid >= RTSize)) 
		return;
	
	int2 coord = int2(dtid);
	float lc = LoadLuma(int2(dtid));
	
	// Min luma
	float tileLuma = lc;

	float neighbors[8];
	for (int i = 0; i < 8; i++)
	{
		neighbors[i] = LoadLuma(coord, kOffsets[i]);
		tileLuma = min(tileLuma, neighbors[i]);
	}

	static const float DeviceBlack = 0.04f; // 0.0f;
	const float visibilityThreshold = kVisibilityQualityThreshold * max(tileLuma, DeviceBlack);

	// Horizontal
	float dh = max( abs(neighbors[0] - lc), abs(neighbors[1] - lc) );
	float ddh = max( abs(neighbors[4] - neighbors[5]), abs(neighbors[6] - neighbors[7]) );

	// Vertical
	float dv = max( abs(neighbors[2] - lc), abs(neighbors[3] - lc) );
	float ddv = max( abs(neighbors[4] - neighbors[6]), abs(neighbors[5] - neighbors[7]) );

	// VH direction and delta
	uint vrsRate = VRS_IB_0;
	vrsRate |= dh < visibilityThreshold ? 0 : VRS_IB_DH;
	vrsRate |= dv < visibilityThreshold ? 0 : VRS_IB_DV;

	// Merge quads - promoting through individual directions to all directions
	// TODO...
	
	RWGradientTexture[coord] = (float(vrsRate) + 0.5f) / float(VRS_IB_Count);
}
