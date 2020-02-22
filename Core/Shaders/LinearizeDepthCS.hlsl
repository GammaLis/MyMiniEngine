#include "SSAORS.hlsli"

cbuffer CSConstants	: register(b0)
{
	float _ZMagic;	// (zFar - zNear) / zNear
};

Texture2D<float> _TexDepth		: register(t0);

RWTexture2D<float> linearDepth 	: register(u0);

[RootSignature(SSAO_RootSig)]
[numthreads(16, 16, 1)]
void main( uint3 dispatchThreadId : SV_DispatchThreadID )
{
	linearDepth[dispatchThreadId.xy] = 1.0 / (1.0 + _TexDepth[dispatchThreadId.xy] * _ZMagic);
}

// 注：linearDepth 并不对应观察空间[zNear, zFar],
// 根据公式 少乘nFar，实际对应[zNear/zFar, 1]
