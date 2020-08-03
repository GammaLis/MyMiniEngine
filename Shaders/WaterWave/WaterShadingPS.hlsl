
struct VSOutput
{
	float4 pos 	: SV_POSITION;
	float2 uv	: TEXCOORD0;
};

Texture2D<float4> _NormalAndFoldMap	: register(t1);

SamplerState s_LinearWrapSampler	: register(s0);

float4 main(VSOutput i) : SV_TARGET
{
	float4 color = float4(0.5f, 0.25f, 1.0f, 1.0f);

	float foam = _NormalAndFoldMap.Sample(s_LinearWrapSampler, i.uv).a;

	color = foam;

	// TO DO
	// ocean surface shading (now just wireframe)	-2020-8-3

	return color;
}
