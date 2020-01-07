
static float2 _FullScrQuadUVs[6] =
{
	float2(0.0f, 1.0f),
	float2(0.0f, 0.0f),
	float2(1.0f, 0.0f),

	float2(0.0f, 1.0f),
	float2(1.0f, 0.0f),
	float2(1.0f, 1.0f)
};

#define _MaxBlurRadius 5

// static float _GaussianWeights[] = 
// {
//	// 暂时没有找到系数
// }

cbuffer cbBlurVec	: register(b0)
{
	float2 _BlurVec;
};

cbuffer cbBlur 		: register(b1)
{
	float4 _MainTexSize;	// (width, height, 1.0f/width, 1.0f/height)

	int _BlurRadius;
	// support up to 11 blur weights
	float w0;
	float w1;
	float w2;
	float w3;
	float w4;
	float w5;
	float w6;
	float w7;
	float w8;
	float w9;
	float w10;
};

Texture2D _MainTex	: register(t0);

SamplerState gPointWrapSampler   		: register(s0);
SamplerState gPointClampSampler       	: register(s1);
SamplerState gLinearWrapSampler       	: register(s2);
SamplerState gLinearClampSampler      	: register(s3);
SamplerState gAnisotropicWrapSampler  	: register(s4);
SamplerState gAnisotropicClampSampler 	: register(s5);
SamplerComparisonState gShadowSampler	: register(s6);

// struct VertexInput
// {
// 	float3 vertex 	: POSITION;
// 	float2 uv 		: TEXCOORD0;
// };

struct VertexOutput
{
	float4 position : SV_POSITION;
	float2 uv 		: TEXCOORD0;
};

VertexOutput vert(uint vid : SV_VertexID)
{
	VertexOutput o;

	float2 uv = _FullScrQuadUVs[vid];

	// quad covering screen in NDC space
	float2 scrPos = float2(uv.x - 0.5f, 0.5f - uv.y) * 2.0f;

	o.position = float4(scrPos, 0.0f, 1.0f);
	o.uv = uv;

	return o;
}

float4 main(VertexOutput i) : SV_Target
{
	// put in an array for each indexing
	float weights[11] = {w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10};

	float2 uv = i.uv;
	float4 baseColor = _MainTex.Sample(gLinearClampSampler, uv);

	float4 color = weights[_BlurRadius] * baseColor;
	float2 offset = _BlurVec * _MainTexSize.zw;
	float2 currUV = uv;

	// [unroll]
	for (int k = 1; k <= _BlurRadius; ++k)
	{
		float lWeight = weights[_BlurRadius - k];
		currUV = uv - k * offset.xy;
		float4 lCol = _MainTex.Sample(gLinearClampSampler, currUV);
		color += lWeight * lCol;

		float rWeight = weights[_BlurRadius + k];
		currUV = uv + k * offset.xy;
		float4 rCol = _MainTex.Sample(gLinearClampSampler, currUV);
		color += rWeight * rCol;
	}

	return color;
}