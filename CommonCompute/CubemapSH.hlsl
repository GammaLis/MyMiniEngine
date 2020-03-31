#include "PreComputing.hlsli"

static const uint GroupSizeX = 32;
static const uint GroupSizeY = 32;

cbuffer CSConstants	 : register(b0)
{
	uint2 _Dimensions;	// x - width, y - height
};

TextureCube<float4> _EnvironmentMap	: register(t0);

RWStructuredBuffer<SH9Color> Output	: register(u0);

SamplerState s_LinearSampler		: register(s0);

[numthreads(GroupSizeX, GroupSizeY, 1)]
void main( uint3 dtid : SV_DispatchThreadID, uint gtIndex : SV_GroupIndex )
{
	if (dtid.x >= _Dimensions.x || dtid.y >= _Dimensions.y)
		return;

	uint GroupSize = GroupSizeX * GroupSizeY;

	// 
	uint width = _Dimensions.x;
	uint height = _Dimensions.y;
	uint numXTiles = (width + GroupSizeX - 1) / GroupSizeX;
	uint numYTiles = (height + GroupSizeY - 1) / GroupSizeY;

	SH9Color sh;
	SH9ColorInit(sh);
	SH9 shBasis;
	for (uint i = dtid.x; i < width; i += GroupSizeX)
	{
		for (uint j = dtid.y; j < height; j += GroupSizeY)
		{
			float2 uv = (float2(i, j) + 0.5) / _Dimensions;
			uv = 2 * uv - 1;
			uv.y *= -1;	// [-1, 1]

			float theta0 = uv.y * PiOver2;	// [-pi/2, pi/2]
			float phi0 = uv.x * Pi;	// [-pi, pi]

			float x = cos(theta0) * sin(phi0);
			float y = sin(theta0);
			float z = cos(theta0) * cos(phi0);

			float3 N = float3(x, y, z);
			float3 color = _EnvironmentMap.SampleLevel(s_LinearSampler, N, 0.0).rgb;
			
			shBasis = SH9Basis(N);
			SH9CosineLobe(shBasis);
			[unroll]
			for (uint k = 0; k < 9; ++k) 
			{
				sh.c[k] += color * shBasis.c[k];
			}
		}
	}
	Output[gtIndex] = sh;

	GroupMemoryBarrierWithGroupSync();

	// sum
	for (uint s = GroupSize/2; s > 0; s >>= 1)
	{
		if (gtIndex < s)
		{
			for (uint k = 0; k < 9; ++k)
				Output[gtIndex].c[k] += Output[gtIndex + s].c[k];
		}
		GroupMemoryBarrierWithGroupSync();
	}

	uint NumSamples = GroupSize * GroupSize;
	if (gtIndex == 0)
	{
		for (uint k = 0; k < 9; ++k)
			Output[gtIndex].c[k] *= 4 * Pi / NumSamples;

		// debug
		// for (uint k = 0; k < 9; ++k)
		// 	Output[gtIndex].c[k] = 1;
	}
}

// Cubemap to spherical harmonics 
/**
 * 	<<An Efficient Representation for Irradiance Environment Maps>>
 * 	<<GPU Gems2 - chapter 10 real time computation dynamic Dynamic Irradiance Environment Maps>>
 *
 * 	Filament
 * 	TheRealMJP
 *  http://xlgames-inc.github.io/
 *  ...
 *  
 * 	SHBasics.CPP
 */
