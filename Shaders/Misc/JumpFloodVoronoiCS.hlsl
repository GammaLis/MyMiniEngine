#include "VoronoiRS.hlsli"

cbuffer CSConstants	: register(b0)
{
	uint2 _Size;
	uint _StepSize;
};

Texture2D<float4> _SourceTex	: register(t0);

RWTexture2D<float4>	Output		: register(u0);

SamplerState s_PointClampSampler: register(s0);

[RootSignature(RootSig_VoronoiTexture)]
[numthreads(8, 8, 1)]
void main( uint3 dtid : SV_DispatchThreadID )
{
	int2 tid = dtid.xy;

	float4 color = _SourceTex[tid];
	uint2 centerCoord = UnpackCoord(color);

	float minLen = 9999.0f;
	uint2 minCoord = uint2(0, 0);
	for (int i = -1; i <= 1; ++i)
	{
		for (int j = -1; j <= 1; ++j)
		{
			int2 neighbor = tid + int2(i, j) * _StepSize;
			if (neighbor.x >= 0 && neighbor.x < (int) _Size.x &&
				neighbor.y >= 0 && neighbor.y < (int) _Size.y)
			{
				float4 c = _SourceTex[neighbor];
				uint2 coord = UnpackCoord(c);
				float len = length(float2(coord) - float2(tid));
				if (coord.x != 0 && coord.y != 0 && len < minLen)
				{
					minLen = len;
					minCoord = coord;
				}
			}
		}
	}
	Output[tid] = PackCoord(minCoord);

}

/**
 * 	https://blog.demofox.org/2016/02/29/fast-voronoi-diagrams-and-distance-dield-textures-on-the-gpu-with-the-jump-flooding-algorithm/
 * 	https://www.shadertoy.com/view/Mdy3DK
 * 	https://www.shadertoy.com/view/4syGWK
 */
