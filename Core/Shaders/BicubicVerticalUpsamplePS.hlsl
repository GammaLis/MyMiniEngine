#include "ShaderUtility.hlsli"
#include "PresentRS.hlsli"

//--------------------------------------------------------------------------------------
// Simple bicubic filter
//
// http://en.wikipedia.org/wiki/Bicubic_interpolation
// http://http.developer.nvidia.com/GPUGems/gpugems_ch24.html
//
//--------------------------------------------------------------------------------------

Texture2D<float3> _ColorTex	: register(t0);

cbuffer Constants : register(b0)
{
	uint2 _TextureSize;
	float _A;
}

float W1(float x)
{
	return x * x * ((_A + 2) * x - (_A + 3)) + 1.0;
}

float W2(float x)
{
	return _A * (x * (x * (x - 5) + 8) - 4);
}

float4 GetWeights(float d1)
{
	return float4(W2(1.0 + d1), W1(d1), W1(1.0 - d1), W2(2.0 - d1));
}

float3 GetColor(uint s, uint t)
{
	return _ColorTex[uint2(s, t)];
}

[RootSignature(Present_RootSig)]                                      
float3 main(float4 pos : SV_Position, float2 uv : Texcoord0) : SV_TARGET
{
	float2 t = uv * _TextureSize + 0.5;
	float2 f = frac(t);
	int2 st = int2(pos.x, t.y);		// int ( x + 0.5) -> 四舍五入

	uint maxHeight = _TextureSize.y - 1;

	uint t0 = max(st.y - 2, 0);
	uint t1 = max(st.y - 1, 0);
	uint t2 = min(st.y + 0, maxHeight);
	uint t3 = min(st.y + 1, maxHeight);

	float4 weights = GetWeights(f.y);

	float3 color = weights.x * GetColor(st.x, t0) + 
		weights.y * GetColor(st.x, t1) + 
		weights.z * GetColor(st.x, t2) +
		weights.w * GetColor(st.x, t3);

#ifdef GAMMA_SPACE
        return color;
#else
        return ApplyDisplayProfile(color, DISPLAY_PLANE_FORMAT);
#endif

	return color;
}

/**
	W(x) = {
	(a + 2) |x|^3 - (a + 3) |x|^2 + 1, 	for |x| < 1,
	a |x|^3 - 5a |x|^2 + 8a |x| - 4a,	for 1 < |x| < 2
	0, otherwise
	}
*/
