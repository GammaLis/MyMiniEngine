#ifndef BASIC_LIGHTING_HLSLI
#define BASIC_LIGHTING_HLSLI

// Phong lighting
// 
void AntiAliasSpecular(inout float3 texNormal, inout float gloss)
{
	float normalLenSq = dot(texNormal, texNormal);
	float invNormalLen = rsqrt(normalLenSq);
	texNormal *= invNormalLen;
	gloss = lerp(1, gloss, rcp(invNormalLen));
}

// apply fresnel to modulate the specular albedo
void FSchlick(inout float3 specular, inout float3 diffuse, float3 lightDir, float3 halfVec)
{
	float fresnel = pow(1.0 - saturate(dot(lightDir, halfVec)), 5.0);
	specular = lerp(specular, 1, fresnel);
	diffuse = lerp(diffuse, 0, fresnel);
}

// diffuse - Diffuse albedo
// ao - Pre-computed ambient-occlusion
// lightColor - Radiance of ambient light
float3 ApplyAmbientLight(float3 diffuse, float ao, float3 lightColor)
{
	return ao * diffuse * lightColor;
}

float3 ApplyLightCommon(
	float3 diffuseColor,	// diffuse albedo
	float3 specularColor,	// specular color
	float specularMask,		// where is it shiny or dingy?
	float gloss,			// specular power
	float3 normal,			// world-space normal
	float3 viewDir,			// world-space vector from eye to point
	float3 lightDir,		// world-space vector from point to light
	float3 lightColor		// radiance of directional light
	)
{
	float3 halfVec = normalize(lightDir - viewDir);
	float NdotH = saturate(dot(halfVec, normal));

	FSchlick(diffuseColor, specularColor, lightDir, halfVec);

	float specularFactor = specularMask * pow(NdotH, gloss) * (gloss + 2) / 8;

	float NdotL = saturate(dot(normal, lightDir));

	return NdotL * lightColor * (diffuseColor + specularFactor * specularColor);
}

float3 ApplyDirectionalLight(
	float3 diffuseColor,	// diffuse albedo
	float3 specularColor,	// specular color
	float specularMask,		// where is it shiny or dingy?
	float gloss,			// specular power
	float3 normal,			// world-space normal
	float3 viewDir,			// world-space vector from eye to point
	float3 lightDir,		// world-space vector from point to light
	float3 lightColor,		// radiance of directional light
	float3 shadowCoord		// shadow coordinate (shadow map UV & light-relative Z)
	)
{
	float shadow = 1.0f;
	// shadow = GetShadow(shadowCoord);
	
	return shadow * ApplyLightCommon(diffuseColor, specularColor, 
		specularMask, gloss, normal, viewDir, lightDir, lightColor);
}

float3 ApplyPointLight(
	float3 diffuseColor,	// Diffuse albedo
	float3 specularColor,	// Specular albedo
	float specularMask, 	// Where is it shiny or dingy?
	float gloss,			// Specular power
	float3 normal,			// World-space normal
	float3 viewDir,			// World-space vector from eye to point
	float3 worldPos,		// World-space fragment position
	float3 lightPos,		// World-space light position
	float lightRadiusSq,	// 
	float3 lightColor		// Radiance of directional light
	)
{
	float3 lightDir = lightPos - worldPos;
	float lightDistSq = dot(lightDir, lightDir);
	float invLightDist = rsqrt(lightDistSq);
	lightDir *= invLightDist;

	// modify 1/d^2 * R^2 to fall off at a fixed radius
	// (R/d)^2 - d/R = [(1/d^2) - (1/R^2)*(d/R)] * R^2
	float distanceFalloff = lightRadiusSq * (invLightDist * invLightDist);
	distanceFalloff = max(0, distanceFalloff - rsqrt(distanceFalloff));

	return distanceFalloff * ApplyLightCommon(
		diffuseColor,
		specularColor,
		specularMask,
		gloss,
		normal,
		viewDir,
		lightDir,
		lightColor
		);
}

float3 ApplyConeLight(
	float3 diffuseColor,	// Diffuse albedo
	float3 specularColor, 	// Specular albedo
	float specularMask,		// Where is it shiny or dingy?
	float gloss,			// Specular power
	float3 normal,			// World-space normal
	float3 viewDir,			// World-space vector from eye to point
	float3 worldPos,		// World-space fragment position
	float3 lightPos,		// World-space light position
	float lightRadiusSq,
	float3 lightColor,		// Radiance of directional light
	float3 coneDir,	
	float2 coneAngles
	)
{
	float3 lightDir = lightPos - worldPos;
	float lightDistSq = dot(lightDir, lightDir);
	float invLightDist = rsqrt(lightDistSq);
	lightDir *= invLightDist;

	// modify 1/d^2 * R^2 to fall off at a fixed radius
	// (R/d)^2 - d/R = [(1/d^2) - (1/R^2)*(d/R)] * R^2
	float distanceFalloff = lightRadiusSq * (invLightDist * invLightDist);
	distanceFalloff = max(0, distanceFalloff - rsqrt(distanceFalloff));

	float coneFalloff = dot(-lightDir, coneDir);
	coneFalloff = saturate((coneFalloff - coneAngles.y) * coneAngles.x);

	return (coneFalloff * distanceFalloff) * ApplyLightCommon(
		diffuseColor,
		specularColor,
		specularMask,
		gloss,
		normal,
		viewDir,
		lightDir,
		lightColor);
}

float3 ApplyConeShadowedLight(
    float3 diffuseColor, 	// Diffuse albedo
    float3 specularColor,	// Specular albedo
    float specularMask,		// Where is it shiny or dingy?
    float gloss,			// Specular power
    float3 normal,			// World-space normal
    float3 viewDir,			// World-space vector from eye to point
    float3 worldPos,		// World-space fragment position
    float3 lightPos,		// World-space light position
    float lightRadiusSq,
    float3 lightColor,		// Radiance of directional light
    float3 coneDir,
    float2 coneAngles,
    float4x4 shadowTextureMatrix,
    uint lightIndex
    )
{
	float4 shadowCoord = mul(float4(worldPos, 1.0), shadowTextureMatrix);
	shadowCoord.xyz *= rcp(shadowCoord.w);
	float shadow = 1.0;
	// shadow = GetShadowConeLight(lightIndex, shadowCoord.xyz);
	
	return shadow * ApplyConeLight(
		diffuseColor,
        specularColor,
        specularMask,
        gloss,
        normal,
        viewDir,
        worldPos,
        lightPos,
        lightRadiusSq,
        lightColor,
        coneDir,
        coneAngles
        );
}

#endif	// BASIC_LIGHTING_HLSLI
