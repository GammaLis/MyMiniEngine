#define RootSig_VoronoiTexture \
	"RootFlags(0)," \
	"RootConstants(b0, num32BitConstants = 8)," \
	"DescriptorTable(SRV(t0, numDescriptors = 2))," \
	"DescriptorTable(UAV(u0, numDescriptors = 2))," \
	"StaticSampler(s0, " \
		"addressU = TEXTURE_ADDRESS_CLAMP, " \
		"addressV = TEXTURE_ADDRESS_CLAMP, " \
		"addressW = TEXTURE_ADDRESS_CLAMP, " \
		"filter = FILTER_MIN_MAG_MIP_POINT)," \
	"StaticSampler(s1," \
		"addressU = TEXTURE_ADDRESS_WRAP, " \
		"addressV = TEXTURE_ADDRESS_WRAP, " \
		"addressW = TEXTURE_ADDRESS_WRAP, " \
		"filter = FILTER_MIN_MAG_MIP_LINEAR)"

// x - rg, y - ba
float4 PackCoord(uint2 coord)
{
	uint x = coord.x, y = coord.y;
	float r = ((x >> 0) & 0xFF) / 255.0f;
	float g = ((x >> 8) & 0xFF) / 255.0f;
	float b = ((y >> 0) & 0xFF) / 255.0f;
	float a = ((y >> 8) & 0xFF) / 255.0f;
	return float4(r, g, b, a);
}

uint2 UnpackCoord(float4 color)
{
	uint4 iColor = uint4(color * 255.0f);
	iColor &= 0xFF;
	uint x = (iColor.r) | (iColor.g << 8);
	uint y = (iColor.b) | (iColor.a << 8);
	return uint2(x, y);
}

float3 CoordColor(uint2 coord)
{
	uint id = coord.x | coord.y;
	id = id * 1234;	// id太小，放大 增加差异
	float r = ((id >>  0) & 0xFF) / 255.0f;
	float g = ((id >>  8) & 0xFF) / 255.0f;
	float b = ((id >> 16) & 0xFF) / 255.0f;
	return float3(r, g, b);
}
