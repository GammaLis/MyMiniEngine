#include "CommonCullingRS.hlsli"

#ifndef NON_POWER_OF_TWO
#define NON_POWER_OF_TWO 0
#endif

#define GroupSize 8

cbuffer CSConstants	: register(b0)
{
	uint _SrcMipLevel;	// texture level of source mip
	uint _NumMipLevel;	// number of OutputMips to write: [1, 4]
	float2 _TexelSize;	// 1.0 / OutputMips[0].Dimensions
};

Texture2D<float> _SrcTexture	: register(t1);

RWTexture2D<float> OutputMips[4]	: register(u0);

groupshared float sh_Z[64];

[RootSignature(Culling_RootSig)]
[numthreads(GroupSize, GroupSize, 1)]
void main( uint3 dtid : SV_DispatchThreadID, uint gtindex : SV_GroupIndex )
{
	float2 uv0 = (dtid.xy + float2(0.25, 0.25)) * _TexelSize;
	float2 uv1 = uv0 + float2(0.5, 0.0) * _TexelSize;
	float2 uv2 = uv0 + float2(0.0, 0.5) * _TexelSize;
	float2 uv3 = uv0 + float2(0.5, 0.5) * _TexelSize;
	float4 depth = 
	{
		_SrcTexture.SampleLevel(s_PointClampSampler, uv0, _SrcMipLevel),
		_SrcTexture.SampleLevel(s_PointClampSampler, uv1, _SrcMipLevel),
		_SrcTexture.SampleLevel(s_PointClampSampler, uv2, _SrcMipLevel),
		_SrcTexture.SampleLevel(s_PointClampSampler, uv3, _SrcMipLevel)
	};
#ifdef REVERZED_Z
	float src0 = min( min( min(depth.x, depth.y), depth.z ), depth.w );
#else
	float src0 = max( max( max(depth.x, depth.y), depth.z ), depth.w );
#endif	// REVERZED_Z
	
	OutputMips[0][dtid.xy] = src0;

	// a scalar (constant) branch can exit all threads coherently
	if (_NumMipLevel == 1)
		return;

	// without lane swizzle operations, the only way to share data with other threads is through LDS
	sh_Z[gtindex] = src0;

	// this guarantee all LDS writes are complete and that all threads have executed all instructions
	// so far (and therefore have issued their LDS write instructions)
	GroupMemoryBarrierWithGroupSync();

	// with low 3 bits for X and high 3 bits for Y, this bit mask (binary: 001001) checks that 
	// X and Y are even (GroupSize = 8)
	if ((gtindex & 0x9) == 0)	// ( (dtid.x | dtid.y) & 1 ) == 0
	{
		float src1 = sh_Z[gtindex + 1];
		float src2 = sh_Z[gtindex + GroupSize];
		float src3 = sh_Z[gtindex + GroupSize + 1];
#ifdef REVERZED_Z
		src0 = min( min( min(src1, src2), src3 ), src0 );
#else
		src0 = max( max( max(src1, src2), src3 ), src0 );
#endif	// REVERZED_Z
		
		OutputMips[1][dtid.xy / 2] = src0;
		sh_Z[gtindex] = src0;
	}

	if (_NumMipLevel == 2)
		return;

	GroupMemoryBarrierWithGroupSync();

	// this bit mask (binary: 011011) checks that X and Y are multiples of 4
	if ((gtindex & 0x1B) == 0)
	{
		float src1 = sh_Z[gtindex + 2];
		float src2 = sh_Z[gtindex + 2 * GroupSize];
		float src3 = sh_Z[gtindex + 2 * GroupSize + 2];
#ifdef REVERZED_Z
		src0 = min( min( min(src1, src2), src3 ), src0 );
#else
		src0 = max( max( max(src1, src2), src3 ), src0 );
#endif	// REVERZED_Z
		
		OutputMips[2][dtid.xy / 4] = src0;
		sh_Z[gtindex] = src0;
	}

	if (_NumMipLevel == 3)
		return;

	GroupMemoryBarrierWithGroupSync();

	// this bit mask would be 111111 (X & Y multiples of 8), but only 1 thread fits that criteria
	if ((gtindex & 0x3F) == 0)
	{
		float src1 = sh_Z[gtindex + 4];
		float src2 = sh_Z[gtindex + 4 * GroupSize];
		float src3 = sh_Z[gtindex + 4 * GroupSize + 4];
#ifdef REVERZED_Z
		src0 = min( min( min(src1, src2), src3 ), src0 );
#else
		src0 = max( max( max(src1, src2), src3 ), src0 );
#endif	// REVERZED_Z
		
		OutputMips[3][dtid.xy / 8] = src0;
	}
}
