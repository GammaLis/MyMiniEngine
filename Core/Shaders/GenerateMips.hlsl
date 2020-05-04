#define GenerateMips_RootSig \
	"RootFlags(0)," \
	"RootConstants(b0, num32BitConstants = 4)," \
	"DescriptorTable(SRV(t0, numDescriptors = 1))," \
	"DescriptorTable(UAV(u0, numDescriptors = 4))," \
	"StaticSampler(s0, " \
		"addressU = TEXTURE_ADDRESS_CLAMP," \
		"addressV = TEXTURE_ADDRESS_CLAMP," \
		"addressW = TEXTURE_ADDRESS_CLAMP," \
		"filter = FILTER_MIN_MAG_MIP_LINEAR)"

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

Texture2D<float4> _SrcTexture	: register(t0);

RWTexture2D<float4> OutputMips[4]	: register(u0);

SamplerState s_BilinearClampSampler	: register(s0);

// the reason for separating channels is to reduce bank conflicts in 
// the local data memory controller. A large stride will cause more threads to
// collide on the same memory bank
groupshared float sh_R[64];
groupshared float sh_G[64];
groupshared float sh_B[64];
groupshared float sh_A[64];

void StoreColor(uint index, float4 color)
{
	sh_R[index] = color.r;
	sh_G[index] = color.g;
	sh_B[index] = color.b;
	sh_A[index] = color.a;
}

float4 LoadColor(uint index)
{
	return float4(sh_R[index], sh_G[index], sh_B[index], sh_A[index]);
}

float3 ApplySRGBCurve(float3 x)
{
	float3 res;

	// this is the exactly the sRGB curve
	// ret = x < 0.0031308 ? 12.92 * x : 1.055 * pow(abs(x), 1.0 / 2.4) - 0.055;

	// this is cheaper but nearly equivalent
	res = x < 0.0031308 ? 12.92 * x : 1.13005 * sqrt(abs(x - 0.00228)) - 0.13448 * x + 0.005719;

	return res;
}

float4 PackColor(float4 linearColor)
{
#ifdef CONVERT_TO_SRGB
	return float4(ApplySRGBCurve(linearColor.rgb), linearColor.a);
#else
	return linearColor;
#endif	// CONVERT_TO_SRGB
}

[RootSignature(GenerateMips_RootSig)]
[numthreads(GroupSize, GroupSize, 1)]
void main( uint3 dtid : SV_DispatchThreadID, uint gtindex : SV_GroupIndex )
{
	// one bilinear sample is insufficient when scaling down by more than 2x
	// You will slightly undersample in the case where the source dimension is odd.
	// This is why it's a really good idea to only generate mips on power-of-two sized textures.
	// Trying to handle the undersampling case will force this shader to be slower and 
	// more complicated as it will have to take more source texture samples
#if NON_POWER_OF_TWO == 0
	float2 uv = (dtid.xy + 0.5) * _TexelSize;
	float4 src0 = _SrcTexture.SampleLevel(s_BilinearClampSampler, uv, _SrcMipLevel);
#elif NON_POWER_OF_TWO == 1
	// > 2:1 in X dimension
	// use 2 bilinear samples to guarantee we don't undersample when downsizing by more than 2x horizontally
	float2 uv0 = _TexelSize * (dtid.xy + float2(0.25, 0.5));
	float2 uv1 = uv0 + _TexelSize * float2(0.5, 0);
	float4 src0 = 0.5 * (_SrcTexture.SampleLevel(s_BilinearClampSampler, uv0, _SrcMipLevel) + 
		_SrcTexture.SampleLevel(s_BilinearClampSampler, uv1, _SrcMipLevel));
#elif NON_POWER_OF_TWO == 2
	// > 2:1 in Y dimension
	// use 2 bilinear samples to guarantee we don't undersample when downsizing by more than 2x vertically
	float2 uv0 = _TexelSize * dtid.xy + float2(0.5, 0.25);
	float2 uv1 = uv0 + _TexelSize * float2(0, 0.5);
	float4 src0 = 0.5 * (_SrcTexture.SampleLevel(s_BilinearClampSampler, uv0, _SrcMipLevel) + 
		_SrcTexture.SampleLevel(s_BilinearClampSampler, uv1, _SrcMipLevel));
#elif NON_POWER_OF_TWO == 3
	// > 2:1 in both dimensions
	// use 4 bilinear samples to guarantee we don't undersample when downsizing by more than 2x 
	// in both directions
	float2 uv0 = _TexelSize * (dtid.xy + float2(0.25, 0.25));
	float2 uv1 = uv0 + _TexelSize * float2(0.5, 0  );
	float2 uv2 = uv0 + _TexelSize * float2(0,   0.5);
	float2 uv3 = uv0 + _TexelSize * float2(0.5, 0.5);
	float4 src0 = _SrcTexture.SampleLevel(s_BilinearClampSampler, uv0, _SrcMipLevel);
	src0 += _SrcTexture.SampleLevel(s_BilinearClampSampler, uv1, _SrcMipLevel);
	src0 += _SrcTexture.SampleLevel(s_BilinearClampSampler, uv2, _SrcMipLevel);
	src0 += _SrcTexture.SampleLevel(s_BilinearClampSampler, uv3, _SrcMipLevel);
	src0 *= 0.25;
#endif
	
	OutputMips[0][dtid.xy] = PackColor(src0);

	// a scalar (constant) branch can exit all threads coherently
	if (_NumMipLevel == 1)
		return;

	// without lane swizzle operations, the only way to share data with other threads is through LDS
	StoreColor(gtindex, src0);

	// this guarantee all LDS writes are complete and that all threads have executed all instructions
	// so far (and therefore have issued their LDS write instructions)
	GroupMemoryBarrierWithGroupSync();

	// with low 3 bits for X and high 3 bits for Y, this bit mask (binary: 001001) checks that 
	// X and Y are even (GroupSize = 8)
	if ((gtindex & 0x9) == 0)	// ( (dtid.x | dtid.y) & 1 ) == 0
	{
		float4 src1 = LoadColor(gtindex + 1);
		float4 src2 = LoadColor(gtindex + GroupSize);
		float4 src3 = LoadColor(gtindex + GroupSize + 1);
		src0 = 0.25 * (src0 + src1 + src2 + src3);

		OutputMips[1][dtid.xy / 2] = src0;
		StoreColor(gtindex, src0);
	}

	if (_NumMipLevel == 2)
		return;

	GroupMemoryBarrierWithGroupSync();

	// this bit mask (binary: 011011) checks that X and Y are multiples of 4
	if ((gtindex & 0x1B) == 0)
	{
		float4 src1 = LoadColor(gtindex + 2);
		float4 src2 = LoadColor(gtindex + 2 * GroupSize);
		float4 src3 = LoadColor(gtindex + 2 * GroupSize + 2);
		src0 = 0.25 * (src0 + src1 + src2 + src3);
		
		OutputMips[2][dtid.xy / 4] = PackColor(src0);
		StoreColor(gtindex, src0);
	}

	if (_NumMipLevel == 3)
		return;

	GroupMemoryBarrierWithGroupSync();

	// this bit mask would be 111111 (X & Y multiples of 8), but only 1 thread fits that criteria
	if ((gtindex & 0x3F) == 0)
	{
		float4 src1 = LoadColor(gtindex + 4);
		float4 src2 = LoadColor(gtindex + 4 * GroupSize);
		float4 src3 = LoadColor(gtindex + 4 * GroupSize + 4);
		src0 = 0.25 * (src0 + src1 + src2 + src3);
		
		OutputMips[3][dtid.xy / 8] = PackColor(src0);
	}
}
