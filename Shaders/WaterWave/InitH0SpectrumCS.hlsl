// 初始化 H0
#include "WaterWaveRS.hlsli"

cbuffer CSConstants	: register(b0)
{
	uint _N;
	float _L;
	float _V;	// 风速
	float _A;	// 振幅
	float2 _W;	// 风向

};

RWTexture2D<float4> OutputH0	: register(u0);

// Phillips 频谱
float Phillips(float2 k, float2 w, float V, float A)
{
	// if (k.x == 0.0f && k.y == 0.0f) return 0.0f;

	float k_len2 = dot(k, k);	// squared length of wave vector k
	if (k_len2 < c_epsilon)	// 1e-6
	{
		return 0.0f;
	}
	float k_len4 = k_len2 * k_len2;

	float L = (V * V) / c_g;	// largest possible wave for wind speed V
	float L2 = L * L;
	float l = L * 0.001f;		// supress waves smaller than this
	float l2 = l * l;
	
	// normalize
	k = normalize(k);
	w = normalize(w);
	float kDotW = dot(k, w);
	float kDotW2 = kDotW * kDotW;
	float kDotW4 = kDotW2 * kDotW2;
	float kDotW8 = kDotW4 * kDotW4;

	float P = A * exp(-1.0f / (k_len2 * L2)) / k_len4 * kDotW4;	// |dot(k, w)^x| x 默认=2，可以试试4,6,8,...

	if (kDotW < 0.0f)
	{
		// wave if moving against wind direction w
		P *= 0.07f;
	}

	return P * exp(-k_len2 * l2);
}

[RootSignature(RootSig_WaterWave)]
[numthreads(16, 16, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	uint2 halfSize = _N.xx / 2;	// Nx == Ny == N
	// wave vector
	float2 k = (M_2PI * (float2(dtid.xy) - float2(halfSize)) ) / _L;	// _L - [Lx, Ly]
	// Error: 之前采用 dtid.xy - halfSize, 2个uint2相减，向下溢出	-2020-8-1
	// k = (2Pi * k - Pi*N) / L
	float k_len = length(k);

	// h0
	uint seed = dtid.y * _N + dtid.x;
	float2 gaussian = Gauss_BoxMuller(seed);
	float2 hTilde0 = M_1_SQRT2 * gaussian * sqrt(Phillips(k, _W, _V, _A));
	float2 hTilde0Conj = M_1_SQRT2 * gaussian * sqrt(Phillips(-k, _W, _V, _A));
	hTilde0Conj.y *= -1.0f;	// 共轭可以改到后面计算

	// dispersion
	// float w, dwdk;
	// DeepDispersion(k_len, w, dwdk);

	OutputH0[dtid.xy] = float4(hTilde0, hTilde0Conj);
	 
	// debug
	// float r0 = random01(seed);
	// float r1 = random01(seed);
	// float2 g0 = Gauss_BoxMuller(seed);
	// float2 g1 = Gauss_BoxMuller(seed);
	// OutputH0[dtid.xy] = float4(gaussian.x, gaussian.y, 0.0f, 1.0f);
	// OutputH0[dtid.xy] = float4(r0, r1, 0.0f, 1.0f);
}

/**
 * 	References:
 * 	https://zhuanlan.zhihu.com/p/95482541
 * 	https://github.com/speps/GX-EncinoWaves
 */
