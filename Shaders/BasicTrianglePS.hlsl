struct VertexOutput
{
	float4 pos 		: SV_POSITION;
	float3 color 	: COLOR;
};

float4 main (VertexOutput i) : SV_TARGET
{
	float4 c = float4(1.0, 1.0, 1.0, 1.0);

	c = float4(i.color, 1.0);

	return c;
}