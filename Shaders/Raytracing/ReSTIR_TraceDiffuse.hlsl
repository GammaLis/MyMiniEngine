// Ref: kajiya

#define RTRT 1
#include "ReSTIRGICommon.hlsl"


MaterialProperties GetMaterialProperties(FHitInfo payload)
{
	MaterialProperties material = (MaterialProperties) 0;
	material.baseColor = payload.color;
	material.opacity = 1.0f;
	material.roughness = 0.5f;
	return material;
}


[shader("raygeneration")]
void RayGen()
{
	const uint2 launchIndex = DispatchRaysIndex().xy;
	const uint2 launchDimensions = DispatchRaysDimensions().xy;
	const uint2 pixel = GetFramePixel(launchIndex);
	
	float depth = _TexDepth[pixel];
	if (depth == 0)
	{
		RWSampleRadiance[launchIndex] = 0;
		RWSampleNormal[launchIndex] = float4(0, 0, -1, 0);
		RWSampleHitInfo[launchIndex] = float4(0, 0, 0, 0);
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
	float4 randomSample = UniformSampleSphere(float2(Rand(rngState), Rand(rngState))); // Tangent space
	float3 sampleDir = randomSample.xyz;
	sampleDir = SafeNormalize(normalWS + sampleDir);
	
	RayDesc ray;
	ray.Origin = OffsetRay(posWS, normalWS);
	ray.Direction = sampleDir;
	ray.TMin = 0.1f;
	ray.TMax = s_FarDistance;
	
	FHitInfo payload = (FHitInfo) 0;
	payload.T = -1;
	payload.materialID = -1;
	payload.color = 0;
	
	// Initialize path tracing data
    // Radiance is the final intensity of the light energy presented on screen for a given path
	float3 radiance = float3(0, 0, 0);
	float3 throughput = float3(1, 1, 1);
	float3 hitNormal = -ray.Direction;
	float3 hitOffset = 0;
	// Uniform
	// 1.0 / (cosTheta / PI)
	float pdf = max(0.0, 1.0f / dot(normalWS, ray.Direction)) * PI; // ???
	
	uint traceFlags = 0;
	traceFlags |= RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
	
	TraceRay(
		g_Accel,
		traceFlags,
		0xFF,
		0, 1, 0,
		ray,
		payload);
	
	// On a miss, load the sky value and break out of the ray tracing loop
	if (!payload.HasHit())
	{
		// TODO: SkyLight should be included here
		// radiance += throughput * _Dynamics.backgroundColor.rgb;
		hitOffset = s_FarDistance * ray.Direction;
	}
	else
	{
		// Decode normals and flip them towards the incident ray direction (needed for backfacing triangles)
		float3 geometryNormal = DecodeNormalOctahedron(payload.encodedNormals.xy);
		float3 shadingNormal = DecodeNormalOctahedron(payload.encodedNormals.zw);
		float3 hitPosition = payload.hitPosition;
	
		float3 V = -ray.Direction;
		if (dot(geometryNormal, V) < 0.0)
			geometryNormal = -geometryNormal;
		if (dot(geometryNormal, shadingNormal) < 0.0f)
			shadingNormal = -shadingNormal;
		
		hitNormal = shadingNormal;
		hitOffset = payload.T * ray.Direction;
	
		// Material inputs
		MaterialProperties material = GetMaterialProperties(payload);
	
	#if USE_EMISSIVE
		radiance += material.emissive;
	#endif
	
		// Project the sample into clip space, and check if it's on-screen
		float4 hitPosCS = mul(float4(hitPosition, 1.0f), _View.ViewProjMatrix);
		hitPosCS.xyz /= hitPosCS.w;
		float2 hitUV = hitPosCS.xy * (0.5f, -0.5f) + 0.5f;
		float screenDepth = _TexDepth.SampleLevel(SamplerPointClamp, hitUV, 0).x;
		bool bOnScreen = all(abs(hitPosCS.xy) < 1.0f)
			&& InverseDepthRelativeDiff(hitPosCS.z, screenDepth) < 5e-3 // (screenDepth < hitPosCS.z)
			;
	
		// If it's on screen, we'll try to use its reprojected radiance from last frame
		float4 reprojectedRadiance = 0;
		if (bOnScreen)
		{
			// TODO...
			// reprojectedRadiance = 
		
			// Check if the temporal reprojection is valid
			bOnScreen = reprojectedRadiance.w > 0;
		}
	
		// Sun light
		{
			float3 sunRadiance = _SunColor;
			if (Luminance(sunRadiance) > 0)
			{
				LightData sunLight = GetSunLight(float2(Rand(rngState), Rand(rngState)));
				float3 LSun = -sunLight.pos;
				if (CastShadowRay(hitPosition, geometryNormal, LSun, s_FarDistance))
				{
					radiance += throughput * EvalCombinedBRDF(shadingNormal, LSun, V, material) * GetLightIntensity(sunLight, hitPosition);
				}
			}
		
			// TODO...
		}
	
		if (USE_SSGI_REPROJECTION && bOnScreen)
		{
			// TODO...
		}
		else
		{
			if (USE_LOCAL_LIGHTS)
			{
				// TODO...
			}
		
			if (USE_IRRADIANCE_CACHE)
			{
				// TODO...
			}
		}
	}
	
	RWSampleRadiance[launchIndex] = radiance;
	RWSampleNormal[launchIndex] = float4(hitNormal, 0);
	RWSampleHitInfo[launchIndex] = float4(hitOffset, pdf);
}

[shader("miss")]
void Miss(inout FHitInfo payload)
{
    payload.materialID = INVALID_ID;
	payload.T = -1;
}
