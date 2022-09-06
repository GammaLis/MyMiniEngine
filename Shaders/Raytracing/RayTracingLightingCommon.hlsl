#ifndef RAYTRACING_LIGHTING_COMMON
#define RAYTRACING_LIGHTING_COMMON

// Ref: UE - RayTracingLightingCommon.ush

#include "RayTracingCommon.hlsl"
#include "Reservoir.hlsl"

#define LIGHT_TYPE_DIRECTIONAL (-1)
#define LIGHT_TYPE_POINT 0
#define LIGHT_TYPE_SPOT 1
#define LIGHT_TYPE_MAX 2

#define MAX_LIGHTS 64

#ifndef RIS_CANDIDATES_LIGHTS
#define RIS_CANDIDATES_LIGHTS 8
#endif

#ifndef SHADOW_RAY_IN_RIS
#define SHADOW_RAY_IN_RIS 0
#endif

LightData GetSunLight()
{
	LightData sunLight = (LightData) 0;
	sunLight.color = _SunColor;
	sunLight.pos = -_SunDirection; // direction from sun
	sunLight.type = LIGHT_TYPE_MAX;
	return sunLight;
}

LightData GetRayTracingLight(uint lightIndex)
{
	lightIndex = min(lightIndex, MAX_LIGHTS - 1);
	return _LightBuffer[lightIndex];
}

// Returns intensity of given light at specified distance
float3 GetLightIntensity(LightData light, float3 position)
{
	if (light.type >= LIGHT_TYPE_MAX) // atm directional light
		return light.color;

	float3 lightVec = light.pos - position;
	float  lightDistSqr = dot(lightVec, lightVec);
	float  invLightDist = rsqrt(max(lightDistSqr, 0.001f));
	float3 L = lightVec * invLightDist;

	float falloff = 1.0;
		
	// Distance falloff
	// 0. Simple attenuation by inverse square root of distance
#if 0
	float distanceFalloff = max(0, light.radiusSq * rcp(lightDistSqr));

#elif 0
	// modify 1/d^2 * R^2 to fall off at a fixed radius
	// (R/d)^2 - d/R = [(1/d^2) - (1/R^2)*(d/R)] * R^2
	float distanceFalloff = light.radiusSq * (invLightDist * invLightDist);
	distanceFalloff = max(0, distanceFalloff - rsqrt(distanceFalloff));

#else
	// Cem Yuksel's improved attenuation avoding singularity at distance=0
	// Ref: http://www.cemyuksel.com/research/pointlightattenuation/
	const float radius = 0.5f; // we hardcode radius at 0.5, but this should be a light parameter
	const float radiusSqr = radius * radius;
	float distanceFalloff = 2.0f * light.radiusSq / (lightDistSqr + radiusSqr + sqrt(lightDistSqr) * sqrt(lightDistSqr + radiusSqr));
#endif

	falloff *= distanceFalloff;

	// coneAngles
	if (light.type == LIGHT_TYPE_SPOT)
	{
		float coneFalloff = dot(-light.coneDir, L);
		coneFalloff = saturate((coneFalloff - light.coneAngles.y) * light.coneAngles.x);

		falloff *= coneFalloff;
	}

	return light.color * falloff;
}

// Samples a random light from the pool of all lights using simplest uniform distribution
bool SampleLightUniform(inout RngStateType rngState, float3 position, float3 normal, out LightData light, out float sampleWeight)
{
	// if (_LightCount == 0) return false;

	int lightIndex = min(int(Rand(rngState) * MAX_LIGHTS), MAX_LIGHTS-1);
	light = _LightBuffer[lightIndex];

	// Pdf of uniform distribution is (1 / LightCount). Reciprocal of that PDF (simply LightCount) is a weight of this sample
	sampleWeight = float(MAX_LIGHTS);
	
	return true;
}

bool SampleLightUniform(inout RngStateType rngState, float3 position, float3 normal, out int lightIndex, out float sampleWeight)
{
	// if (_LightCount == 0) return false;

	lightIndex = min(int(Rand(rngState) * MAX_LIGHTS), MAX_LIGHTS-1);

	// Pdf of uniform distribution is (1 / LightCount). Reciprocal of that PDF (simply LightCount) is a weight of this sample
	sampleWeight = float(MAX_LIGHTS);
	
	return true;
}

// SampleLightUniform a random light from the pool of all lights using RIS (Resampled Importance Sampling)
bool SampleLightRIS(inout RngStateType rngState, float3 position, float3 normal, out LightData light, out float sampleWeight)
{
	light = (LightData) 0;
	float totalWeights = 0.0f;
	float samplePdfG = 0.0f;

	for (uint i = 0; i < RIS_CANDIDATES_LIGHTS; ++i)
	{
		float candidateWeight;
		LightData candidate;
		bool bValidLight = SampleLightUniform(rngState, position, normal, candidate, candidateWeight);
		if (bValidLight)
		{
			float3 lightVector = candidate.pos - position;
			float lightDistance = length(lightVector);

			// Ignore backfacing light
			float3 L = lightVector / max(0.01f, lightDistance);
			float NdotL = dot(normal, L);
			if (NdotL < 0.0001f)
				continue;

		#if SHADOW_RAY_IN_RIS
			// Casting a shadow ray for all candidates here is expensive, but can significantly decrease noise
			if (!CastShadowRay(position, normal, L, lightDistance))
				continue;
		#endif

			float3 lightIntensity = GetLightIntensity(candidate, position) * NdotL;
			float candidatePdfG = Luminance(lightIntensity);
			const float candidateRISWeight = candidatePdfG * candidateWeight;

			totalWeights += candidateRISWeight;
			if ((Rand(rngState) * totalWeights) < candidateRISWeight)
			{
				light = candidate;
				samplePdfG = candidatePdfG;
			}
		}
	}

	if (totalWeights == 0)
	{
		return false;
	}
	else 
	{
		sampleWeight = (totalWeights / float(RIS_CANDIDATES_LIGHTS)) / samplePdfG;
		return true;
	}
}

// SampleLightUniform a random light from the pool of all lights using RIS (Resampled Importance Sampling)
bool SampleLightRIS(inout RngStateType rngState, float3 position, float3 normal, inout Reservoir r)
{
	int lightIndex = -1;
	int M = 0;
	float totalWeights = 0.0f;
	float samplePdfG = 0.0f;

	// Update reservoir
	for (uint i = 0; i < RIS_CANDIDATES_LIGHTS; ++i)
	{
		float candidateWeight;
		int candidateIndex = -1;
		bool bValidLight = SampleLightUniform(rngState, position, normal, candidateIndex, candidateWeight);
		if (bValidLight)
		{
			LightData candidate = _LightBuffer[candidateIndex];
			float3 lightVector = candidate.pos - position;
			float lightDistance = length(lightVector);

			// Ignore backfacing light
			float3 L = lightVector / max(0.01f, lightDistance);
			float NdotL = dot(normal, L);
			if (NdotL < 0.0001f)
				continue;

		#if SHADOW_RAY_IN_RIS
			if (!CastShadowRay(position, normal, L, lightDistance))
				continue;
		#endif

			float3 lightIntensity = GetLightIntensity(candidate, position) * NdotL;
			float candidatePdfG = Luminance(lightIntensity);
			const float candidateRISWeight = candidatePdfG * candidateWeight;

			totalWeights += candidateRISWeight;
			++M;
			if (Rand(rngState) * totalWeights < candidateRISWeight)
			{
				lightIndex = candidateIndex;
				samplePdfG = candidatePdfG;
			}
		}
	}

	r.LightData = lightIndex;
	r.TargetPdf = samplePdfG;
	r.Weights = totalWeights;
	r.M = RIS_CANDIDATES_LIGHTS; // M
#if RESTIR_WEIGHT_NORMALIZATION
	FinalizeResampling(r);
	r.M = 1;
#endif

	return r.LightData >= 0;
}

bool SampleLightReSTIR(inout RngStateType rngState, uint2 pixel, float3 position, float3 normal, inout Reservoir r)
{
	bool bValid = SampleLightRIS(rngState, position, normal, r);
	// Evalutate visibility for initial candidates
	if (bValid)
	{
		// Prepare data needed to evaluate the light
        LightData light = _LightBuffer[r.LightData];
		float3 lightVec = light.pos - position;
        float  lightDist = length(lightVec);
		float3 L = lightVec / max(0.01f, lightDist);

		if (!SHADOW_RAY_IN_RIS && !CastShadowRay(position, normal, L, lightDist))
		{
			r.Weights = 0;
			r.LightData = -1;
			bValid = false;
		}
	}

#if 1
	// Temporal reuse

#if 1
	float3 velocity = UnpackVelocity(_TexVelocity[pixel]);
	float2 prevPixel = float2(pixel + 0.5) + velocity.xy;
	if (any(prevPixel < 0) || any(prevPixel >= _Dynamics.resolution))
		return bValid;
#else
	float2 prevPixel = pixel;
#endif

	Reservoir prevReservoir = GetReservoir(uint2(prevPixel), _Dynamics.resolution);
	// Clamp the previous frame's M to at most 20x of the current frame's reservoir's M
	float MMax = r.M * 20;
	if (prevReservoir.M > MMax)
	{
		prevReservoir.Weights *= MMax / prevReservoir.M;
		prevReservoir.M = MMax;
	}
	CombineReservoirs(r, prevReservoir, Rand(rngState), r.TargetPdf);
	bValid = r.LightData >= 0; // r.Weights > 0;
#endif

	return bValid;
}


// Ref: RTXDI

// Compares 2 values and returns true if their relative difference is lower than the threshold
// 0 or negative threshold makes test always succeed, not fail
bool CompareRelativeDifference(float reference, float candidate, float threshold)
{
	return (threshold <= 0) || abs(reference - candidate) <= threshold * max(reference, candidate);
}

// Spatio-temporal resampling
// A combination of the temporal and spatial passes that operates only on the previous frame reservoirs.
Reservoir SampleLightSpatiotemporal(inout RngStateType rngState, Reservoir curSample, uint2 pixel, float3 position, float3 normal)
{
	Reservoir stReservoir = CreateReservoir();

	const int MMax = min(s_MaxM, curSample.M * 20);

	// float  surfaceDepth = _TexDepth[pixel];
	// float3 surfaceNormal = _TexNormal[pixel].xyz * 2.0 - 1.0;

	// stReservoir = curSample;
	CombineReservoirs(stReservoir, curSample, 0.5, curSample.TargetPdf);

	int2 temporalPixelPos = int2(-1, -1);
	// Backproject this pixel to last frame
#if 1
    float3 velocity = UnpackVelocity(_TexVelocity[pixel]);
    float2 reprojectedPos = float2(pixel + 0.5) + velocity.xy;
    int2 prevPos = reprojectedPos;
#else
	int2 prevPos = pixel;
#endif

	int i = 0;
	int2 pos;
	int2 spatialOffset = 0;

	// Walk the specified number of neighbors, resampling using RIS
	// for (i = 0; i < 2; ++i)
	{
		if (i == 0)
		{
			spatialOffset = 0;
			pos = prevPos + spatialOffset;
		}
		else
		{
			spatialOffset = (float2(Rand(rngState), Rand(rngState)) - 0.5) * 10;
			pos = prevPos + spatialOffset;
		}

		if (IsValidScreenSample(pos))
		{
			// TODO: depth & normal differences
			float  sampleDepth = _TexDepth[pos].x;
			float3 sampleNormal = _TexNormal[pos].xyz * 2.0 - 1.0;

			Reservoir sample = GetReservoir(pos, _Dynamics.resolution);
			sample.M = min(sample.M, MMax);

			float sampleWeight = 0;
			if (sample.LightData >= 0)
			{
				LightData sampleLight = _LightBuffer[sample.LightData];
				float3 lightVector = sampleLight.pos - position;
				float  lightDistance = length(lightVector);
				float3 L = lightVector / max(0.01f, lightDistance);
				float  NdotL = saturate(dot(normal, L));

			#if SHADOW_RAY_IN_RIS
				if (!CastShadowRay(position, normal, L, lightDistance))
					NdotL = 0;
			#endif

				float3 lightIntensity = GetLightIntensity(sampleLight, position) * NdotL;
				sampleWeight = Luminance(lightIntensity);
			}

			CombineReservoirs(stReservoir, sample, Rand(rngState), sampleWeight);
		}
	}

#if RESTIR_WEIGHT_NORMALIZATION
	FinalizeResampling(stReservoir);
#endif
	
	return stReservoir;
}

#endif // RAYTRACING_LIGHTING_COMMON
