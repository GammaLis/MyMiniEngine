#ifndef PBRUTILITY_HLSLI
#define PBRUTILITY_HLSLI

#define PI 3.14159265359

#define MEDIUMP_FLT_MAX	65504.0
#define MEDIUMP_FLT_MIN	0.00006103515625
#ifdef TARGET_MOBILE
#define FLT_EPS				MEDIUMP_FLT_MIN
#define saturateMediump(x) 	min(x, MEDIUMP_FLT_MAX)
#else
#define FLT_EPS 	1e-5
#define saturateMediump(x) 	x
#endif

#include "ColorUtility.hlsli"

// structs
struct TLight
{
	float3 color;	// the color of emitted light, as a linear RGB color
	float intensity;	// the light's brighness. The unit depends on the type of light
	float3 positionOrDirection;
	float type;		// 0 - directional lights, 1 - punctual lights
	float3 spotDirection;
	float falloffRadius;	// maximum distance of influence
	float2 spotAttenScaleOffset;	// Dot(...) * scaleOffset.x + scaleOffset.y
	// or float2 spotAngles;	// x - innerAngle, y - outerAngle
	float2 padding;	// CPP里字节对齐，这里需要补齐
};

struct TMaterial
{
	float4 baseColor;
	float4 emissive;
	float occlusion;
#if defined(SHADING_MODEL_METALLIC_ROUGHNESS)
	float metallic;
	float perceptualRoughness;
	float f0;
#elif defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
	float3 specularColor;
	float glossiness;
#endif
};

// ************************************************************
// Specular BRDF
/// D
// D_GGX = alpha^2 / ( pi * ( (NdotH)^2 * (alpha^2 - 1) + 1 )^2 )
float D_GGX(float NdotH, float roughness)
{
	float a = NdotH * roughness;
	float k = roughness / (1.0 - NdotH * NdotH + a * a);
	return k * k * (1.0 / PI);

	// float a2 = roughness * roughness;
	// float f = (NdotH * a2 - NdotH) * NdotH + 1.0;
	// return a2 / (PI * f * f);
}

/**
 * we can improve this implementation by using half precision. This optimization
 *requires changes to the original equation as there are 2 problems when computing 
 *1 - (NdotH)^2 in half floats. First this computation suffers from floating cancellation
 *when (NdotH)^2 is close to 1 (highlights). Secondly, NdotH does not have enough 
 *precision around 1.
 *	Lagrange's identity: |cross(a, b)^2| = |a|^2*|b|^2 - (NdotH)^2
 *	since both n and h are unit vectors, = 1 - (NdotH)^2.
 *	we can compute 1 - (NdotH)^2 directly with half precision floats by using 
 *a simple cross product.
 */
// 注：针对移动端的优化，PC端不必
float D_GGX_Optimized(float roughness, float NdotH, const float3 n, const float3 h)
{
	float3 NxH = cross(n, h);
	float a = NdotH * roughness;
	float k = roughness / (dot(NxH, NxH) + a * a);
	float d = k * k * (1.0 / PI);
	return saturateMediump(d);
}

// ** unused **
// Estevez and Kulla 2017, "Production Friendly Microfacet Sheen BRDF"
float D_Charlie(float roughness, float NdotH)
{
	float invAlpha = 1.0 / roughness;
	float cos2h = NdotH * NdotH;
	float sin2h = max(1.0 - cos2h, 0.0078125);	// 2^(-14/2), so sin2h^2 > 0 in fp16
	return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * PI);
}

float D_Ashikhmin(float NdotH, float roughness)
{
	float a = roughness;
	float a2 = a * a;
	float cos2h = NdotH * NdotH;
	float sin2h = 1 - cos2h;
	float sin4h = sin2h * sin2h;
	return 1.0 / (PI * (1 + 4 * a2)) * (sin4h + 4 * exp(-cos2h / (a2 * sin2h)));
}
// ** end **

/// G
// G(v, l, a) = G1(l, a) * G1(v, a)
// fr(v, l) = D(h, a) * V(v, l, a) * F(v, h, F0)
// V(v, l, a) = G(v, l, a) / 4(NdotV * NdotL) = V1(l,a) * V1(v, a)
// Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
float V_SmithGGXCorrelated(float NdotV, float NdotL, float roughness)
{
	float a2 = roughness * roughness;
	float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
	float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
	return 0.5 / (GGXV + GGXL);
}

// we can optimize this visibility function by using an approximation after
// noticing that all the terms under the square roots are squares and that 
// all the terms are in the [0, 1] range:
// V(v, l, a) = 0.5 / (NdotL * ( NdotV * (1-a) + a ) + NdotV * ( NdotL*(1-a) + a) )
// this approximation is mathematically wrong but saves 2 square root operations and is good 
// enough for real-time applications
// Hammon 2017, "PBR Diffuse Lighting for GGX+Smith Microsurfaces"
float V_SmithGGXCorrelatedFast(float NdotV, float NdotL, float roughness)
{
	float a = roughness;
	float GGXV = NdotL * (NdotV * (1.0 - a) + a);
	float GGXL = NdotV * (NdotL * (1.0 - a) + a);
	return 0.5 / (GGXV + GGXL);
}

 // Kelemen 2001, "A Microfacet Based Coupled Specular-Matte BRDF Model with Importance Sampling"
float V_Kelemen(float LdotH)
{
	return saturateMediump(0.25 / (LdotH * LdotH));
}

// Neubelt and Pettineo 2013, "Crafting a Next-gen Material Pipeline for The Order: 1886"
float V_Neubelt(float NdotV, float NdotL)
{
	return saturateMediump(1.0 / (4.0 * (NdotL + NdotV - NdotL * NdotV));
}

/// F
// the Fresnel term defines how light reflects and refracts at the interface between 2 different
// media, or the ratio of reflected and transmitted energy. [Schlick94] describes an inexpensive
// approximation of the Fresnel term for the Cook-Torrance specular BRDF:
// F_Schlick(v, h, F0, F90) = F0 + (F90 - F0) * (1 - VdotH)^5
// the constant F0 represets the specular reflectance at normal incident and is achromatic for 
// dielectrics, and chromatic for metals.
float3 F_Schlick(float VdotH, float3 F0, float F90)
{
	return F0 + (F90 - F0) * pow(1.0 - VdotH, 5.0);
}

// using F90 set to 1, the Schlick approximation for the Fresnel term can be optimized for
// scalar operations
float3 F_Schlick(float VdotH, float3 F0)
{
	float f = pow(1.0 - VdotH, 5.0);
	return f + F0 * (1.0 - f);
}

float F_Schlick(float VdotH, float F0, float F90)
{
	return F0 + (F90 - F0) * pow(1.0 - VdotH, 5.0);
}

// https://learnopengl.com/PBR/IBL/Diffuse-irradiance
float3 F_SchlickRoughness(float VdotH, float3 F0, float roughness)
{
	return F0 + (max(1.0 - roughness, F0) - F0) * pow(1.0 - VdotH, 5.0);
}

// ************************************************************
// Diffuse BRDF
// use a simple Lambertian BRDF that assumes a uniform diffuse response over the microfacets hemisphere:
// fd(v, l) = sigma / pi
float Fd_Lambert()
{
	return 1.0 / PI;
}
// in practice, the diffuse reflectance sigma is multiplied later
// float Fd = diffuseColor 

/**
 * 	however, the diffuse part would ideally be coherent with the specular term and take into account the surface
 *roughness. Both the Disney diffuse BRDF and Oren-Nayar model take the roughness into account and create some
 *retro-reflection at grazing angles.
 *	The Disney diffuse BRDF is:
 *	fd(v, l) = sigma / pi * F_Schlick(n, l, 1, F90) * F_Schlick(n, v, 1, F90)
 *	F90 = 0.5 + 2 * a * cos(theta_d)^2
 */
// Burley 2012, "Physically-Based Shading at Disney"
float Fd_Burley(float NdotV, float NdotL, float LdotH, float roughness)
{
	float F90 = 0.5 + 2.0 * roughness * LdotH * LdotH;
	float lightScatter = F_Schlick(NdotL, 1.0, F90);
	float viewScatter = F_Schlick(NdotV, 1.0, F90);
	return lightScatter * viewScatter * (1.0 / PI);
}

// Energy conserving wrap diffuse term, does *not* include the divide by pi
float Fd_Wrap(float NoL, float w) {
    return saturate((NoL + w) / ((1.0 + w) * (1.0 + w)));
}

// ************************************************************
// improving the BRDFs
// > energy gain in diffuse reflectance
// > energy lose in specular reflectance
// the Cook-Torrance BRDF we presented earlier attempts to model several events at the mocrofacet but
// dot so by accounting for a single bounce of light. This approximation can cause a loss of energy 
// at high roughness, the surface is not energy preserving.
// ...
// E(l) is the directional albedo of the specular BRDF fr, with F0 set to 1:
// E(l) = Inv(f(l, v) * NdotV, dw)
// fms(l, v) = F0 * (1-E(l))/E(l) * fss(l, v) (fss(l, v) - previous fr)
// 		= fss(l, v) + F0 * (1/r - 1) * fss(l, v);
// 	r = Inv(D(l, v) * V(l, v)NdotL, dw)
// We can implement specular energy compensation at a negligible cost if we store r in the DFG lookup
// table.
// float3 energyCompensation = 1.0 + F0 * (1.0 / dfg.y - 1.0);
// // scale the specular lobe to account for multiscattering
// Fr *= energyCompensation;

// ************************************************************
// Remapping
// 1. baseColor remapping
// The base color of a material is affected by the “metallicness” of said material. Dielectrics have
// achromatic specular reflectance but retain their base color as the diffuse color. Conductors on
// the other hand use their base color as the specular color and do not have a diffuse component. 
// 
// 2. reflectance remapping
// a.dielectrics
// The Fresnel term relies on f0, the specular reflectance at normal incidence angle, and is 
// achromatic for dielectrics. 
// 		f0 = 0.16 * reflectance^2
// all materials have a Fresnel reflectance of 100% at grazing angles so we will set f90 to 1.0
// 		f90 = 1.0
// b.conductors
// 		f0 = baseColor * metallic	
// 		
// 3. roughness remapping and clamping
// the roughness set by the user, called perceptualRoughness here, is remapped to a perceptually 
// linear range
// 		a = clamp(perceptualRoughness^2, 0.045)	(The roughness can also not be set to 0 to avoid obvious divisions by 0. )
void Remapping()
{
	// base color
	float4 baseColor;

	float metallic = 0.1;
	float3 diffuseColor = (1.0 - metallic) * baseColor.rgb;

	// reflectance
	float reflectance = 0.5;	
	float f0_d = 0.16 * reflectance * reflectance;	// reflectance = 0.5, f0 = 0.04 (common case)

	float3 f0_m = baseColor.rgb * metallic;

	float3 f0 = 0.16 * reflectance * reflectance * (1 - metallic) + baseColor.rgb * metallic;

	// roughness
	float perceptualRoughness = 0.1;
	float a = max(perceptualRoughness * perceptualRoughness, 0.045);
}

// ************************************************************
float3 BRDF(float3 lightDir, float3 viewDir, float3 normal, const TMaterial mat);
// Lighting
// 1. directional lights
float3 EvaluateDirectionalLights(float3 lightColor, float lightIntensity, float3 lightDir, 
	float3 viewDir, float3 worldPos, float3 normal, const TMaterial mat)
{
	float3 L = normalize(-lightDir);
	float3 V = viewDir;
	float NdotL = saturate(dot(normal, L));

	// lightIntensity is the illuminance 
	// at perpendicular incidence in lux
	float illuminance = lightIntensity * NdotL;
	float3 luminance = BRDF(L, V, normal, mat) * illuminance * lightColor;
	return luminance;
}

// 2. punctual lights
// E = I/max(d^2, 0.01^2)(1 - d2/r2)	r - light's radius of influence
float GetSquareFalloffAttenuation(float3 lightVec, float lightInvRadius)
{
	float distanceSquare = dot(lightVec, lightVec);
	float factor = distanceSquare * lightInvRadius * lightInvRadius;
	float smoothFactor = max(1.0 - factor, 0.0);
	return (smoothFactor * smoothFactor) / max(distanceSquare, 1e-4);
}
// atten = (dot(lightDir, spotDir) - cosOuter) / (cosInnter - cosOuter)
float GetSpotAngleAttenuation(float3 lightDir, float spotDir, float innerAngle, float outerAngle)
{
	// the scale and offset computations can be done CPU-side
	float cosOuter = cos(outerAngle);
	float spotScale = 1.0 / max(cos(innerAngle) - cosOuter, 1e-4);
	float spotOffset = -cosOuter * spotScale;

	float cd = dot(-lightDir, spotDir);
	float attenuation = saturate(cd * spotScale + spotOffset);
	return attenuation * attenuation;
}
float GetSpotAngleAttenuation(float3 lightDir, float3 spotDir, float2 scaleOffset)
{
	float spotScale = scaleOffset.x, spotOffset = scaleOffset.y;
	float cd = dot(-lightDir, spotDir);
	float attenuation = saturate(cd * spotScale + spotOffset);
	return attenuation * attenuation;
}
// 3.photometric lights
// the photometric profile can be applied at rendering time as a simple attenuation
// Lout = f(v, l) * I/d^2 * Psi - Psi is the photometric attenuation func
// float GetPhotometricAttenuation(float3 lightVec, float3 spotDir)
// {
// 	float cosTheta = dot(-lightVec, spotDir);
// 	float angle = acos(cosTheta) * (1.0 / PI);
// 	return texture2DLodEXT(_LightProfileMap, float2(angle, 0.0), 0.0).r;
// }
// 
float3 EvaluatePunctualLights(float3 lightColor, float3 lightIntensity,
	float3 lightPos, float lightRadius, float3 spotDir, float2 spotScaleOffset,
	float3 worldPos, float3 normal, float3 viewDir, const TMaterial mat)
{
	float3 lightVec = lightPos - worldPos;
	float3 lightDir = normalize(lightVec);
	float NdotL = saturate(dot(normal, lightDir));

	float lightInvRadius = 1.0 / lightRadius;

	float atten;
	atten = GetSquareFalloffAttenuation(lightVec, lightInvRadius);
	atten *= GetSpotAngleAttenuation(lightDir, spotDir, spotScaleOffset);
	// atten *= GetPhotometricAttenuation(lightVec, spotDir);

	float3 luminance = BRDF(lightDir, viewDir, normal, mat) * lightIntensity * atten * NdotL * lightColor;

	return luminance;
}

//
float3 DirectLighting(TLight light, const TMaterial mat, 
	float3 worldPos, float3 normal, float3 viewDir)
{
	float3 col = 0;

	float3 lightColor = light.color;
	float lightIntensity = light.intensity;
	if (light.type == 0)
	{
		float3 lightDir = normalize(light.positionOrDirection);
		col = EvaluateDirectionalLights(lightColor, lightIntensity, lightDir,
			viewDir, worldPos, normal, mat);
	}
	else
	{
		float3 lightPos = light.positionOrDirection;
		float lightRadius = light.falloffRadius;
		float3 spotDir = normalize(light.spotDirection);
		float2 spotScaleOffset = float2(light.spotAttenScaleOffset);
		col = EvaluatePunctualLights(lightColor, lightIntensity, lightPos,
			lightRadius, spotDir, spotScaleOffset,
			worldPos, normal, viewDir, mat);
	}

	return col;
}

float3 BRDF(float3 lightDir, float3 viewDir, float3 normal, const TMaterial mat)
{
	float3 L = lightDir;
	float3 V = viewDir;
	float3 N = normal;

	float3 H = normalize(L + V);
	float NdotV = abs(dot(N, V)) + 1e-5;
	float NdotL = saturate(dot(N, L));
	float NdotH = saturate(dot(N, H));
	float LdotH = saturate(dot(L, H));

	// remapping
	float3 baseColor = mat.baseColor.rgb;
	float metallic = mat.metallic;
	float roughness = max(mat.perceptualRoughness * mat.perceptualRoughness, 0.045);
	float3 F0 = mat.f0 * (1 - metallic) + baseColor * metallic;
	float3 diffuseColor = baseColor * (1 - metallic);

	float _D = D_GGX(NdotH, roughness);
	float3 _F = F_Schlick(LdotH, F0);	// LdotH == VdotH
	float _V = V_SmithGGXCorrelated(NdotV, NdotL, roughness);

	// specular BRDF
	float3 Fr = (_D * _V) * _F;

	// diffuse BRDF
	float3 Fd = Fd_Lambert();
	// Fd = Fd_Burley(NdotV, NdotL, LdotH, roughness);
	Fd *= diffuseColor;

	return Fr + Fd;
}

// ************************************************************
// Indirect lighting
// 
// static const float MAX_REFLECTION_LOD = 4.0;

// float3 ApproximateDiffuseIBL(float3 N, float3 ks, float3 albedo)
// {
// 	float3 kd = 1.0 - ks;
// 	float3 irradiance = _IrradianceMap.Sample(_LinearSampler, N).rgb;
// 	float3 indirectDiffuse = kd * irradiance * albedo;
// 	return indirectDiffuse;
// }

// SH
struct SH9
{
    float c[9];
};
struct SH9Color
{
	float3 c[9];
};
SH9 SH9Basis(float3 s)
{
    float x = s.x, y = s.y,  z = s.z;
    float x2= x*x, y2 = y*y, z2 = z*z;
    SH9 sh;
    sh.c[0] =  0.282095f;   // 1 / (2 * sqrt(pi))

    sh.c[1] = -0.488603f * y;   // -sqrt(3)  / (2 * sqrt(pi))  * y
    sh.c[2] =  0.488603f * z;   //  sqrt(3)  / (2 * sqrt(pi))  * z
    sh.c[3] = -0.488603f * x;   // -sqrt(3)  / (2 * sqrt(pi))  * x

    sh.c[4] =  1.092548f * x * y;   //  sqrt(15) / (2 * sqrt(pi))  * xy
    sh.c[5] = -1.092548f * y * z;   // -sqrt(15) / (2 * sqrt(pi))  * yz
    sh.c[6] =  0.315392f * (3 * z2 - 1);    //  sqrt(5)  / (4 * sqrt(pi))  * (3z^2 - 1)
    sh.c[7] = -1.092548f * z * x;   // -sqrt(15) / (2 * sqrt(pi))  * xz
    sh.c[8] =  0.546274f * (x2 - y2);       //  sqrt(15) / (4 * sqrt(pi))  * (x^2 - y^2)

    return sh;
}
float3 ApproximateDiffuseSH(SH9Color sh, float3 N, float3 diffuseColor)
{
	// compute the SH basis, oriented about the normal direction
	SH9 shBasis = SH9Basis(N);

	// compute the SH dot product to get irradiance
	float3 irradiance = 0.0f;
	for (uint k = 0; k < 9; ++k)
		irradiance += sh.c[k] * shBasis.c[k];

	irradiance *= diffuseColor / PI;
	return irradiance;
}

// // specularColor = F_SchlickRoughness(NdotV, F0, roughness);	// the indirect Fresnel result F
// float3 ApproximateSpecularIBL(float3 N, float3 V, float3 specularColor, float roughness)
// {
// 	float NdotV = satate(dot(N, V));
// 	float3 R = 2 * dot(V, N) * N - V;

// 	float lod = roughness * MAX_REFLECTION_LOD;
// 	float3 prefilteredColor = _PrefilterEnvMap.SampleLevel(_LinearSampler, R, lod);
// 	float2 envBRDF = _EnvBRDF.Sample(_PointSampler, float2(NdotV, roughness)).rg;

// 	return prefilteredColor * (specularColor * envBRDF.x + envBRDF.y);
// }

// ************************************************************
// Precomputing
float RadicalInverse_VdC(uint bits) 
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}
// a low-discrepancy sequence
float2 Hammersley(uint i, uint N)
{
    return float2(float(i)/float(N), RadicalInverse_VdC(i));
}

#endif	// PBRUTILITY_HLSLI

/**
 *	https://google.github.io/filament/Filament.html
 *	## Notation
 *	v - view unit vector, 	l - incident light unit vector,	
 *	n - surface normal,		h - half unit vector between l and v
 *	f - BRDF,				
 *	fd - diffuse component of a BRDF,
 *	fr - specular component of a BRDF
 *	alpha - roughness, remapped from using input perceptualRoughness
 *	sigma - diffuse reflectance
 *	Omega - spherical domain
 *	F0 - reflectance at normal incident
 *	F90 - reflectance at grazing angle
 *	n_ior - index of fraction (IOR) of an interface
 *	<n, l> - dot product clamped to [0, 1]
 *	<a> - saturated value (clamped to [0, 1])
 *
 * 
 */

/**
 * 	** Material
 * 	a material defines the visual appearance of a surface. To completely describe
 * and render a surface,a material provides the following information:
 	> material model
 	> set of use-controllable named parameters
 	> raster state(blending mode, backface culling, etc)
 	> vertex shader code
 	> fragment shader code

 	## Material models
 	> Lit (or standard)
 	> Subsurface
 	> Cloth
 	> Unlit
 	> Specular glossiness(legacy)
	this material model can be used to describe a large number of non-metallic surfaces
(dieletrics) or metallic surfaces(conductors)
	
 */

/**
 * ## Standard parameters
 * BaseColor - Diffuse albedo for non-metallic surfaces, and specular color for metallic surfaces 
 * Metallic - Whether a surface appears to be dielectric (0.0) or conductor (1.0). Often used as a binary value (0 or 1) 
 * Roughness - Perceived smoothness (0.0) or roughness (1.0) of a surface. Smooth surfaces exhibit sharp reflections 
 * Reflectance - Fresnel reflectance at normal incidence for dielectric surfaces.
 * 		This replaces an explicit index of refraction 
 * Emissive - Additional diffuse albedo to simulate emissive surfaces (such as neons, etc.) 
 * 		This parameter is mostly useful in an HDR pipeline with a bloom pass 
 * Ambient - Defines how much of the ambient light is accessible to a surface point. 
 * 		It is a per-pixel shadowing factor between 0.0 and 1.0. 
 * 
 */
