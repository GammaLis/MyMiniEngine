// Ref: <<RayTracingGems II>>
// - Chapter 14 "The Reference  Path Tracer"
// https://github.com/boksajak/referencePT

#define USE_PCG 1

// Defines after how many bounces will be the Russian Roulette applied
#define MIN_BOUNCES 3

// Number of candidates 
#define RIS_CANDIDATES_LIGHTS 8

// Enable this to cast shadow rays for each candidate during resampling. This is expensive but increases quality
#define SHADOW_RAY_IN_RIS 0

#include "RayTracingCommon.hlsl"
#include "RayTracingIntersection.hlsl"
#include "PathTracingCommon.hlsl"
#include "RayTracingLightingCommon.hlsl"
#include "../OctahedralCommon.hlsl"
#include "ShaderPassPathTracing.hlsl"

// #define DEBUG 1

MaterialProperties GetMaterialProperties(FHitInfo payload)
{
    MaterialProperties material = (MaterialProperties)0;
	material.baseColor = payload.color;
	material.opacity = 1.0f;
	material.roughness = 0.5f;
	return material;
}

// Calculate probability of selecting BRDF (specular or diffuse) using the approximate Fresnel term
float GetBRDFProbability(MaterialProperties material, float3 V, float3 shadingNormal)
{
	// Evaluate Fresnel term using the shading normal
    // Note: we use the shading normal instead of the microfacet normal (half-vector) for Fresnel term here.
    // That's suboptimal for rough surfaces at grazing angles, but half-vector is yet unknown at this point
	float specularF0 = Luminance(BaseColorToSpecularF0(material.baseColor, material.metallic));
	float diffuseReflectance = Luminance(BaseColorToDiffuseReflectance(material.baseColor, material.metallic));
	float Fresnel = saturate(Luminance(EvalFresnel(specularF0, ShadowedF90(specularF0), max(0.0f, dot(V, shadingNormal)))));

    // Approximate relative contribution of BRDFs using the Fresnel term
	float specular = Fresnel;
	float diffuse = diffuseReflectance * (1.0f - Fresnel); // if diffuse term is weighted by Fresnel, apply it here as well

    // Return probability of selecting specular BRDF over diffuse BRDF
	float p = (specular / max(0.0001f, (specular + diffuse)));

    // Clamp probability to avoid undersampling of less prominent BRDF
	return clamp(p, 0.1f, 0.9f);
}

/**
 * A Monte Carlo Unidirectional Path Tracer
 * > Path tracing simulates how light energy moves through an environment (light transport) by constructing `paths` between
 * light sources and the virtual camera. A path is the combination of several ray segments. Ray segments are the connections
 * between the camera, surfaces in environment, and/or light sources.
 * > Unidirectional means paths are constructed in a single (macro) direction. Paths can start at the camera and move toward
 * light sources or vice versa.
 * > Monte Carlo algorithm use random sampling to approximate difficult or intractable integrals. In a path tracer,
 * the approximated integral is the rendering equation, and tracing more paths (a) increases the accuracy of the integral
 * approximation and (b) produces a better image. Since paths are costly to compute, we accumulate the resulting color from
 * each path over time to construct the final image.
 */
[shader("raygeneration")]
void RayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDimensions = DispatchRaysDimensions().xy;

    // Initialize random number generator
    RngStateType rngState = InitRNG(launchIndex, launchDimensions, uint(_Dynamics.accumulationIndex));

    float2 pixel = float2(DispatchRaysIndex().xy);
    const float2 resolution = float2(DispatchRaysDimensions().xy);

    // TODO...
    // Add a random offset to the pixel's screen coordinates
	float2 offset = 0.5f;
	if (_Dynamics.accumulationIndex >= 0)
		offset = float2(Rand(rngState), Rand(rngState));

    pixel = (pixel + offset) / resolution * 2.0f - 1.0f;
    pixel.y = -pixel.y;

    /**
     * The inverse view-projection matrix can be applied to "unproject" points on screen from
     * normalized device coordinate space. This is not recommended, however, as near and far plane
     * settings stored in the projection matrix cause numerical precision issues when the transformation 
     * is reversed.
     */
	float4 unprojected = mul(float4(pixel, 0, 1), _Dynamics.cameraToWorld);
    float3 world = unprojected.xyz / unprojected.w;
	float3 origin = origin = _Dynamics.worldCameraPosition.xyz;
    float3 direction = normalize(world - origin);

    RayDesc ray;
    ray.Origin = _Dynamics.worldCameraPosition.xyz;
    ray.TMin = 0.0f;
    ray.TMax = FLT_MAX;
    ray.Direction = direction;

    FHitInfo payload = (FHitInfo)0;
    payload.T = -1;
    payload.materialID = -1;
	payload.color = 0;

    // Initialize path tracing data
    // Radiance is the final intensity of the light energy presented on screen for a given path
    float3 radiance = float3(0, 0, 0);
    // Throughput represents the amount of energy that may be transferred along a path's ray segment
    // after interacting with a surface. Every time the path intersects a surface, the throughput is 
    // attenuated based on the properties of the intersected surface (dictated by the material BRDF 
    // at the point on the surface). When a path arrives at a light source (i.e. an emissive surface), 
    // the throughput is `multiplied` by the intensity of the light source and added to the radiance.
    // To properly attenuate the ray throughput, we importance-sample the material's BRDF lobes by
    // evaluating the `brdf` and `brdfPdf` values.
    float3 throughput = float3(1, 1, 1);

    const uint MaxBounces = max(_MaxBounces, 1);
    // Start the ray tracing loop
    for (uint bounce = 0; bounce < MaxBounces; ++bounce)
    {
        // Trace the ray
        TraceRay(
            g_Accel,
            RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
            0xFF,
            STANDARD_RAY_INDEX, 
            1,
            STANDARD_RAY_INDEX,
            ray,  
            payload);
        // On a smiss, load the sky value and break out of the ray tracing loop
        if (!payload.HasHit())
        {
            radiance += throughput * _Dynamics.backgroundColor.rgb;
            break;
        }

        // Decode normals and flip them towards the incident ray direction (needed for backfacing triangles)
		float3 geometryNormal = DecodeNormalOctahedron(payload.encodedNormals.xy);
		float3 shadingNormal = DecodeNormalOctahedron(payload.encodedNormals.zw);
        float3 hitPosition = payload.hitPosition;

		float3 V = -ray.Direction;
		if (dot(geometryNormal, V) < 0.0)
			geometryNormal = -geometryNormal;
		if (dot(geometryNormal, shadingNormal) < 0.0f)
			shadingNormal = -shadingNormal;

        // MaterialInputs
		MaterialProperties material = GetMaterialProperties(payload);

        // Account for emissive surfaces
		radiance += throughput * material.emissive;

        // Evalute sun light
#if 1
		LightData sunLight = GetSunLight();
		float3 LSun = -sunLight.pos;
		if (CastShadowRay(hitPosition, geometryNormal, LSun, FLT_MAX))
        {
			radiance += throughput * EvalCombinedBRDF(shadingNormal, LSun, V, material) * GetLightIntensity(sunLight, hitPosition);
		}
#endif

        // >> DEBUG
		// radiance = geometryNormal * 0.5f + 0.5f;
		// radiance = saturate(material.baseColor);
        // <<< 

        // Evaluate punctual light
#if 1
        // Evalute direct light (next event estimation), start by sampling one light
        LightData light;
		float lightWeight;
    #if 0
		bool bValidLightSample = SampleLightUniform(rngState, hitPosition, geometryNormal, light, lightWeight);
    #else
        bool bValidLightSample = SampleLightRIS(rngState, hitPosition, geometryNormal, light, lightWeight);
    #endif
		// bValidLightSample = false;
        if (bValidLightSample)
        {
            // Prepare data needed to evaluate the light
            float3 lightVec = light.pos - hitPosition;
            float  lightDist = length(lightVec);
			float3 L = lightVec / max(0.01f, lightDist);

			if (SHADOW_RAY_IN_RIS || CastShadowRay(hitPosition, geometryNormal, L, lightDist))
			{
				// If the light is not in shadow, evaluate BRDF and accumulate its contribution into radiance
				radiance += throughput * EvalCombinedBRDF(shadingNormal, L, V, material) * GetLightIntensity(light, hitPosition) * lightWeight;
			}
		}
#endif

        // Terminate loop early on last bounce (we don't need to sample BRDF)
		if (bounce == MaxBounces - 1)
			break;

        // Russian Roulette
        if (bounce > MIN_BOUNCES)
        {
			float rrProb = min(0.95f, Luminance(throughput));
            if (rrProb < Rand(rngState))
				break;
            else
				throughput /= rrProb;
		}

        // Sample BRDF to generate the next ray
        // First, figure out whether to sample diffuse or specular BRDF
		int brdfType;
        if (material.metallic == 1.0f && material.roughness == 0.0f)
        {
	        // Fast path for mirrors
			brdfType = SPECULAR_TYPE;
		}
        else
        {
            // TODO: no specular yet
#if 0
	        // Decide whether to sample diffuse or specular BRDF (based on Fresnel term)
			float brdfProb = GetBRDFProbability(material, V, shadingNormal);

            if (Rand(rngState) < brdfType)
            {
				brdfType = SPECULAR_TYPE;
				throughput /= brdfProb;
			}
            else
            {
				brdfType = DIFFUSE_TYPE;
				throughput /= (1.0 - brdfProb);
			}
#else
			brdfType = DIFFUSE_TYPE;
#endif
        }

        // Run importance sampling of selected BRDF to generate reflecting ray direction
		float3 brdfWeight;
		float2 u = float2(Rand(rngState), Rand(rngState));
        if (!EvalIndirectCombinedBRDF(u, shadingNormal, geometryNormal, V, material, brdfType, ray.Direction, brdfWeight))
			break; // ray was  eaten by the surface

        // Account for surface properties using the BRDF weight
		throughput *= brdfWeight;

        // Offset a new ray origin from the hit point to prevent self-intersection
		ray.Origin = OffsetRay(hitPosition, geometryNormal);
	}

    // Temporal accumulation
	float3 prevColor = g_AccumulationOutput[launchIndex].rgb;
	float3 accumulatedColor = radiance;
    if (_Dynamics.accumulationIndex >= 0)
    {
		int accumulationIndex = _Dynamics.accumulationIndex;
		int accumulationCount = min(accumulationIndex + 1, 256);
		accumulatedColor = lerp(prevColor, accumulatedColor, 1.0f / accumulationCount);
	}

	g_AccumulationOutput[launchIndex] = float4( /*SrgbToLinear*/(accumulatedColor), 1.0f);
}

[shader("miss")]
void Miss(inout FHitInfo payload)
{
    payload.materialID = INVALID_ID;
	payload.T = -1;
}

/**
 * Acceleration Structure Memory
 *   A basic scratch buffer management strategy allocates a single memory block of predefined size to use for 
 * building all acceleration structures. But this approach's strengths and weakness stem from its simplicity.
 * Since any part of the memory block may be in use, buffer resize and/or deallocation operations must be 
 * deferred until all ASs are built - leading to worst-case memory requirements.
 *   To reduce the total memory use, an alternative approach insread serializes BLAS builds (to some degree) and
 * reuses scratch buffer memory for multiple builds. Barriers and inserted between builds to ensure that the scratch
 * memory blocks are safe to reuse before upcoming BLAS build(s) execute.
 *   Amortizing builds across multiple frame is especially useful when a scene contains a large number of meshes.
 */

/**
 * Terminating the Path Tracing Loop
 *  Light paths naturally end when the light energy carried along the path is fully absorbed or once the path 
 * exits the scene to the sky but reaching this state can require an extremely large number of bounces.
 * To prevent tracing paths of unbounded length, we define a maximum number of bounces and terminate the ray tracing
 * loop once the maximum is reached. Bias is introduced when terminating paths too early, but paths that never
 * encounter light are unnecessary and limit performance. 
 *  To balance this trade-off, we use a `Russian roulette` approach to decide if insignificant paths should be 
 * terminated early. An insignificant path is one that encounters a surface that substantially decreases ray throughput
 * and as a result will not significantly contribute to the final image. Russian roulette is run before tracing
 * each non-primary ray, and it randomly selects whether to terminate the ray tracing loop or not. The probability
 * of termination is based on the luminance of the path's throughput and is clamped to be at most 0.95.
 */
