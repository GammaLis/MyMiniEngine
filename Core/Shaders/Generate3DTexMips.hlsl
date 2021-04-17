// generate 3D texture mips
// now only support PowOfTwo 3D textures
#define RootSig_Generate3DTextureMips \
	"RootFlags(0)," \
	"RootConstants(b0, num32BitConstants = 8)," \
	"DescriptorTable(SRV(t0, numDescriptors = 1))," \
	"DescriptorTable(UAV(u0, numDescriptors = 3))," \
	"StaticSampler(s0, " \
		"addressU = TEXTURE_ADDRESS_CLAMP," \
		"addressV = TEXTURE_ADDRESS_CLAMP," \
		"addressW = TEXTURE_ADDRESS_CLAMP," \
		"filter = FILTER_MIN_MAG_MIP_LINEAR)"

#define GroupSize 4
#define GroupSizeX GroupSize
#define GroupSizeY GroupSize
#define GroupSizeZ GroupSize
#define GroupTotalSize (GroupSizeX * GroupSizeY * GroupSizeZ)

cbuffer CSConstants	: register(b0)
{
	float3 _TexelSize;	// 1.0 / OutMip1.Dimensions
	uint _SrcMipLevel;	// texture level of source mip
	uint _NumMipLevels;	// number of OutMips to write : [1, 4]
};

Texture3D<float4> _SrcMip 	: register(t0);

RWTexture3D<float4> OutMips[3]	: register(u0);	// default group size = 4

SamplerState s_BilinearClampSampler	: register(s0);

// the reason for separating channels is to reduce bank conflicts in 
// the local data memory controller. A large stride will cause more threads to
// collide on the same memory bank
groupshared float sh_R[GroupTotalSize];
groupshared float sh_G[GroupTotalSize];
groupshared float sh_B[GroupTotalSize];
groupshared float sh_A[GroupTotalSize];

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

[RootSignature(RootSig_Generate3DTextureMips)]
[numthreads(GroupSizeX, GroupSizeY, GroupSizeZ)]
void main(uint3 dtid : SV_DispatchThreadID, uint gtindex : SV_GroupIndex)
{
	float3 uv = (dtid + 0.5f) * _TexelSize;
	float4 src0 = _SrcMip.SampleLevel(s_BilinearClampSampler, uv, _SrcMipLevel);

	OutMips[0][dtid] = src0;

	if (_NumMipLevels == 1)
		return;

	uint c = (dtid.x | dtid.y | dtid.z);

	StoreColor(gtindex, src0);
	GroupMemoryBarrierWithGroupSync();

	if (c & 1 == 0)
	{
		float4 src1 = LoadColor(gtindex + 1);
		float4 src2 = LoadColor(gtindex + GroupSizeX);
		float4 src3 = LoadColor(gtindex + 1 + GroupSizeX);

		uint zPlane = GroupSizeX * GroupSizeY;
		float4 src4 = LoadColor(gtindex + zPlane);
		float4 src5 = LoadColor(gtindex + 1 + zPlane);
		float4 src6 = LoadColor(gtindex + GroupSizeX + zPlane);
		float4 src7 = LoadColor(gtindex + 1 + GroupSizeX + zPlane);

		src0 = 0.125f * (src0 + src1 + src2 + src3 + 
			src4 + src5 + src6 + src7);
		OutMips[1][dtid / 2] = src0;
		StoreColor(gtindex, src0);
	}

	if (_NumMipLevels == 2)
		return;

	GroupMemoryBarrierWithGroupSync();

	if (c & 3 == 0)
	{
		uint yRow = 2 * GroupSizeX;
		float4 src1 = LoadColor(gtindex + 2);
		float4 src2 = LoadColor(gtindex + yRow);
		float4 src3 = LoadColor(gtindex + 2 + yRow);

		uint zPlane = 2 * GroupSizeX * GroupSizeY;
		float4 src4 = LoadColor(gtindex + zPlane);
		float4 src5 = LoadColor(gtindex + 2 + zPlane);
		float4 src6 = LoadColor(gtindex + yRow + zPlane);
		float4 src7 = LoadColor(gtindex + 2 + yRow + zPlane);

		src0 = 0.125f * (src0 + src1 + src2 + src3 + 
			src4 + src5 + src6 + src7);
		OutMips[2][dtid / 4] = src0;
	}

}
