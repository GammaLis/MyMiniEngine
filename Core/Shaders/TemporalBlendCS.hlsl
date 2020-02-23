#include "TemporalRS.hlsli"
#include "ShaderUtility.hlsli"
#include "PixelPacking_Velocity.hlsli"

static const uint kLdsPitch = 18;
static const uint kLdsRows = 10;

cbuffer CSConstants	: register(b1)
{
	float2 _RcpBufferDim;		// 1/width, 1/height
	float _TemporalBlendFactor;
	float _RcpSpeedLimiter;
	float2 _ViewportJitter;
};

Texture2D<packed_velocity_t> _VelocityBuffer: register(t0);
Texture2D<float3> _InColor		: register(t1);
Texture2D<float4> _InTemporal	: register(t2);
Texture2D<float> _CurDepth		: register(t3);
Texture2D<float> _PreDepth		: register(t4);

RWTexture2D<float4> OutTemporal	: register(u0);

SamplerState s_LinearSampler	: register(s0);
SamplerState s_PointSampler		: register(s1);

// 共享内存，存储group内（+1 pixel border）depth和R, G, B
groupshared float ldsDepth[kLdsPitch * kLdsRows];
groupshared float ldsR[kLdsPitch * kLdsRows];
groupshared float ldsG[kLdsPitch * kLdsRows];
groupshared float ldsB[kLdsPitch * kLdsRows];

void StoreRGB(uint ldsIndex, float3 rgb)
{
	ldsR[ldsIndex] = rgb.r;
	ldsG[ldsIndex] = rgb.g;
	ldsB[ldsIndex] = rgb.b;
}

float3 LoadRGB(uint ldsIndex)
{
	return float3(ldsR[ldsIndex], ldsG[ldsIndex], ldsB[ldsIndex]);
}

float2 STtoUV(float2 st)
{
	return (st + 0.5) * _RcpBufferDim;
}

float3 ClipColor(float3 color, float3 boxMin, float3 boxMax, float dilation = 1.0)
{
	float3 boxCenter = (boxMin + boxMax) * 0.5;
	float3 halfDim = (boxMax - boxMin) * 0.5 * dilation + 0.001;
	float3 displacement = color - boxCenter;
	float3 units = abs(displacement / halfDim);
	float maxUnit = max(max(units.x, units.y), max(units.z, 1.0));
	return boxCenter + displacement / maxUnit;
}

// siggraph2014_<<High Quality Temporal Supersampling>>
// neighborhood clamping
// 1.
// restrict history to the range of current frame's local neighborhood
// - assumes AA results is blend of neighbors
// - clamp with min/max of 3x3 neighborhood
// 
// 2.
// Simple clamp to min/max of 8 neighbors results in 3x3 box artifacts
// want min/max to appear filtered - round out the shape
// solution: average 2 neighborhood's min/max
void GetBBoxForPair(uint fillIndex, uint holeIndex, out float3 boxMin, out float3 boxMax)
{
	boxMin = boxMax = LoadRGB(fillIndex);
	
	float3 a = LoadRGB(fillIndex - kLdsPitch - 1);
	float3 b = LoadRGB(fillIndex - kLdsPitch + 1);
	boxMin = min(boxMin, min(a, b));
	boxMax = max(boxMax, max(a, b));

	a = LoadRGB(fillIndex + kLdsPitch - 1);
	b = LoadRGB(fillIndex + kLdsPitch + 1);
	boxMin = min(boxMin, min(a, b));
	boxMax = max(boxMax, max(a, b));

	a = LoadRGB(holeIndex);
	b = LoadRGB(holeIndex - fillIndex + holeIndex);
	boxMin = min(boxMin, min(a, b));
	boxMax = max(boxMax, max(a, b));
}

float MaxOf(float4 depths)
{
	return max(max(depths.x, depths.y), max(depths.z, depths.w));
}

int2 GetClosestPixel(uint index, out float closestDepth)
{
	float center = ldsDepth[index];
	float left = ldsDepth[index - 1];
	float right = ldsDepth[index + 1];
	float up = ldsDepth[index - kLdsPitch];
	float down = ldsDepth[index + kLdsPitch];

	closestDepth = min(center, min(min(left, right), min(up, down)));

	if (closestDepth == left)
		return int2(-1, 0);
	else if (closestDepth == right)
		return int2(+1, 0);
	else if (closestDepth == up)
		return int2(0, -1);
	else if (closestDepth == down)
		return int2(0, +1);

	return int2(0, 0);
}

// velocity .xy - screen space velocity, .w - reversedZ  or linearZ
void ApplyTemporalBlend(uint2 st, uint ldsIndex, float3 boxMin, float3 boxMax)
{
	float3 curColor = LoadRGB(ldsIndex);

	float compareDepth;

	// get the velocity of the closest pixel in the '+' formation
	// using velocity of closest (depth) fragment within 3x3 region - <<INSIDE>>
	float3 velocity = UnpackVelocity(_VelocityBuffer[st + GetClosestPixel(ldsIndex, compareDepth)]);

	// curDepth + velocity.z = prevDepth (详见CameraVelocityCS计算Velocity)
	compareDepth += velocity.z;

	// the temporal depth is the actual depth of the pixel found at the same reprojected location
	// 注：因为这里使用Gather，采样不经滤波，所以需要加上_ViewportJitter，
	// 如果使用Sample，应该不用再加，因为考虑了滤波，Jitter因素其实已经考虑进去（大概如此，尚不确定 -20-2-22） 
	float temporalDepth = MaxOf(_PreDepth.Gather(s_LinearSampler, STtoUV(st + velocity.xy + _ViewportJitter))) + 1e-3;
	// float temporalDepth = _PreDepth.SampleLevel(s_LinearSampler, STtoUV(st + velocity.xy), 0);	// 每帧都在变化

	// fast-moving pixels cause motion blur and probably don't need TAA
	float speedFactor = saturate(1.0 - length(velocity.xy) * _RcpSpeedLimiter);

	// fetch temporal color, its "confidence" weight is stored in alpha
	float4 temp = _InTemporal.SampleLevel(s_LinearSampler, STtoUV(st + velocity.xy), 0);
	float3 temporalColor = temp.rgb;
	float temporalWeight = temp.w;

	// pixel colors are pre-multiplied by their weight to enable bilinear filtering
	// divide by weight to recover color
	temporalColor /= max(temporalWeight, 1e-6);

	// clip the temporal color to the current neighborhood's bounding box.
	// increase the size of the bounding box for stationary（静止的）pixels to 
	// avoid rejecting noisy specular highlights
	temporalColor = ClipColor(temporalColor, boxMin, boxMax, lerp(1.0, 4.0, speedFactor * speedFactor));

	// update the confidence term based on speed and disocclusion
	// ret step(y, x)
	// 	= 1 if the x parameter is greater than or equal to the y parameter; otherwise, 0.
	// temporalWeight 越小，变化越快，之前影响越小
	temporalWeight *= speedFactor * step(compareDepth, temporalDepth);

	// 这是关键，更新temporalColor
	// blend previous color with new color based on confidence
	// confidence steadily grows with each iteration until it is broken by movement
	// such as through disocclusion, color changes, or moving beyond the resolution
	// of the velocity buffer
	// siggraph2014_<<High Quality Temporal Supersampling>>
	// >>tonemap solution
	// - apply before all post
	// - tone map input 
	// - accumulate samples
	// - reverse tone map output
	temporalColor = ITM(lerp(TM(curColor), TM(temporalColor), temporalWeight));

	// update weight
	temporalWeight = saturate(rcp(2.0 - temporalWeight));
	// temporalWeight = 1.0 / (1.0 + RGBToLuminance(temporalColor));	// Karis

	// quantize weight to what is representable
	temporalWeight = f16tof32(f32tof16(temporalWeight));

	// breaking this up into 2 buffers means it can be 40 bits instead of 64
	OutTemporal[st] = float4(temporalColor, 1) * temporalWeight;
}

[RootSignature(Temporal_RootSig)]
[numthreads(8, 8, 1)]
void main( 
	uint3 dispatchThreadId 	: SV_DispatchThreadID,
	uint groupIndex 		: SV_GroupIndex,
	uint3 groupThreadId		: SV_GroupThreadID,
	uint3 groupId 			: SV_GroupID)
{
	const uint ldsHalfPitch = kLdsPitch / 2;

	// prefetch an 16 * 8 tile of pixels (8x8 colors) including a 1 pixel border
	// 10x18 IDs with 4 IDs per thread = 45
	for (uint i = groupIndex; i < 45; i += 64)
	{
		uint X = (i % ldsHalfPitch) * 2;
		uint Y = (i / ldsHalfPitch) * 2;
		uint topLeftIdx = X + Y * kLdsPitch;
        
		float2 uv = _RcpBufferDim * (groupId.xy * uint2(8, 8) * float2(2, 1) + float2(X, Y) + 1);

		// gather
		// w - z
		// x - y
		// depth
		float4 depths = _CurDepth.Gather(s_LinearSampler, uv);
		ldsDepth[topLeftIdx + 0] = depths.w;
		ldsDepth[topLeftIdx + 1] = depths.z;
		ldsDepth[topLeftIdx + kLdsPitch] = depths.x;
		ldsDepth[topLeftIdx + kLdsPitch + 1] = depths.y;

		// color
		float4 r4 = _InColor.GatherRed(s_LinearSampler, uv);
		float4 g4 = _InColor.GatherGreen(s_LinearSampler, uv);
		float4 b4 = _InColor.GatherBlue(s_LinearSampler, uv);
		StoreRGB(topLeftIdx, float3(r4.w, g4.w, b4.w));
		StoreRGB(topLeftIdx + 1, float3(r4.z, g4.z, b4.z));
		StoreRGB(topLeftIdx + kLdsPitch, float3(r4.x, g4.x, b4.x));
		StoreRGB(topLeftIdx + kLdsPitch + 1, float3(r4.y, g4.y, b4.y));
	}

	GroupMemoryBarrierWithGroupSync();

	// 考虑 1 border pixel
	uint idx0 = (groupThreadId.x * 2 + 1) + (groupThreadId.y + 1) * kLdsPitch;
	uint idx1 = idx0 + 1;

	float3 boxMin, boxMax;
	GetBBoxForPair(idx0, idx1, boxMin, boxMax);

	uint2 st0 = dispatchThreadId.xy * uint2(2, 1);
	ApplyTemporalBlend(st0, idx0, boxMin, boxMax);

	// uint2 st1 = st0 + 1;		// 记：这里写错，搞得锯齿严重，唉 -20-2-22
	uint2 st1 = st0 + uint2(1, 0);
	ApplyTemporalBlend(st1, idx1, boxMin, boxMax);
}

// SV_GroupIndex = 
//	SV_GroupThreadID.z*dimx*dimy + SV_GroupThreadID.y*dimx + SV_GroupThreadID.x

/**
 * https://docs.microsoft.com/en-us/windows/win32/direct3dhlsl/gather4--sm5---asm-
 * gather4 (sm5 - asm)
 * Gathers the 4 texels that would be used in a bi-linear filtering operation and packs them 
 *into a single register. This instruction only works with 2D or CubeMap textures, including arrays.
 *Only the addressing modes of the sampler are used and the top level of any mip pyramid is used.
 *	注：This instruction behaves like the sample instruction, but a filtered sample is not generated.
 *The 4 samples that would contribute to filering are placed into xyzw in counter clockwise order 
 *starting with the sample to the lower left of the queried location. This is the same as point
 *as point sampling with (u, v) texture coordinate deltas at the following locations: 
 *	(-,+), (+,+), (+,-), (-,-)
 *	w -- z
 * 	x -- y
 *
 * 
 * https://www.khronos.org/opengl/wiki/Sampler_(GLSL) Texture gather access
 * while OpenGL's standard filtering abilities are useful, sometimes it is useful to be able to
 *bypass（避过） filtering altogather.
 *	Gather only fetch a single component from the texture, specified by comp, which defaults to 0.
 *All filtering is ignored for these functions, and they only access texels from the texture's base 
 *mipmap layer
 *	These functions fetch just 1 component at a time, but they return 4 values, as the components of 
 *a 4-element vector. These values represent the nearest 4 texels, in the following order:
 *	X = top-left, Y = top-right, Z = bottom-right, W = bottom-left
 *
 * 
 * https://cloud.tencent.com/developer/article/1151484 - about cuda
 * Texture Gather
 * Texture gather is a special texture fetch that is available for 2-dimensional
 *textures only. It returns 4 32-bit numbers that correspond to the value of 
 *the component 'comp' of each of the 4 texels that would have been used for bilinear
 *filtering during a regular texture fetch. For example, if these texels are of values
 *  (253, 20, 31, 255)
 *  (250, 25, 29, 254)
 *  (249, 16, 37, 253)
 *  (251, 22, 30, 250)
 *and comp is 2, it returns (31, 29, 37, 30)
 */

/**
 * siggraph2014_High Quality Temporal Supersampling, Brian Karis
 * Better tone map solution
 * Tone mapping desaturates bright pixels
 * >weight samples instead based on luminance 	weight = 1 / (1 + luma)
 * - maintains chroma
 * - perceptually closer to ground truth
 * >no need to store the weight
 * - rederive weight 		
 * - save GPRs 
 * T(color) = color / (1 + luma)
 * T^(-1)(color) = color / (1 - luma)
 */

/**
 * siggraph2014_High Quality Temporal Supersampling, Brian Karis
 * Temporal Reprojection Antialiasing in INSIDE
 */