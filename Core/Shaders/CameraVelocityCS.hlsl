#include "MotionBlurRS.hlsli"
#include "PixelPacking_Velocity.hlsli"

// we can use the original depth buffer or a linearized one. In this case, 
// we use linear Z.
// Note that hyperbolic Z is reversed by default (TBD) for increased precision,
// so its Z=0 maps to the far plane. With linear Z, Z=0 maps to the eye position.
// Both extent to Z=1.
#define USE_LINEAR_Z

cbuffer CSConstants	: register(b1)
{
	matrix _CurToPrevXForm;	// ScreenSpaceToClipSpace * Reprojection(=Inv(ViewProjMat) * Prev_ViewProjMat) * ClipSpaceToScreenSpace
}

Texture2D<float> _DepthBuffer	: register(t0);
RWTexture2D<packed_velocity_t> velocityBuffer : register(u0);

[RootSignature(MotionBlur_RootSig)]
[numthreads(8, 8, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	uint2 st = DTid.xy;
	float depth = _DepthBuffer[st];
	float2 curPixel = st + 0.5;		// 偏移+0.5到像素中心
#ifdef USE_LINEAR_Z
	// 线性深度 zlin = 1 / (1 + ZMagic * zc) ZMagic = (f - n) / n,	<==> zc = (1/zlin) * rcpZMagic - rcpZMagic
	// 实际线性深度 zlin = f *n / (n + (f -n) * zc) -> f * 1 / (1 + ZMagic * zc) zlin belongs to [n, f]
	// zc = n/(n-f) - nf/(n-f)/z => [-n/(f-n) *z + nf/(f-n)] / z => 1 * RcpZMagic - zlin * RcpZMagic
	float4 hPos = float4(curPixel * depth, 1.0, depth);	// _CurToPrevXForm 含有变换 z' = z *RcpZMagic - w *RcpZMagic (RcpZMagic = n / (f-n))
#else
	float4 hPos = float4(curPixel, depth, 1.0);
#endif	// USE_LINEAR_Z

	float4 prevHPos = mul(hPos, _CurToPrevXForm);

	prevHPos.xyz /= prevHPos.w;

#ifdef USE_LINEAR_Z
	prevHPos.z = prevHPos.w;
#endif

	velocityBuffer[st] = PackVelocity(prevHPos.xyz - float3(curPixel, depth));
}
