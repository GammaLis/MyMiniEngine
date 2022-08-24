#ifndef BSDF_INCLUDED
#define BSDF_INCLUDED

#include "Common.hlsl"

/// Ray tracing

// Ref: <<Ray Tracing Gems II>>
// Chapter 14: Reference Path Tracing

// -------------------------------------------------------------------------
//    Constant Definitions
// -------------------------------------------------------------------------

#define NONE 0

// NDF definitions
#define GGX 1
#define BECKMANN 2

// Specular BRDFs
#define MICROFACET 1
#define PHONG 2

// Diffuse BRDFs
#define LAMBERTIAN 1
#define OREN_NAYAR 2
#define DISNEY 3
#define FROSTBITE 4

// BRDF types
#define DIFFUSE_TYPE 1
#define SPECULAR_TYPE 2

// -------------------------------------------------------------------------
//    Configuration macros (user editable - set your preferences here)
// -------------------------------------------------------------------------

// Specify what NDF (GGX or BECKMANN you want to use)
#ifndef MICROFACET_DISTRIBUTION 
#define MICROFACET_DISTRIBUTION GGX
#endif

// Specify default specular and diffuse BRDFs
#ifndef SPECULAR_BRDF
// #define SPECUALR_BRDF MICROFACET
// #define SPECULAR_BRDF PHONG
#define SPECULAR_BRDF NONE
#endif

#ifndef DIFFUSE_BRDF
#define DIFFUSE_BRDF LAMBERTIAN
// #define DIFFUSE_BRDF OREN_NAYAR
// #define DIFFUSE_BRDF DISNEY
// #define DIFFUSE_BRDF FROSTBITE
// #define DIFFUSE_BRDF NONE
#endif

// Specify minimal reflectance for dielectrices (when metalness is 0)
// Nothing has lower reflectance than 2%, but we use 4% to have consistent results with UE4, Frostbite, et al.
#define MIN_DIELECTRICS_F0 0.04f

// Enable this to weigh diffuse by Fresnel too, otherwise specular and diffuse will be simply added together
// (this is disabled by default for Frostbite diffuse which is normalized to combine well with GGX specular BRDF)
#if DIFFUSE_BRDF != FROSTBITE
#define COMBINE_BRDFS_WITH_FRESNEL 1
#endif

// Uncommont this to use "general" version of G1 which is not optimized and uses NDF-specific G_Lambda (can be useful
// for experimenting and debugging)
// #define Smith_G1 Smith_G1_General

// Enable optimized G2 implementation which includes division by specular BRDF denominator (not available for all NDFs,
// check macro G2_DIVIDED_BY_DENOMINATOR if it was actually used)
#define USE_OPTIMIZED_G2 1

// Enable height correlated version of G2 term. Separable version will be used otherwise
#define USE_HEIGHT_CORRELATED_G2 1

// -------------------------------------------------------------------------
//    Automatically resolved macros based on preferences (don't edit these)
// -------------------------------------------------------------------------

// Select distribution function
#if MICROFACET_DISTRIBUTION == GGX
#define Microfacet_D GGX_D
#elif MICROFACET_DISTRIBUTION == BECKMANN
#define Microfacet_D Beckmann_D
#endif

// Select G functions (masking/shadowing) depending on selected distribution
#if MICROFACET_DISTRIBUTION ==  GGX
#define Smith_G_Lambda Smith_G_Lambda_GGX
#elif MICROFACET_DISTRIBUTION == BECKMANN
#define Smith_G_Lambda Smith_G_Lambda_Beckmann_Walter
#endif

#ifndef Smith_G1
// Define version of G1 optimized specifically for selected NDf
#if MICROFACET_DISTRIBUTION == GGX
#define Smith_G1 Smith_G1_GGX
#elif MICROFACET_DISTRIBUTION == BECKMANN
#define Smith_G1 Smith_G1_Beckmann_Walter
#endif
#endif

// Select default specular and diffuse BRDF functions
#if SPECULAR_BRDF == MICROFACET
#define EvalSpecular EvalMicrofacet
#define SampleSpecular SampleSpecularMicrofacet

#if MICROFACET_DISTRIBUTION == GGX
#define SampleSpecularHalfVector SampleGGXVNDF
#else
#define SampleSpecularHalfVector SampleBeckmannWalter
#endif

#elif SPECULAR_BRDF == PHONG // SPECULAR_BRDF
#define EvalSpecular EvalPhong
#define SampleSpecular SampleSpecularPhong
#define SampleSpecularHalfVector SamplePhong

#else // SPECULAR_BRDF
#define EvalSpecular EvalVoid
#define SampleSpecular SampleSpecularVoid
#define SampleSpecularHalfVector SampleSpecularHalfVectorVoid

#endif // SPECULAR_BRDF

#if MICROFACET_DISTRIBUTION == GGX
#define SpecularSampleWeight SpecularSampleWeightGGXVNDF
#define SepcularPdf SpecularGGXVNDFReflectionPdf
#else
#define SpecularSampleWeight SpecularSampleWeightBeckmannWalter
#define SpecularPdf SpecularBeckmannWalterReflectionPdf
#endif

#if DIFFUSE_BRDF == LAMBERTIAN
#define EvalDiffuse EvalLambertian
#define DiffuseTerm Lambertian

#elif DIFFUSE_BRDF == OREN_NAYAR
#define EvalDiffuse EvalOrenNayar
#define DiffuseTerm OrenNayar

#elif DIFFUSE_BRDF == DISNEY
#define EvalDiffuse EvalDisneyDiffuse
#define DiffuseTerm DisneyDiffuse

#elif DIFFUSE_BRDF == FROSTBITE
#define EvalDiffuse EvalFrostbiteDisneyDiffuse
#define DiffuseTerm FrostbiteDisneyDiffuse

#else
#define EvalDiffuse EvalVoid
#define DiffuseTerm None

#endif

// -------------------------------------------------------------------------
//    Structures
// -------------------------------------------------------------------------

struct MaterialProperties
{
	float3 baseColor;
	float metallic;

	float3 emissive;
	float roughness;

	float transmittance;
	float opacity;
};

// Data needed to evaluate BRDF (surface and material properties at given point + configuration of light and normal vectors)
struct BRDFData
{
	// Material properties
	float3 specularF0;
	float3 diffuseReflectance;

	// Roughness
	float roughness;// perceptively linear roughness (artist's input)
	float alpha;	// linear roughness - often `alpha` in specular BRDF equations
	float alphaSqr;	// alpha squared - pre-calculated value commonly used in BRDF equations

	// Commonly used terms for BRDF evaluation
	float3 F; // fresnel term

	// Vectors
	float3 V; // direction to viewer (or opposite direction of incident ray)
	float3 N; // shading normal
	float3 H; // half vector (microfacet normal)
	float3 L; // direction to light (or direction of reflecting ray)

	float NdotL;
	float NdotV;

	float LdotH;
	float NdotH;
	float VdotH;

	// True when V/L is backfacing wrt. shading normal N
	bool bVbackfacing;
	bool bLbackfacing;
};

// -------------------------------------------------------------------------
//    Utilities
// -------------------------------------------------------------------------

// Convert Phong's exponent (shininess) to Beckmann roughness (alpha)
// Ref: "Microfacet Models for Refraction through Rough Surfaces" by Walter et al.
float ShininessToBeckmannAlpha(float shininess)
{
	return sqrt(2.0f / (shininess + 2.0f));
}

// Converts Beckmann roughness (alpha) to Phong's exponent (shininess)
// Source: "Microfacet Models for Refraction through Rough Surfaces" by Walter et al.
float BeckmannAlphaToShininess(float alpha) {
	return 2.0f / min(0.9999f, max(0.0002f, (alpha * alpha))) - 2.0f;
}


// Converts Beckmann roughness (alpha) to Oren-Nayar roughness (sigma)
// Source: "Moving Frostbite to Physically Based Rendering" by Lagarde & de Rousiers
float BeckmannAlphaToOrenNayarRoughness(float alpha) {
	return 0.7071067f * atan(alpha);
}

float3 BaseColorToSpecularF0(float3 baseColor, float metallic)
{
	return lerp(float3(MIN_DIELECTRICS_F0, MIN_DIELECTRICS_F0, MIN_DIELECTRICS_F0), baseColor, metallic);
}

float3 BaseColorToDiffuseReflectance(float3 baseColor, float metallic)
{
	return baseColor * (1.0f - metallic);
}

float None(const BRDFData brdfData)
{
	return 0.0f;
}

float3 EvalVoid(const BRDFData brdfData)
{
	return float3(0, 0, 0);
}

void EvalIndirectVoid(const BRDFData brdfData, float2 u, out float3 rayDirection, out float3 weight)
{
	rayDirection = float3(0, 0, 0);
	weight = float3(0, 0, 0);
}

float3 SampleSpecularVoid(float3 V, float alpha, float alphaSqr, float3 specularF0, float2 u, out float3 weight)
{
	weight = float3(0, 0, 0);
	return float3(0, 0, 0);
}

float3 SampleSpecularHalfVectorVoid(float3 V, float2 alpha, float2 u)
{
	// FIXME
#if 0
	return float3(0, 0, 0);
#else
	return V;
#endif
}

// -------------------------------------------------------------------------
//    Quaternion rotations
// -------------------------------------------------------------------------

// Calculates rotation quaternion from input vector to the vector (0, 0, 1)
// Input vector must be normalized
float4 GetRotationToZAxis(float3 input)
{
	// Handle special case when input is exact or near opposite of (0, 0, 1)
	if (input.z < -0.99999f) return float4(1.0f, 0.0f, 0.0f, 0.0f);

	return normalize(float4(input.y, -input.x, 0.0f, 1.0f + input.z));
}

// Calculates  rotation quaternion from vector (0, 0, 1) to the input vector
// Input vector must be normalized
float4 GetRotationFromZAxis(float3 input)
{
	// Handle special case when input is exact or near opposite of (0, 0, 1)
	if (input.z < -0.99999f) return float4(1.0f, 0.0f, 0.0f, 0.0f);

	return normalize(float4(-input.y, input.x, 0.0f, 1.0f + input.z));
}

// Returns the quaternion with inverted rotation
float4 InvertRotation(float4 q)
{
	return float4(-q.x, -q.y, -q.z, q.w);
}

// Optimized point rotation using quaternion
// https://gamedev.stackexchange.com/questions/28395/rotating-vector3-by-a-quaternion
float3 RotatePoint(float4 q, float3 v) 
{
	const float3 qAxis = float3(q.x, q.y, q.z);
	return 2.0f * dot(qAxis, v) * qAxis + (q.w * q.w - dot(qAxis, qAxis)) * v + 2.0f * q.w * cross(qAxis, v);
}

// -------------------------------------------------------------------------
//    Sampling
// -------------------------------------------------------------------------

// Samples a direction within a hemisphere oriented along +Z axis with a cosine-weighted distribution
// Ref: "Sampling Transformations Zoo" in <<Ray Tracing Gems>> by Shirley et al.
float3 SampleHemisphere(float2 u, out float pdf)
{
	float a = sqrt(u.x);
	float b = 2 * PI * u.y;

	float3 result = float3(
		a * cos(b),
		a * sin(b),
		sqrt(1.0f - u.x));

	pdf = result.z * ONE_OVER_PI;

	return result;
}

float3 SampleHemisphere(float2 u)
{
	float pdf;
	return SampleHemisphere(u, pdf);
}

// For sampling of all our diffuse BRDFs we use consine-weighted hemisphere sampling, with PDF equal to (NdotL/PI)
float DiffusePdf(float NdotL)
{
	return NdotL * ONE_OVER_PI;
}

// -------------------------------------------------------------------------
//    Fresnel
// -------------------------------------------------------------------------

// Schlick's approximation to Fresnel term
// f90 should be 1.0, except for the trick used by Schuler (see `ShadowedF90` function)
float3 EvalFresnelSchlick(float3 f0, float f90, float NdotS)
{
	return f0 + (f90 - f0) * Pow5(1.0f - NdotS);
}

// Schlick's approximation to Fresnel term calculated using spherical gaussian approximation
// https://seblagarde.wordpress.com/2012/06/03/spherical-gaussien-approximation-for-blinn-phong-phong-and-fresnel/ by Lagarde
float3 EvalFresnelSchlickSphericalGaussian(float3 f0, float f90, float NdotV)
{
	return f0 + (f90 - f0) * exp2((-5.55473f * NdotV - 6.983146f) * NdotV);
}

// Schlick's approximation to Fresnel term with Hoffman's improvement using the Lazanyi's error term
// Ref: "Fresnel Equations Considered Harmful" by Hoffman
// http://renderwonk.com/publications/mam2019/naty_mam2019.pdf for examples and explanation of f82 term
float3 EvalFresnelHoffman(float3 f0, float f82, float f90, float NdotS)
{
	const float alpha = 6.0f; //< Fixed to 6 in order to put peak angle for Lazanyi's error term at 82 degrees (f82)
	float3 a = 17.6513846f * (f0 - f82) + 8.166666f * (float3(1.0f, 1.0f, 1.0f) - f0);
	return saturate(f0 + (f90 - f0) * pow(1.0f - NdotS, 5.0f) - a * NdotS * pow(1.0f - NdotS, alpha));
}

float3 EvalFresnel(float3 f0, float f90, float NdotS)
{
	// Default is Schlick's approximation
	return EvalFresnelSchlick(f0, f90, NdotS);
}

// Attenuates F90 for very low F0 values
// Ref: "An Efficient and Physically Plausible Real-Time Shading Model" in ShaderX7 by Schuler
// Also see section "Overbright highlights" in Hoffman's 2010 "Crafting Physically Motivated Shading Models for Game Development" for discussion
// IMPORTANT: Note that when F0 is calculated using metalness, its value is never less than MIN_DIELECTRICS_F0, and therefore,
// this adjustment has no effect. To be effective, F0 must be authored separately, or calculated in different way. 
float ShadowedF90(float3 F0)
{
	// This scaler value is somewhat arbitrary, Schuler used 60 in his article. In here, we derive it from MIN_DIELECTRICS_F0 so
	// that it takes effect for any reflectance lower than least reflective dielectrics
	// const float t = 60.0f;
	const float t = (1.0f / MIN_DIELECTRICS_F0);
	return min(1.0f, t * Luminance(F0));
}

// -------------------------------------------------------------------------
//    Lambert
// -------------------------------------------------------------------------

// brdfPdf = cosTheta / PI, brdfWeight = diffuse / PI
float Lambertian(const BRDFData brdfData)
{
	return 1.0f;
}

float3 EvalLambertian(const BRDFData brdfData)
{
	return brdfData.diffuseReflectance * (brdfData.NdotL * ONE_OVER_PI);
}

// -------------------------------------------------------------------------
//    Phong
// -------------------------------------------------------------------------

// For derivation see "Phong Normalization Factor derivation" by Giesen
float PhongNormalizationTerm(float shininess)
{
	return (1.0f + shininess) * ONE_OVER_TWO_PI;
}

float3 EvalPhong(const BRDFData brdfData)
{
	// First convert roughness to shininess (Phong exponent)
	float shininess = BeckmannAlphaToShininess(brdfData.alpha);

	float3 R = reflect(-brdfData.L, brdfData.N);
	return brdfData.specularF0 * (PhongNormalizationTerm(shininess) * pow(max(0.0f, dot(R, brdfData.V)), shininess) * brdfData.NdotL);
}

// Samples a Phong distribution lobe oriented along +Z axis
// Ref: "Sampling Transformations Zoo" in <<Ray Tracing Gems>> by Shirley et al.
float3 SamplePhong(float3 V, float shininess, float2 u, out float pdf)
{
	float cosTheta = pow(1.0f - u.x, 1.0f / (1.0f + shininess));
	float sinTheta = sqrt(1.0f - cosTheta * cosTheta);

	float phi = TWO_PI * u.y;

	pdf = PhongNormalizationTerm(shininess) * pow(cosTheta, shininess);

	return float3(
		cos(phi) * sinTheta,
		sin(phi) * sinTheta,
		cosTheta);
}

float3 SamplePhong(float3 V, float2 alpha, float2 u)
{
	float shininess = BeckmannAlphaToShininess(dot(alpha, float2(0.5, 0.5)));
	float pdf;
	return SamplePhong(V, shininess, u, pdf);
}

// Samples the specular BRDF based on Phong, includes normalization term
float3 SampleSpecularPhong(float3 Vlocal, float alpha, float alphaSqr, float3 specularF0, float2 u, out float3 weight)
{
	// First convert roughness to shininess (Phong exponent)
	float shininess = BeckmannAlphaToShininess(alpha);

	float pdf;
	float3 LPhong = SamplePhong(Vlocal, shininess, u, pdf);

	// Sampled L is in "lobe space" - where Phong lobe is centered around +Z axis
	// We need to rotate it in direction of perfect reflection
	float3 Nlocal = float3(0, 0, 1);
	float3 lobeDirection = reflect(-Vlocal, Nlocal);
	float3 Llocal = RotatePoint(GetRotationFromZAxis(lobeDirection), LPhong);

	// Calculate the weight of the sample
	float3 Rlocal = reflect(-Llocal, Nlocal);
	float NdotL = max(0.00001f, dot(Nlocal, Llocal));
	weight = max(float3(0.0f, 0.0f, 0.0f), specularF0 * NdotL);

	// Unoptimized formula was:
	// weight = specularF0 * (PhongNormalizationTerm(shininess) * pow(max(0.0f, dot(Rlocal, Vlocal)), shininess) * NdotL) / pdf;

	return Llocal;
}

// -------------------------------------------------------------------------
//    Oren-Nayar 
// -------------------------------------------------------------------------

// Based on Ore-Nayar's qualitative model
// Ref: "Generalization of Lambert's Reflectance Model" by Oren & Nayar
float OrenNayar(BRDFData brdfData)
{
	// Oren-Nayar roughness (sigma) is in radians - use conversion from Beckmann roughness here
	float sigma = BeckmannAlphaToOrenNayarRoughness(brdfData.alpha);

	float thetaV = acos(brdfData.NdotV);
	float thetaL = acos(brdfData.NdotL);

	float alpha = max(thetaV, thetaL);
	float beta = min(thetaV, thetaL);

	// Calculate cosine of azimuth angles difference - by projecting L and V onto plane defined by N. Assume L, V, N are normalized.
	float3 l = brdfData.L - brdfData.NdotL * brdfData.N;
	float3 v = brdfData.V - brdfData.NdotV * brdfData.N;
	float cosPhiDifference = dot(normalize(v), normalize(l));

	float sigma2 = sigma * sigma;
	float A = 1.0f - 0.5f * (sigma2 / (sigma2 + 0.33f));
	float B = 0.45f * (sigma2 / (sigma2 + 0.09f));

	return (A + B * max(0.0f, cosPhiDifference) * sin(alpha) * tan(beta));
}

float3 EvalOrenNayar(const BRDFData brdfData) 
{
	return brdfData.diffuseReflectance * (OrenNayar(brdfData) * ONE_OVER_PI * brdfData.NdotL);
}

// -------------------------------------------------------------------------
//    Disney
// -------------------------------------------------------------------------

// Disney's diffuse term
// Ref: "Physically-Based Shading at Disney" by Burley
float DisneyDiffuse(const BRDFData brdfData)
{
	float FD90MinusOne = 2.0f * brdfData.roughness * brdfData.LdotH * brdfData.LdotH - 0.5f;

	float FDL = 1.0f + (FD90MinusOne * Pow5(1.0f - brdfData.NdotL));
	float FDV = 1.0F + (FD90MinusOne * Pow5(1.0f - brdfData.NdotV));

	return FDL * FDV;
}

float3 EvalDisneyDiffuse(const BRDFData brdfData)
{
	return brdfData.diffuseReflectance * (DisneyDiffuse(brdfData) * ONE_OVER_PI * brdfData.NdotL);
}

// Frostbite's version of Disney diffuse with energy normalization.
// Ref: "Moving Frostbite to Physically Based Rendering" by Lagarde & de Rousiers
float FrostbiteDisneyDiffuse(const BRDFData brdfData)
{
	float energyBias = 0.5f * brdfData.roughness;
	float energyFactor = lerp(1.0f, 1.0f / 1.51f, brdfData.roughness);

	float FD90MinusOne = energyBias + 2.0 * brdfData.LdotH * brdfData.LdotH * brdfData.roughness - 1.0f;

	float FDL = 1.0f + (FD90MinusOne * Pow5(1.0f - brdfData.NdotL));
	float FDV = 1.0f + (FD90MinusOne * Pow5(1.0f - brdfData.NdotV));

	return FDL * FDV * energyFactor;
}

float3 EvalFrostbiteDisneyDiffuse(const BRDFData brdfData)
{
	return brdfData.diffuseReflectance * (FrostbiteDisneyDiffuse(brdfData) * ONE_OVER_PI * brdfData.NdotL);
}

// -------------------------------------------------------------------------
//    Smith G term
// -------------------------------------------------------------------------

// Function to calculate `a` parameter for lambda functions needed in SmithG term
// This is a version for shape invariant (isotropic) NDFs
// NOTE: make sure NdotS is not negative
float Smith_G_a(float alpha, float NdotS)
{
	return NdotS / (max(0.00001f, alpha) * sqrt(1.0f - min(0.99999f, NdotS * NdotS)));
}

// Lambda function for Smith G term derived for GGX distribution
float Smith_G_Lambda_GGX(float a)
{
	return (-1.0f + sqrt(1.0f + (1.0f / (a * a)))) * 0.5f;
}

// Lambda function for Smith G term derived for Beckmann distribution
// This is Walter's rational approximation (avoids evaluating of error function)
// Ref: "Real-time Rendering", 4th edition, p.339 by Akenine-Moller et al.
// Note that this formulation is slightly optimized and different from Walter's
float Smith_G_Lambda_Beckmann_Walter(float a) 
{
	if (a < 1.6f) 
	{
		return (1.0f - (1.259f - 0.396f * a) * a) / ((3.535f + 2.181f * a) * a);
		// return ((1.0f + (2.276f + 2.577f * a) * a) / ((3.535f + 2.181f * a) * a)) - 1.0f; // Walter's original
	} 
	else 
	{
		return 0.0f;
	}
}

// Smith G1 term (maksing function)
// This non-optimized version uses NDF specific lambda function (G_Lambda) resolved bia macro based on selected NDF
float Smith_G1_General(float a)
{
	return 1.0f / (1.0f + Smith_G_Lambda(a));
}

// Smith G1 term (maksing function) optimized for GGX distribution (by substituting G_Lambda_GGX into G1)
float Smith_G1_GGX(float a)
{
	float a2 = a * a;
	return 2.0f / (sqrt((a2 + 1.0f) / a2) + 1.0f);
}

// Smith G1 term (masking function) further optimized for GGX distribution (by substituting G_a into G1_GGX)
float Smith_G1_GGX(float alpha, float NdotS, float alphaSquared, float NdotSSquared)
{
	return 2.0f / (sqrt(((alphaSquared * (1.0f - NdotSSquared)) + NdotSSquared) / NdotSSquared) + 1.0f);
}

// Smith G1 term (masking function) optimized for Beckmann distribution (by substituting G_Lambda_Beckmann_Walter into G1)
// Ref: "Microfacet Models for Refraction through Rough Surfaces" by Walter et al.
float Smith_G1_Beckmann_Walter(float a) 
{
	if (a < 1.6f) {
		return ((3.535f + 2.181f * a) * a) / (1.0f + (2.276f + 2.577f * a) * a);
	} else {
		return 1.0f;
	}
}

float Smith_G1_Beckmann_Walter(float alpha, float NdotS, float alphaSquared, float NdotSSquared) 
{
	return Smith_G1_Beckmann_Walter(Smith_G_a(alpha, NdotS));
}

// Smith G2 term (masking-shadowing function)
// Separable version assuming independent (uncorrelated) masking and shadowing, uses G1 functions for selected NDF
float Smith_G2_Separable(float alpha, float NdotL, float NdotV) 
{
	float aL = Smith_G_a(alpha, NdotL);
	float aV = Smith_G_a(alpha, NdotV);
	return Smith_G1(aL) * Smith_G1(aV);
}

// Smith G2 term (masking-shadowing function)
// Height correlated version - non-optimized, uses G_Lambda functions for selected NDF
float Smith_G2_Height_Correlated(float alpha, float NdotL, float NdotV) 
{
	float aL = Smith_G_a(alpha, NdotL);
	float aV = Smith_G_a(alpha, NdotV);
	return 1.0f / (1.0f + Smith_G_Lambda(aL) + Smith_G_Lambda(aV));
}

// Smith G2 term (masking-shadowing function) for GGX distribution
// Separable version assuming independent (uncorrelated) masking and shadowing - optimized by substituing G_Lambda for G_Lambda_GGX and 
// dividing by (4 * NdotL * NdotV) to cancel out these terms in specular BRDF denominator
// Ref: "Moving Frostbite to Physically Based Rendering" by Lagarde & de Rousiers
// Note that returned value is G2 / (4 * NdotL * NdotV) and therefore includes division by specular BRDF denominator
float Smith_G2_Separable_GGX_Lagarde(float alphaSquared, float NdotL, float NdotV) 
{
	float a = NdotV + sqrt(alphaSquared + NdotV * (NdotV - alphaSquared * NdotV));
	float b = NdotL + sqrt(alphaSquared + NdotL * (NdotL - alphaSquared * NdotL));
	return 1.0f / (a * b);
}

// Smith G2 term (masking-shadowing function) for GGX distribution
// Height correlated version - optimized by substituing G_Lambda for G_Lambda_GGX and dividing by (4 * NdotL * NdotV) to cancel out 
// the terms in specular BRDF denominator
// Ref: "Moving Frostbite to Physically Based Rendering" by Lagarde & de Rousiers
// Note that returned value is G2 / (4 * NdotL * NdotV) and therefore includes division by specular BRDF denominator
float Smith_G2_Height_Correlated_GGX_Lagarde(float alphaSquared, float NdotL, float NdotV) 
{
	float a = NdotV * sqrt(alphaSquared + NdotL * (NdotL - alphaSquared * NdotL));
	float b = NdotL * sqrt(alphaSquared + NdotV * (NdotV - alphaSquared * NdotV));
	return 0.5f / (a + b);
}

// Smith G2 term (masking-shadowing function) for GGX distribution
// Height correlated version - approximation by Hammon
// Ref: "PBR Diffuse Lighting for GGX + Smith Microsurfaces", slide 84 by Hammon
// Note that returned value is G2 / (4 * NdotL * NdotV) and therefore includes division by specular BRDF denominator
float Smith_G2_Height_Correlated_GGX_Hammon(float alpha, float NdotL, float NdotV) 
{
	return 0.5f / (lerp(2.0f * NdotL * NdotV, NdotL + NdotV, alpha));
}

// A fraction G2/G1 where G2 is height correlated can be expressed using only G1 terms
// Ref: "Implementing a Simple Anisotropic Rough Diffuse Material with Stochastic Evaluation", Appendix A by Heitz & Dupuy
float Smith_G2_Over_G1_Height_Correlated(float alpha, float alphaSquared, float NdotL, float NdotV) 
{
	float G1V = Smith_G1(alpha, NdotV, alphaSquared, NdotV * NdotV);
	float G1L = Smith_G1(alpha, NdotL, alphaSquared, NdotL * NdotL);
	return G1L / (G1V + G1L - G1V * G1L);
}

// Evaluates G2 for selected configuration (GGX/Beckmann, optimized/non-optimized, separable/height-correlated)
// Note that some paths aren't optimized too much...
// Also note that when USE_OPTIMIZED_G2 is specified, returned value will be: G2 / (4 * NdotL * NdotV) if GG-X is selected
float Smith_G2(float alpha, float alphaSquared, float NdotL, float NdotV) 
{

#if USE_OPTIMIZED_G2 && (MICROFACET_DISTRIBUTION == GGX)
#if USE_HEIGHT_CORRELATED_G2
#define G2_DIVIDED_BY_DENOMINATOR 1
	return Smith_G2_Height_Correlated_GGX_Lagarde(alphaSquared, NdotL, NdotV);
#else
#define G2_DIVIDED_BY_DENOMINATOR 1
	return Smith_G2_Separable_GGX_Lagarde(alphaSquared, NdotL, NdotV);
#endif

#else
#if USE_HEIGHT_CORRELATED_G2
	return Smith_G2_Height_Correlated(alpha, NdotL, NdotV);
#else
	return Smith_G2_Separable(alpha, NdotL, NdotV);
#endif
#endif

}

// -------------------------------------------------------------------------
//    Normal distribution functions
// -------------------------------------------------------------------------

float Beckmann_D(float alphaSqr, float NdotH)
{
	float cos2Theta = NdotH * NdotH;
	float numerator = exp((cos2Theta - 1.0f) / (alphaSqr * cos2Theta));
	float denominator = PI * alphaSqr * cos2Theta * cos2Theta;
	return numerator / denominator;
}

float GGX_D(float alphaSqr, float NdotH)
{
	float b = ((alphaSqr - 1.0f) * NdotH * NdotH + 1.0f);
	return alphaSqr / (PI * b * b);
}

// -------------------------------------------------------------------------
//    Microfacet model
// -------------------------------------------------------------------------

// Samples a microfacet normal for the GGX distribution using VNDF method
// Ref: "Sampling the GGX Distribution of Visible Normals" by Heitz
// https://hal.inria.fr/hal-00996995v1/document and http://jcgt.org/published/0007/04/01/
// Random variable 'u' must be in [0, 1) interval
// PDF is 'G1(NdotV) * D'
float3 SampleGGXVNDF(float3 Ve, float2 alpha, float2 u)
{
	// Section 3.2: transforming the view direction to the hemisphere configuration
	float3 Vh = normalize(float3(alpha.x * Ve.x, alpha.y * Ve.y, Ve.z));

	// Section 4.1: orthonormal basis (with special case if cross product is 0)
	float lenSqr = Vh.x * Vh.x + Vh.y * Vh.y;
	float3 T1 = lenSqr > 0.0f ? float3(-Vh.y, Vh.x, 0.0) * rsqrt(lenSqr) : float3(1.0, 0.0, 0.0);
	float3 T2 = cross(Vh, T1);

	// Section 4.2: parameterization of the projected area
	float r = sqrt(u.x);
	float phi = TWO_PI * u.y;
	float t1 = r * cos(phi);
	float t2 = r * sin(phi);
	float s = 0.5f * (1.0f + Vh.z);
	t2 = lerp(sqrt(1.0f - t1 * t1), t2, s);

	// Section 4.3: reprojection onto hemisphere
	float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0f, 1.0f - t1 * t1 - t2 * t2)) * Vh;

	// Section 3.4: transforming the normal back to the ellipsoid configuration
	return normalize(float3(alpha.x * Nh.x, alpha.y * Nh.y, max(0.0f, Nh.z)));
}

// PDF of sampling a reflection vector L using 'sampleGGXVNDF'.
// Note that PDF of sampling given microfacet normal is (G1 * D) when vectors are in local space (in the hemisphere around shading normal). 
// Remaining terms (1.0f / (4.0f * NdotV)) are specific for reflection case, and come from multiplying PDF by jacobian of reflection operator
float SampleGGXVNDFReflectionPdf(float alpha, float alphaSquared, float NdotH, float NdotV, float LdotH) {
	NdotH = max(0.00001f, NdotH);
	NdotV = max(0.00001f, NdotV);
	return (GGX_D(max(0.00001f, alphaSquared), NdotH) * Smith_G1_GGX(alpha, NdotV, alphaSquared, NdotV * NdotV)) / (4.0f * NdotV);
}

// "Walter's trick" is an adjustment of alpha value for Walter's sampling to reduce maximal weight of sample to about 4
// Ref: "Microfacet Models for Refraction through Rough Surfaces" by Walter et al., page 8
float WaltersTrick(float alpha, float NdotV) 
{
	return (1.2f - 0.2f * sqrt(abs(NdotV))) * alpha;
}

// PDF of sampling a reflection vector L using 'sampleBeckmannWalter'.
// Note that PDF of sampling given microfacet normal is (D * NdotH). Remaining terms (1.0f / (4.0f * LdotH)) are specific for
// reflection case, and come from multiplying PDF by jacobian of reflection operator
float SampleBeckmannWalterReflectionPdf(float alpha, float alphaSquared, float NdotH, float NdotV, float LdotH) 
{
	NdotH = max(0.00001f, NdotH);
	LdotH = max(0.00001f, LdotH);
	return Beckmann_D(max(0.00001f, alphaSquared), NdotH) * NdotH / (4.0f * LdotH);
}

// Samples a microfacet normal for the Beckmann distribution using walter's method.
// Source: "Microfacet Models for Refraction through Rough Surfaces" by Walter et al.
// PDF is 'D * NdotH'
float3 SampleBeckmannWalter(float3 Vlocal, float2 alpha2D, float2 u) 
{
	float alpha = dot(alpha2D, float2(0.5f, 0.5f));

	// Equations (28) and (29) from Walter's paper for Beckmann distribution
	float tanThetaSquared = -(alpha * alpha) * log(1.0f - u.x);
	float phi = TWO_PI * u.y;

	// Calculate cosTheta and sinTheta needed for conversion to H vector
	float cosTheta = rsqrt(1.0f + tanThetaSquared);
	float sinTheta = sqrt(1.0f - cosTheta * cosTheta);

	// Convert sampled spherical coordinates to H vector
	return normalize(float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta));
}

// Weight for the reflection ray sampled from GGX distribution using VNDF method
float SpecularSampleWeightGGXVNDF(float alpha, float alphaSquared, float NdotL, float NdotV, float HdotL, float NdotH) 
{
#if USE_HEIGHT_CORRELATED_G2
	return Smith_G2_Over_G1_Height_Correlated(alpha, alphaSquared, NdotL, NdotV);
#else 
	return Smith_G1_GGX(alpha, NdotL, alphaSquared, NdotL * NdotL);
#endif
}

// Weight for the reflection ray sampled from Beckmann distribution using Walter's method
float SpecularSampleWeightBeckmannWalter(float alpha, float alphaSquared, float NdotL, float NdotV, float HdotL, float NdotH) 
{
	return (HdotL * Smith_G2(alpha, alphaSquared, NdotL, NdotV)) / (NdotV * NdotH);
}

// Samples a reflection ray from the rough surface using selected microfacet distribution and sampling method
// Resulting weight includes multiplication by cosine (NdotL) term
float3 SampleSpecularMicrofacet(float3 Vlocal, float alpha, float alphaSquared, float3 specularF0, float2 u, out float3 weight) 
{

	// Sample a microfacet normal (H) in local space
	float3 Hlocal;
	if (alpha == 0.0f) {
		// Fast path for zero roughness (perfect reflection), also prevents NaNs appearing due to divisions by zeroes
		Hlocal = float3(0.0f, 0.0f, 1.0f);
	} else {
		// For non-zero roughness, this calls VNDF sampling for GG-X distribution or Walter's sampling for Beckmann distribution
		Hlocal = SampleSpecularHalfVector(Vlocal, float2(alpha, alpha), u);
	}

	// Reflect view direction to obtain light vector
	float3 Llocal = reflect(-Vlocal, Hlocal);

	// Note: HdotL is same as HdotV here
	// Clamp dot products here to small value to prevent numerical instability. Assume that rays incident from below the hemisphere have been filtered
	float HdotL = max(0.00001f, min(1.0f, dot(Hlocal, Llocal)));
	const float3 Nlocal = float3(0.0f, 0.0f, 1.0f);
	float NdotL = max(0.00001f, min(1.0f, dot(Nlocal, Llocal)));
	float NdotV = max(0.00001f, min(1.0f, dot(Nlocal, Vlocal)));
	float NdotH = max(0.00001f, min(1.0f, dot(Nlocal, Hlocal)));
	float3 F = EvalFresnel(specularF0, ShadowedF90(specularF0), HdotL);

	// Calculate weight of the sample specific for selected sampling method 
	// (this is microfacet BRDF divided by PDF of sampling method - notice how most terms cancel out)
	weight = F * SpecularSampleWeight(alpha, alphaSquared, NdotL, NdotV, HdotL, NdotH);

	return Llocal;
}

// Evaluates microfacet specular BRDF
float3 EvalMicrofacet(const BRDFData data) 
{

	float D = Microfacet_D(max(0.00001f, data.alphaSqr), data.NdotH);
	float G2 = Smith_G2(data.alpha, data.alphaSqr, data.NdotL, data.NdotV);
	// float3 F = EvalFresnel(data.specularF0, ShadowedF90(data.specularF0), data.VdotH); //< Unused, F is precomputed already

#if G2_DIVIDED_BY_DENOMINATOR
	return data.F * (G2 * D * data.NdotL);
#else
	return ((data.F * G2 * D) / (4.0f * data.NdotL * data.NdotV)) * data.NdotL;
#endif
}

// -------------------------------------------------------------------------
//    Combined BRDF
// -------------------------------------------------------------------------

// Precalculates commonly used terms in BRDF evaluation
// Clamps around dot products prevent NaNs and ensure numerical stability, but make sure to 
// correctly ignore rays outside of the sampling hemisphere, by using 'bVbackfacing' and 'bLbackfacing' flags
BRDFData PrepareBRDFData(float3 N, float3 L, float3 V, MaterialProperties material)
{
	BRDFData brdfData = (BRDFData) 0;

	// Evaluate VNHL vectors
	float3 H = normalize(L + V);
	brdfData.V = V;
	brdfData.N = N;
	brdfData.L = L;
	brdfData.H = H;

	float NdotL = dot(N, L);
	float NdotV = dot(N, V);
	brdfData.bLbackfacing = NdotL <= 0.0f;
	brdfData.bVbackfacing = NdotV <= 0.0f;

	// Clamp NdotS to prevent numerical instability. Assume vectors below the hemisphere will be filtered using 'Vbackfacing' and 'Lbackfacing' flags
	brdfData.NdotL = min(max(0.00001f, NdotL), 1.0f);
	brdfData.NdotV = min(max(0.00001f, NdotV), 1.0f);

	brdfData.LdotH = saturate(dot(L, H));
	brdfData.NdotH = saturate(dot(N, H));
	brdfData.VdotH = saturate(dot(V, H));

	// Unpack material properties
	brdfData.specularF0 = BaseColorToSpecularF0(material.baseColor, material.metallic);
	brdfData.diffuseReflectance = BaseColorToDiffuseReflectance(material.baseColor, material.metallic);

	brdfData.roughness = material.roughness;
	brdfData.alpha = material.roughness * material.roughness;
	brdfData.alphaSqr = brdfData.alpha * brdfData.alpha;

	// Pre-calculate some more BRDF terms
	brdfData.F = EvalFresnel(brdfData.specularF0, ShadowedF90(brdfData.specularF0), brdfData.LdotH);

	return brdfData;
}

// This is an entry point for evaluation of all other BRDFs based on selected configuration (for direct light)
float3 EvalCombinedBRDF(float3 N, float3 L, float3 V, MaterialProperties material)
{
	// Prepare data needed for BRDF evaluation - unpack material properties and evaluate commonly used terms (e.g. Fresnel, NdotL, ...)
	const BRDFData brdfData = PrepareBRDFData(N, L, V, material);

	// Ignore V and L rays "below" the hemisphere
	if (brdfData.bLbackfacing || brdfData.bVbackfacing) return float3(0, 0, 0);

	// Eval specular and diffuse BRDFs
	float3 diffuse = EvalDiffuse(brdfData);
	float3 specular = EvalSpecular(brdfData);

	// Combine specular and diffuse layers
#if COMBINE_BRDFS_WITH_FRESNEL
	// Specular is already multiplied by F, just attenuate diffuse
	return (float3(1, 1, 1) - brdfData.F) * diffuse + specular;
#else
	return diffuse + specular;
#endif
}

// This is an entry point for evaluation of all other BRDFs based on selected configuration (for indirect light)
bool EvalIndirectCombinedBRDF(float2 u, float3 shadingNormal, float3 geometryNormal, float3 V, MaterialProperties material, const int brdfType,
	out float3 rayDirection, out float3 sampleWeight)
{
	// Ignore incident ray coming from "below" the hemisphere
	if (dot(shadingNormal, V) <= 0.0f) return false;

	// Transform view direction into local space of our sampling routines
	// (local space is oriented so that its positive Z axis points along the shading normal)
	float4 qRotationToZ = GetRotationToZAxis(shadingNormal);
	float3 Vlocal = RotatePoint(qRotationToZ, V);
	const float3 Nlocal = float3(0, 0, 1);

	float3 rayDirectionLocal = float3(0, 0, 0);

	if (brdfType == DIFFUSE_TYPE)
	{
		// Sample diffuse ray using consine-weighted hemisphere sampling
		rayDirectionLocal = SampleHemisphere(u);
		const BRDFData brdfData = PrepareBRDFData(Nlocal, rayDirectionLocal, Vlocal, material);

		// Function 'DiffuseTerm' is predivided by PDF of sampling the consine weighted hemisphere
		sampleWeight = brdfData.diffuseReflectance * DiffuseTerm(brdfData);

	#if COMBINE_BRDFS_WITH_FRESNEL
		// Sample a half-vector of specular BRDF. Note that we're reusing random variable 'u' here, but correctly 
		// it should be an new independent random number
		float3 HSpecular = SampleSpecularHalfVector(Vlocal, float2(brdfData.alpha, brdfData.alpha), u);

		// Clamp HdotL to small value to prevent numerical instability. Assume that rays incident from below the hemisphere have been filtered
		float VdotH = max(0.00001f, min(1.0f, dot(Vlocal, HSpecular)));
		sampleWeight *= (float3(1, 1, 1) - EvalFresnel(brdfData.specularF0, ShadowedF90(brdfData.specularF0), VdotH));
	#endif
	}
	else if (brdfType == SPECULAR_TYPE)
	{
		const BRDFData brdfData = PrepareBRDFData(Nlocal, float3(0, 0, 1) /* unused L vector */, Vlocal, material);
		rayDirectionLocal = SampleSpecular(Vlocal, brdfData.alpha, brdfData.alphaSqr, brdfData.specularF0, u, sampleWeight);
	}

	// Prevent tracing direction with no contribution
	if (Luminance(sampleWeight) == 0) return false;

	// Transform sampled direction Llocal back to V vector space
	rayDirection = normalize(RotatePoint(InvertRotation(qRotationToZ), rayDirectionLocal));

	// Prevent tracing direction "under" the hemisphere (behind the triangle)
	if (dot(geometryNormal, rayDirection) <= 0) return false;

	return true;
}

#endif // BSDF_INCLUDED

// NOTE: These implementations return BRDF values already divided by the PDf, while also accounting for 
// the rendering equation's cosine term.
