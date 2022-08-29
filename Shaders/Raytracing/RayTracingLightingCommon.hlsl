#ifndef RAYTRACING_LIGHTING_COMMON
#define RAYTRACING_LIGHTING_COMMON

// Ref: UE - RayTracingLightingCommon.ush

#include "RayTracingCommon.hlsl"
#include "Reservoir.hlsl"

#define LIGHT_TYPE_DIRECTIONAL (-1)
#define LIGHT_TYPE_POINT 0
#define LIGHT_TYPE_SPOT 1
#define LIGHT_TYPE_MAX 2

#define MAX_LIGHTS 32

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

	uint lightIndex = uint(Rand(rngState) * MAX_LIGHTS);
	light = _LightBuffer[lightIndex];

	// Pdf of uniform distribution is (1 / LightCount). Reciprocal of that PDF (simply LightCount) is a weight of this sample
	sampleWeight = float(MAX_LIGHTS);
	
	return true;
}

bool SampleLightUniform(inout RngStateType rngState, float3 position, float3 normal, out int lightIndex, out float sampleWeight)
{
	// if (_LightCount == 0) return false;

	lightIndex = int(Rand(rngState) * MAX_LIGHTS);

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
			if (dot(normal, L) < 0.0001f)
				continue;

		#if SHADOW_RAY_IN_RIS
			// Casting a shadow ray for all candidates here is expensive, but can significantly decrease noise
			if (!CastShadowRay(position, normal, L, lightDistance))
				continue;
		#endif

			float3 lightIntensity = GetLightIntensity(candidate, position);
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
bool SampleLightRIS(inout RngStateType rngState, float3 position, float3 normal, inout PackedReservoir r)
{
	int lightIndex = 0;
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
			if (dot(normal, L) < 0.0001f)
				continue;

		#if SHADOW_RAY_IN_RIS
			if (!CastShadowRay(position, normal, L, lightDistance))
				continue;
		#endif

			float3 lightIntensity = GetLightIntensity(candidate, position);
			float candidatePdfG = Luminance(lightIntensity);
			const float candidateRISWeight = candidatePdfG * candidateWeight;

			totalWeights += candidateRISWeight;
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
	r.M = RIS_CANDIDATES_LIGHTS;

	return totalWeights > 0;
}

bool SampleLightReSTIR(inout RngStateType rngState, uint2 pixel, float3 position, float3 normal, inout PackedReservoir r)
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

	PackedReservoir prevReservoir = GetReservoir(uint2(prevPixel), _Dynamics.resolution);
	// Clamp the previous frame's M to at most 20x of the current frame's reservoir's M
	float MMax = r.M * 20;
	if (prevReservoir.M > MMax)
	{
		prevReservoir.Weights *= MMax / prevReservoir.M;
		prevReservoir.M = MMax;
	}
	CombineReservoirs(r, prevReservoir, rngState);
	bValid = r.Weights > 0;
#endif

	return bValid;
}

#endif // RAYTRACING_LIGHTING_COMMON
