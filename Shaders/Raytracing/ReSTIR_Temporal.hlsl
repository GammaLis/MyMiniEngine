// Ref: kajiya
#include "ReSTIRGICommon.hlsl"

int2 GetRandomPixelOffset(uint sampleIndex, uint frameIndex)
{
	const int2 Offsets[4] = {
		int2(-1, -1), int2( 1,  1),
		int2(-1,  1), int2( 1, -1)
	};
	const int2 baseOffset = Offsets[frameIndex & 3] + Offsets[(sampleIndex + (frameIndex ^ 1)) & 3];
	return baseOffset;
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(uint2 dtid : SV_DispatchThreadID)
{
	const uint2 launchIndex = dtid;
	const uint2 launchDimensions = (uint2)_View.BufferSizeAndInvSize.xy;
	const uint2 pixel = GetFramePixel(launchIndex);

	const uint2 BufferSize = _View.BufferSizeAndInvSize.xy;
	
	// Outputs
	uint2  outReservoirCoord = dtid;
	float3 outRayOrigin = 0;
	float3 outSampleDir = 0;
	float3 outSampleRadiance = 0;
	float3 outSamplePos = 0;
	float3 outSampleNormal = 0;
	
	RWRayOrigin[dtid] = outRayOrigin;
	RWSampleRadiance[dtid] = outSampleRadiance;
	RWSampleNormal[dtid] = float4(outSampleNormal * 0.5 + 0.5, -dot(outSampleNormal, outSampleDir));
	RWSampleHitInfo[dtid] = 0;
	RWReservoir[dtid] = 0;

	if (any(pixel >= BufferSize))
		return;
	
	float depth = _TexDepth[pixel];
	if (depth == 0)
	{
		// ...
		return;
	}
	
	float sceneDepth = LinearEyeDepth(depth);
	
	// Initialize random number generator
	RngStateType rngState = InitRNG(launchIndex, launchDimensions, _View.FrameIndex);
	
	float2 offset = 0.5f;
#if 0
	offset = float2(Rand(rngState), Rand(rngState));
#endif
	
	float2 svPosition = pixel + offset;
	float2 uv = svPosition * _View.BufferSizeAndInvSize.zw;
	float2 posCS = (uv - 0.5f) * float2(2.0f, -2.0f);
	float3 posWS = mul(float4(posCS * sceneDepth, -sceneDepth, 1.0f), _View.ScreenToWorldMatrix).xyz;
	float3 normalWS = _TexNormal[pixel].xyz;
	
	float3 hitRadiance = _SampleRadiance[dtid];
	float3 hitNormal = _SampleNormal[dtid];
	float4 hitInfo = _SampleHitInfo[dtid];
	float3 hitOffset = hitInfo.xyz;
	float3 hitPosWS = posWS + hitOffset;
	float3 L = SafeNormalize(hitOffset.xyz);
	float  sampleInvPdf = hitInfo.w;

	outRayOrigin = posWS;
	outSampleDir = L;
	outSampleRadiance = hitRadiance;
	outSamplePos = hitPosWS;
	outSampleNormal = hitNormal;
	
	// Init candidate reservoir
	float targetPdf = Luminance(hitRadiance) * max(0, dot(L, normalWS));
	float sampleWeight = targetPdf * sampleInvPdf;
	FReservoir r = CreateGIReservoir();
	r.payload = (dtid.x | dtid.y << 16);
	r.targetPdf = targetPdf;
	r.M = sampleWeight > 0 ? 1 : 0;
	r.W = sampleWeight;

	// DEBUG: no temporal reuse, MaxSampleCount = 0
	const uint MaxSampleCount = TEMPORAL_USE_PERMUTATIONS ? 5 : 1;

	// Temporal reuse
	[unroll]
	for (uint si = 0; si < MaxSampleCount; ++si) 
	{
	#if 0
		int2 randomOffset = GetRandomPixelOffset(si, _View.FrameIndex);
	#else
		int2 randomOffset = 0;
	#endif
		
		float3 velocity = UnpackVelocity(_TexVelocity[pixel + randomOffset]);
	#if 0
		// Can't use linear interpolation, but we can interpolate stochastically instead
		float2 reprojRandOffset = float2(Rand(rngState), Rand(rngState)) - 0.5;
	#else
		// or not at all
		float2 reprojRandOffset = 0;
	#endif
		// TODO:
		int2 reprojectedPos = (pixel + velocity.xy) * 0.5 + reprojRandOffset + 0.0; // +0.5 ->broken
		reprojectedPos += randomOffset;
		
		int2 reprojectedPixel = 2 * reprojectedPos + s_PixelOffsets[_View.FrameIndex & 3];

		if (all(reprojectedPos >= 0 && 2 * reprojectedPos < (int2) BufferSize))
		{
			uint2 rPacked = _TemporalReservoir[reprojectedPos];
			FReservoir rTemporal = CreateGIReservoir(rPacked);
			uint2 prevReservoirPos = ReservoirPayloadToPixel(rTemporal.payload);

			float visibility = 1.0;
			float relevance = 1.0;

			float sampleDepth = _TexDepth[reprojectedPixel];
			float3 prevRayOrigin = _TemporalRayOrigin[prevReservoirPos];
			float deltaRayOrigin = length(prevRayOrigin - posWS);
			if (sampleDepth == 0 || deltaRayOrigin > 0.1 * sceneDepth)
				continue;

			relevance *= 1.0 - smoothstep(0.0, 0.1, InverseDepthRelativeDiff(depth, sampleDepth));
			float3 sampleNormal = _TexNormal[reprojectedPixel].xyz;
			float  normalSimilarity = max(0, dot(sampleNormal, normalWS));

			// Increase noise, but prevents leaking in areas of geometric complexity
		#if 1
			// High cutoff seems unnecessary. Prev 0.9.
			const float normalCutoff = 0.2;
			if (si != 0 && normalSimilarity < normalCutoff)
				continue;
		#endif
			relevance *= normalSimilarity;

			const float4 prevSamplePosAndT = _TemporalHitInfo[prevReservoirPos] + float4(prevRayOrigin, 0);
			const float3 prevSamplePos = prevSamplePosAndT.xyz;
			const float  prevSampleT = prevSamplePosAndT.w;
			float4 prevSampleNormal = _TemporalSampleNormal[prevReservoirPos];
			prevSampleNormal.xyz = SafeNormalize(prevSampleNormal.xyz * 2.0 - 1.0); // ???
			const float4 prevSampleRadiance = _TemporalSampleRadiance[prevReservoirPos];

			const float3 currToPrevSampleVec = prevSamplePos - posWS;
			const float  currToPrevSampleT = length(currToPrevSampleVec);
			const float3 currToPrevSampleDir = SafeNormalize(currToPrevSampleVec);
			const float  currToPrevSampleDot = -dot(prevSampleNormal.xyz, currToPrevSampleDir);

			// Also doing this for sample 0, as under extreme aliasing we can easily get bad samples in
			if (dot(currToPrevSampleDir, normalWS) < 1e-3)
				continue;

			// Ref: ReSTIR pater
			// With temporal reuse, the number of candidates M contributing to the pixel can in theory grow unbounded,
			// as each frame always combines its reservoir with the previous frame's. This  causes (potentially stale)
			// temporal samples to be weighted disproportionately high during resampling. To fix this, we simply 
			// clamp the previous frame's M to at most 20x of the current frame's reservoir's M
			rTemporal.M = min(rTemporal.M, RESTIR_TEMPORAL_M_CLAMP);

			float prevSamplePdf = Luminance(prevSampleRadiance.rgb) * max(0, dot(currToPrevSampleDir, normalWS.xyz));

			float jacobian = 1.0;
		#if 0
			{
				// Distance falloff, needed to avoid leaks.
				jacobian *= clamp(prevSampleT / currToPrevSampleT, 1e-4, 1e4);
				jacobian *= jacobian;

				// dot(hitN, -L), neede to avoid leaks, without it, light "hugs" corners.
				// TODO...
				jacobian *= clamp(currToPrevSampleDot / prevSampleNormal.w);
			}
			// Fixes boiling artifacts near edges. Unstable jacobians, but also effectively reduces reliance on
			// reservoir exchange in tight corners, which is desirable since the well-distributed raw samples 
			// thrown at temporal filters will do better.
			// TODO...
			if (USE_JACOBIAN_BASED_REJECTION)
			{
				// Clamp neighbors give us a hit point that's considerably easier to sample from our own position
				// than from the neighbor. This can cause some darkening, but prevents fireflies.
				
			#if 1
				// Doesn't over-darken corners as much
				jacobian = min(jacobian, JACOBIAN_BASED_REJECTION_VALUE);
			#else

			#endif
			}
		#endif // Jacobian

			rTemporal.M *= relevance;

			bool bCombined = CombineReservoirs(r, rTemporal, Rand(rngState), jacobian*visibility, prevSamplePdf);
			if (bCombined)
			{
				outReservoirCoord = reprojectedPos;
				outSampleDir = currToPrevSampleDir;
				outSampleRadiance = prevSampleRadiance.rgb;
				outSampleNormal = prevSampleNormal.xyz;
				outSamplePos = prevSamplePos;
			}

		}
	}
	FinalizeReservoir(r);
	// r.W = min(r.W, RESTIR_RESERVOIR_W_CLAMP);
	
	RWRayOrigin[dtid] = outRayOrigin;
	RWSampleRadiance[dtid] = outSampleRadiance;
	RWSampleNormal[dtid] = float4(outSampleNormal * 0.5 + 0.5, -dot(outSampleNormal, outSampleDir));
	float3 sampleDir = outSamplePos - outRayOrigin;
	RWSampleHitInfo[dtid] = float4(sampleDir, length(sampleDir));
	RWReservoir[dtid] = r.Pack();
}
