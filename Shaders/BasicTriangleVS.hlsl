
cbuffer cbPerMaterial 	: register(b0)
{
	float3 _Color;
};

struct VertexInput
{
	float3 vertex 	: POSITION;
	float3 color  	: COLOR;
	float2 uv 		: TEXCOORD0;
};

struct VertexOutput
{
	float4 pos 		: SV_POSITION;
	float3 color 	: COLOR;
	float2 uv		: TEXCOORD0;
};

VertexOutput main (VertexInput v)
{
	VertexOutput o;

	o.pos = float4(v.vertex, 1.0);
	o.color = v.color;
	o.uv = v.uv;

	return o;
}
