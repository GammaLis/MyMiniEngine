
cbuffer cbPerMaterial 	: register(b0)
{
	float3 _Color;
};

struct VertexInput
{
	float3 vertex 	: POSITION;
	float3 color  	: COLOR;
};

struct VertexOutput
{
	float4 pos 		: SV_POSITION;
	float3 color 	: COLOR;
};

VertexOutput main (VertexInput v)
{
	VertexOutput o;

	o.pos = float4(v.vertex, 1.0);
	o.color = v.color;

	return o;
}
