#include "VoronoiRS.hlsli"

cbuffer CBConstants	: register(b0)
{
	uint2 _Size;
};

struct VSInput
{
	float2 center 	: CENTROID;
};

struct VSOutput
{
	float4 position	: SV_POSITION;
	float2 pos 		: POSITION;
	float2 center	: CENTROID;
	float3 color 	: COLOR;
};

float3 Color(uint id)
{
	id = id * 1234;	// id太小，放大 增加差异
	float r = ((id >>  0) & 0xFF) / 255.0f;
	float g = ((id >>  8) & 0xFF) / 255.0f;
	float b = ((id >> 16) & 0xFF) / 255.0f;
	return float3(r, g, b);
}

[RootSignature(RootSig_VoronoiTexture)]
VSOutput main(VSInput v, uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
	static const float scale = .25f;	// 0.05f	// Q: 如果过大，内存会不断上涨(主要是DynamicDescriptors)，尚不明白？？？	-2020-8-5
	const float2 xy0 = v.center - scale * _Size;
	const float2 xy1 = v.center + scale * _Size;

	float2 uv = float2((vid >> 1) & 1, vid & 1);
	// (0, 0), (0, 1), (1, 0), (1, 1)
	
	float2 pos = lerp(xy0, xy1, uv);

	VSOutput o;
	
	float2 cPos = float2(pos.x * 2.0f / _Size.x - 1.0f, 1.0f - pos.y * 2.0f / _Size.y);
	o.position = float4(cPos, 0.0f, 1.0f);
	o.pos = pos;
	o.center = v.center;
	o.color = Color(iid);
	
	return o;
}
