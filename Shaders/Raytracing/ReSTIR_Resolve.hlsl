// Ref: kajiya
#include "ReSTIRGICommon.hlsl"
#if USE_RESOLVE_SPATIAL_FILTER
#include "../MonteCarlo.hlsl"
#endif

#define OUT_IRRADIANCE RWSampleRadiance

// 128 - SVGF paper, 512 - AMD
static const float s_NormalSigma = 128.0f; // 512.0f;
static const float s_DepthSigma = 4.0f;

float GetEdgeStoppingNormalWeight(float3 normal, float3 sampleNormal)
{
	return pow(saturate(dot(normal, sampleNormal)), s_NormalSigma);
}

float GetEdgeStoppingDepthWeight(float depth, float sampleDepth)
{
	return exp(-abs(depth - sampleDepth) * depth * s_DepthSigma);
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(uint2 dtid : SV_DispatchThreadID)
{
	const uint2 pixel = dtid;
	const uint2 BufferSize = (uint2)_View.BufferSizeAndInvSize.xy;
	const uint2 HalfBufferSize = BufferSize / 2;

	if (any(pixel >= BufferSize))
	{
		OUT_IRRADIANCE[dtid] = 0;
		return;
	}

	float depth = _TexDepth[pixel];
	if (depth == 0)
	{
		OUT_IRRADIANCE[dtid] = 0;
		return;
	}
	
	float sceneDepth = LinearEyeDepth(depth);

	// Initialize random number generator
	RngStateType rngState = InitRNG(pixel, BufferSize, _View.FrameIndex);

	float2 offset = 0.5f;	
	float2 svPosition = pixel + offset;
	float2 uv = svPosition * _View.BufferSizeAndInvSize.zw;
	float2 posCS = (uv - 0.5f) * float2(2.0f, -2.0f);
	float3 posWS = mul(float4(posCS * sceneDepth, -sceneDepth, 1.0f), _View.ScreenToWorldMatrix).xyz;
	float3 normalWS = _TexNormal[pixel].xyz;

	const uint pixelIndexInQuad = ((pixel.x & 1) | ((pixel.y & 1) * 2) + JenkinsHash(_View.FrameIndex)) & 3;

	float3 totalIrradiance = 0;
	{
		float weightSum = 0;
		float3 weightedIrradiance = 0;
		const uint SampleCount = USE_RESOLVE_SPATIAL_FILTER ? 4 : 1;
		[unroll]
		for (uint si = 0; si < SampleCount; ++si)
		{
			float radius = si == 0 ? 0 : 1.0;
			float2 offset = UniformSampleDisk(float2(Rand(rngState), Rand(rngState))) * radius;
			float2 neighborPos = pixel + offset;
			int2 neighborPixel = int2(neighborPos);
			int2 loadPosition = int2(neighborPos * 0.5); // int2(float2(pixel) * 0.5 + offset);
			
			if (any(neighborPixel < 0 || neighborPixel >= (int2) BufferSize))
				continue;
			
			float neighborDepth = _TexDepth[neighborPixel];
			float3 neighborNormal = _TexNormal[neighborPixel].xyz;
			if (neighborDepth == 0)
				continue;
			
			float neighborSceneDepth = LinearEyeDepth(neighborDepth);
			
			uint2 rPacked = _TemporalReservoir[loadPosition];
			FReservoir r = CreateGIReservoir(rPacked);
			
			const float3 rayOrigin = _TemporalRayOrigin[loadPosition].xyz;
			const float4 samplePosAndT = _TemporalHitInfo[loadPosition] + float4(rayOrigin, 0);
			const float3 samplePos = samplePosAndT.xyz;
			const float3 sampleRadiance = _TemporalSampleRadiance[loadPosition].rgb;
			
			const float3 currToSampleVec = samplePos - posWS;
			const float  currToSampleDist = length(currToSampleVec);
			const float3 currToSampleDir = currToSampleVec / max(currToSampleDist, RESERVOIR_EPS);
			const float  sampleDot = saturate(dot(currToSampleDir, normalWS));
			
			float w = 1.0f;
		#if 1
			w *= exp2(-200.0 * abs(neighborSceneDepth / sceneDepth - 1.0));
			w *= exp2(-20.0 * (1.0 - saturate(dot(neighborNormal, normalWS))));
		#else
			w *= GetEdgeStoppingDepthWeight(depth, neighborDepth);
			w *= GetEdgeStoppingNormalWeight(normalWS, neighborNormal);
		#endif
			
			float3 contrib = sampleRadiance * sampleDot * r.W;
			
			weightedIrradiance += contrib * w;
			weightSum += w;
		}
		
		totalIrradiance = weightedIrradiance / max(weightSum, RESERVOIR_EPS);
	}

	// Use `RWSamspleRadiance`, actually should be `RWIrradiance`
	OUT_IRRADIANCE[dtid] = totalIrradiance;
}
