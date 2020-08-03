#include "WaterWaveRS.hlsli"

cbuffer CSConstants	: register(b0)
{
	uint _N;
	float _L;
	float _Depth;
	float4 _Time;	// t/20, t, 2t, 3t
};

Texture2D<float4> _InputH0	: register(t0);

RWTexture2D<float2> OutputSpectrum	: register(u0);
RWTexture2D<float2> OutputDX		: register(u1);
RWTexture2D<float2> OutputDY		: register(u2);

// w(k) - 角频率w 和 波长k 的Dispersion关系，取决于重力、海洋深度和其它物理参数
// w^2 = g * k ==> 2w * dwdk = g
void DeepDispersion(float k, out float w, out float dwdk)
{
	w = sqrt(abs(c_g * k));
	dwdk = c_g / (2.0f * w);
}

// speps/GX-EncinoWaves
void FiniteDepthDispersion(float k, out float w, out float dwdk)
{
	float hk = _Depth * k;
	float thk = tanh(hk);
	w = sqrt(abs(c_g * k * thk));
	float chk = cosh(hk);
	dwdk = c_g * (thk + hk / (chk * chk)) / (2.0f * w);
}

// speps/GX-EncinoWaves
void CapillaryDispersion(float k, out float w, out float dwdk)
{
	const float surfaceIntension = 0.074f;
	const float density = 1000.0f;

	float hk = _Depth * k;
	float k2s = k * k * surfaceIntension / density;
	float gpk2s = c_g * k2s;
	float thk = tanh(hk);
	w = sqrt(abs(k * gpk2s * thk));

	float chk = cosh(hk);
	float numerator = ((gpk2s + k2s + k2s) * thk) + (hk * gpk2s / (chk * chk));
	dwdk = numerator / (2.0f * w);
}

[RootSignature(RootSig_WaterWave)]
[numthreads(16, 16, 1)]
void main( uint3 dtid : SV_DispatchThreadID )
{
	uint2 halfSize = _N.xx / 2;
	float2 k = M_2PI * (float2(dtid.xy) - float2(halfSize)) / _L;
	// Error: uint2 (ditd.xy - halfSize) - 可能向下溢出
	float k_len = length(k);
	k /= max(0.001f, k_len);

	// dispersion
	float w, dwdk;
	DeepDispersion(k_len, w, dwdk);

	// time
	float sw, cw;
	sincos(w * _Time.y, sw, cw);
	float2 forward = float2(cw, sw);
	float2 backward = float2(cw, -sw);

	// h = h0 * exp(dispersion * t) + conj(h0) * exp(-dispersion * t)
	float4 h0 = _InputH0[dtid.xy];
	float2 hTilde = ComplexMultiply(h0.xy, forward) + ComplexMultiply(h0.zw, backward);
	OutputSpectrum[dtid.xy] = hTilde;

	// XY displacement spectrum
	float2 dx = float2(k.x * hTilde.y, -k.x * hTilde.x);	// ComplexMultiply(float2(0, -k.x), hTilde)
	float2 dy = float2(k.y * hTilde.y, -k.y * hTilde.x);	// ComplexMultiply(float2(0, -k.y), hTilde);
	OutputDX[dtid.xy] = dx;
	OutputDY[dtid.xy] = dy;
}
