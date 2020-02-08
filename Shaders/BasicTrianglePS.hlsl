cbuffer cbPerMaterial 	: register(b0)
{
	float3 _Color;
};

Texture2D<float4> _DefaultTex	: register(t0);

SamplerState s_LinearWrapSampler: register(s0);


struct VertexOutput
{
	float4 pos 		: SV_POSITION;
	float3 color 	: COLOR;
	float2 uv		: TEXCOORD0;
};

float4 main (VertexOutput i) : SV_TARGET
{
	float4 c = float4(1.0, 1.0, 1.0, 1.0);

	c = float4(i.color, 1.0);

	float4 tex = _DefaultTex.Sample(s_LinearWrapSampler, i.uv);

	c.rgb *= tex.rgb * _Color;

	return c;
}