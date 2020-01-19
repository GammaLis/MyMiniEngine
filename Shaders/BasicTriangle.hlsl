
// cbuffer cbPerMaterial 	: register(b0)
// {
// 	float3 _Color;
// };	

struct VSInput
{
	float3 vertex 	: POSITION;
	float3 color  	: COLOR;
};

struct VSOutput
{
	float4 pos 		: SV_POSITION;
	float3 color 	: COLOR;
};

VSOutput vert (VSInput v)
{
	VSOutput o;

	o.pos = float4(v.vertex, 1.0);
	o.color = v.color;

	return o;
}

float4 frag (VSOutput i) : SV_TARGET
{
	float4 c = float4(1.0, 1.0, 1.0, 1.0);

	c = float4(i.color, 1.0);

	return c;
}
