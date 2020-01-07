
struct VSInput
{
	float3 vertex : POSITION;
	float2 uv : TEXCOORD0;
	float4 color : COLOR;
};

struct VSOutput
{
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD0;
	float4 color : TEXCOORD1;
};

cbuffer CBPerFrame : register(b0)
{
	float4 baseColor;
};

Texture2D _MainTex : register(t0);
SamplerState sampler_MainTex : register(s0);

VSOutput vert(VSInput v)
{
	VSOutput o;
	o.pos = float4(v.vertex, 1.0f);
	o.uv = v.uv;
	o.color = v.color;

	return o;
}

float4 frag(VSOutput i) : SV_TARGET
{
	float4 color = i.color;

	color *= baseColor;

	color *= _MainTex.Sample(sampler_MainTex, i.uv);
	
	return color;
}

/**
	HLSL packs matrices in a column major order so that it can easily do the vector matrix multiplication, by multiplying the
vector by each row in the matrix, rather than each column. This is convenient because HLSL is now able to store an entire
column (now a row in HLSL) in the GPU registers for the calculation in a single instruction.
	Although HLSL packs matrices in a column major order, it actually reads the matrices in a row major order, which is how it
is able to grab a row from a matrix and store it in a register for calculations.
	this is the matrix we are pasing from our app, which is row major ordering:
	[ 1,  2,  3,  4;
	  5,  6,  7,  8;
	  9, 10, 11, 12,
	 13, 14, 15, 16]
	this is how HLSL is storing the matrix:
	[ 1,  5,  9, 13;
	  2,  6, 10, 14;
	  3,  7, 11, 15;
	  4,  8, 12, 16]
*/