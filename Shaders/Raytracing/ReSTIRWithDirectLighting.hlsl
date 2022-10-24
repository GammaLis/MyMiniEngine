// Ref: "Spatiotemporal Reservior Resampling for Real-time Ray Tracing with Dynamic Direct Lighting"

#define USE_PCG 1

// Number of candidates 
#define RIS_CANDIDATES_LIGHTS 16

// Enable this to cast shadow rays for each candidate during resampling. This is expensive but increases quality
#define SHADOW_RAY_IN_RIS 0

#define COMBINE_SPATIOTEMPORAL 1

#include "ModelViewerRTInputs.hlsl"
#include "RayTracingCommon.hlsl"
#include "RayTracingIntersection.hlsl"
#include "Reservoir.hlsl"
#include "RayTracingLightingCommon.hlsl"
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

float3 EvalReservoir(Reservoir r, float3 position, float3 normal, float3 V, MaterialProperties material)
{
    float3 radiance = 0;

    if (r.LightData >= 0)
    {
        LightData light = _LightBuffer[r.LightData];
        float3 lightVec = light.pos - position;
        float  lightDist = length(lightVec);
        float3 L = lightVec / max(0.01f, lightDist);
        float  lightWeight = 1.0f;
    #if RESTIR_WEIGHT_NORMALIZATION
        lightWeight = r.Weights;
    #else
        lightWeight = r.Weights / max(0.001f, r.M * r.TargetPdf);
    #endif

        // If the light is not in shadow, evaluate BRDF and accumulate its contribution into radiance
        radiance = EvalCombinedBRDF(normal, L, V, material) * GetLightIntensity(light, position) * lightWeight;
    }
    
    return radiance;
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
    RngStateType rngState = InitRNG(launchIndex, launchDimensions, uint(_Dynamics.frameIndex));

    float2 pixel = float2(DispatchRaysIndex().xy);
    const float2 resolution = float2(DispatchRaysDimensions().xy);

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
#if 0
	float4 unprojected = mul(float4(pixel, 0, 1), _Dynamics.cameraToWorld);
    float3 world = unprojected.xyz / unprojected.w;
	float3 origin = _Dynamics.worldCameraPosition.xyz;
    float3 direction = normalize(world - origin);

#else
	float3 world = mul(float4(pixel, -1.0, 1.0), _View.ScreenToWorldMatrix).xyz;
	float3 origin = _View.CamPos.xyz;
	float3 direction = normalize(world - origin);
#endif

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
	float3 throughput = float3(1, 1, 1);
    Reservoir r = CreateReservoir();

    // Trace the ray
    TraceRay(
        g_Accel,
        0, // RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
        0xFF,
        STANDARD_RAY_INDEX, 
        1,
        STANDARD_RAY_INDEX,
        ray,  
        payload);
    // On a miss, load the sky value and break out of the ray tracing loop
    if (!payload.HasHit())
    {
        radiance += throughput * _Dynamics.backgroundColor.rgb;
		g_ScreenOutput[launchIndex] = float4( /*SrgbToLinear*/(radiance), 1.0f);
		StoreReservoir(r, launchIndex, (uint2) _Dynamics.resolution);
        return;
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

    // Evalute sun light
#if 0
    LightData sunLight = GetSunLight(float2(Rand(rngState), Rand(rngState)));
    float3 LSun = -sunLight.pos;
    if (CastShadowRay(hitPosition, geometryNormal, LSun, FLT_MAX))
    {
        radiance += throughput * EvalCombinedBRDF(shadingNormal, LSun, V, material) * GetLightIntensity(sunLight, hitPosition);
    }
#endif

    // Evaluate punctual lights
#if 1

    // Evalute direct light (next event estimation), start by sampling one light
    // Local reservoir (RIS candidates)
    Reservoir localReservoir = CreateReservoir();
    bool bValidLightSample = SampleLightRIS(rngState, hitPosition, geometryNormal, localReservoir);
    if (bValidLightSample)
    {
        // Prepare data needed to evaluate the light
        LightData light = _LightBuffer[localReservoir.LightData];
        float3 lightVec = light.pos - hitPosition;
        float  lightDist = length(lightVec);
        float3 L = lightVec / max(0.01f, lightDist);

        // See if the light sample is visible
		if (!SHADOW_RAY_IN_RIS && !CastShadowRay(hitPosition, geometryNormal, L, lightDist))
        {
            // If not visible, discard the sample (but keep the M)
            localReservoir.LightData = -1;
            localReservoir.TargetPdf = 0;
            localReservoir.Weights = 0;
            bValidLightSample = false;
        }
    }
    // r = localReservoir;
    CombineReservoirs(r, localReservoir, 0.5, localReservoir.TargetPdf);
#if COMBINE_SPATIOTEMPORAL
    FinalizeResampling(r);
    r.M = 1;
    // Or
    // r = localReservoir;
#endif


#if !COMBINE_SPATIOTEMPORAL
    // Temporal reuse
    Reservoir temporalReservoir = CreateReservoir();
    {
    #if 1
        float3 velocity = UnpackVelocity(_TexVelocity[launchIndex]);
        float2 prevPixel = float2(launchIndex + 0.5) + velocity.xy;
    #else
        float2 prevPixel = launchIndex;
    #endif

        if (IsValidScreenSample(prevPixel))
        {
            temporalReservoir = GetReservoir(uint2(prevPixel), _Dynamics.resolution);
            // Clamp the previous frame's M to at most 20x of the current frame's reservoir's M
            float MMax = r.M * 20;
            if (temporalReservoir.M > MMax)
            {
                temporalReservoir.Weights *= MMax / temporalReservoir.M;
                temporalReservoir.M = MMax;
            }

            float sampleWeight = 0;
            if (temporalReservoir.LightData >= 0)
            {
                LightData sampleLight = _LightBuffer[temporalReservoir.LightData];
                float3 lightVector = sampleLight.pos - hitPosition;
                float  lightDistance = length(lightVector);
                float3 L = lightVector / max(0.01f, lightDistance);
                float  NdotL = saturate(dot(geometryNormal, L));

            #if SHADOW_RAY_IN_RIS
                if (!CastShadowRay(hitPosition, geometryNormal, L, lightDistance))
                    NdotL = 0;
            #endif

                float3 lightIntensity = GetLightIntensity(sampleLight, hitPosition) * NdotL;
                sampleWeight = Luminance(lightIntensity);
            }

            CombineReservoirs(r, temporalReservoir, Rand(rngState), sampleWeight); // sampleWeight r.TargetPdf
        }
    }

    // Spatiotemporal reuse
    Reservoir spatiotemporalReservoir = CreateReservoir();
    {
        // TODO...
    }

    FinalizeResampling(r);

#else // COMBINE_SPATIOTEMPORAL
    // Reservoir SampleLightSpatiotemporal(inout RngStateType rngState, Reservoir curSample, uint2 pixel, float3 position, float3 normal)
    Reservoir spatiotemporalReservoir = SampleLightSpatiotemporal(rngState, r, launchIndex, hitPosition, geometryNormal);
    
#endif // COMBINE_SPATIOTEMPORAL

    /// Shading
    {
    #if !COMBINE_SPATIOTEMPORAL
        radiance += EvalReservoir(localReservoir, hitPosition, shadingNormal, V, material);
        radiance += EvalReservoir(temporalReservoir, hitPosition, shadingNormal, V, material);
        // radiance += EvalReservoir(spatiotemporalReservoir, hitPosition, shadingNormal, V, material);
    
    #else
        if (spatiotemporalReservoir.LightData >= 0)
        {
			LightData sampleLight = _LightBuffer[spatiotemporalReservoir.LightData];
            float3 lightVector = sampleLight.pos - hitPosition;
            float  lightDistance = length(lightVector);
            float3 L = lightVector / max(0.01f, lightDistance);
			float lightWeight = spatiotemporalReservoir.Weights;

            // If not visible, discard the shading output and the light sample
            if (!SHADOW_RAY_IN_RIS && !CastShadowRay(hitPosition, geometryNormal, L, lightDistance))
            {
                // radiance = 0;

                spatiotemporalReservoir.LightData = -1;
                spatiotemporalReservoir.TargetPdf = 0;
                spatiotemporalReservoir.Weights = 0;
            }
            else
            {                
                radiance += EvalCombinedBRDF(shadingNormal, L, V, material) * GetLightIntensity(sampleLight, hitPosition) * lightWeight;
            }

            r = spatiotemporalReservoir;
        }
    #endif
    }


#if 0

    bool bValidLightSample = SampleLightReSTIR(rngState, launchIndex, hitPosition, geometryNormal, r);
    if (bValidLightSample)
    {
        // Prepare data needed to evaluate the light
        LightData light = _LightBuffer[r.LightData];
        float3 lightVec = light.pos - hitPosition;
        float  lightDist = length(lightVec);
        float3 L = lightVec / max(0.01f, lightDist);

        if (CastShadowRay(hitPosition, geometryNormal, L, lightDist))
		{
        #if RESTIR_WEIGHT_NORMALIZATION
			float lightWeight = r.Weights;
        #else
			float lightWeight = r.Weights / max(0.001f, r.M * r.TargetPdf);
        #endif

            // If the light is not in shadow, evaluate BRDF and accumulate its contribution into radiance
			radiance += EvalCombinedBRDF(shadingNormal, L, V, material) * GetLightIntensity(light, hitPosition) * lightWeight;
		}
	}
#endif

#endif // Punctual lights

	g_ScreenOutput[launchIndex] = float4( /*SrgbToLinear*/(radiance), 1.0f);
	StoreReservoir(r, launchIndex, (uint2) _Dynamics.resolution);
}

[shader("miss")]
void Miss(inout FHitInfo payload)
{
    payload.materialID = INVALID_ID;
	payload.T = -1;
}
