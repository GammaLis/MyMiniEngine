#ifndef PRECOMPUTING_HLSLI
#define PRECOMPUTING_HLSLI

static const float Pi = 3.14159265359;
static const float TwoPi = 6.2831853071796;
static const float PiOver2 = 1.57079632679;
static const float RadToDeg = 180.0 / Pi;
static const float DegToRad = Pi / 180.0;

// ------------------------------------------------------------
float Pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

// DVF
// D
float D_GGX(float NdotH, float roughness)
{
    float a = NdotH * roughness;
    float k = roughness / (1.0 - NdotH * NdotH + a * a);
    return k * k * (1.0 / Pi);

    // float a2 = roughness * roughness;
    // float f = (NdotH * a2 - NdotH) * NdotH + 1.0;
    // return a2 / (Pi * f * f);
}

// V
float V_SmithGGXCorrelated(float NdotV, float NdotL, float roughness)
{
    float a2 = roughness * roughness;
    float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / (GGXV + GGXL);
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

// https://learnopengl.com/PBR/IBL/Diffuse-irradiance
float3 F_SchlickRoughness(float VdotH, float3 F0, float roughness)
{
    return F0 + (max(1.0 - roughness, F0) - F0) * pow(1.0 - VdotH, 5.0);
}

// ------------------------------------------------------------
// low dependency
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

// ------------------------------------------------------------
// sampling
// 
// phi = 2*Pi * xi
// cos(theta)^2 = (1- xi) / (xi * (a^2 - 1) + 1)
float3 HemisphereImportanceSampleGGX(float2 u, float a) // pdf = D(a) * cosTheta
{
    float phi = TwoPi * u.x;
    // NOTE: (aa-1) == (a+1)(a-1) produces better fp accuracy   - Filament CubemapIBL.cpp
    float cosTheta2 = (1 - u.y) / (1 + (a + 1) * ((a - 1) * u.y));
    float cosTheta = sqrt(cosTheta2);
    float sinTheta = sqrt(1.0 - cosTheta2);
    return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

float3 HemisphereCosSample(float2 u)    // pdf = cosTheta / Pi
{
    float phi = TwoPi * u.x;
    float cosTheta2 = 1 - u.y;
    float cosTheta = sqrt(cosTheta2);
    float sinTheta = sqrt(1 - cosTheta2);
    return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

float3 HemisphereUniformSample(float2 u)    // pdf = 1.0 / 2Pi
{
    float phi = TwoPi * u.x;
    float cosTheta = 1 - u.y;
    float sinTheta = sqrt(1 - cosTheta * cosTheta);
    return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

/**
 * Importance sampling Charlie
 * In order to pick the most significative samples and increase the convergence rate, we chose to 
 * rely on Charlie's distribution function for the pdf as we do in HemisphereImportanceSampleGGX.
 *
 * pdf() = DCharlie(h) * <n,h>
 * sin(theta) = xi ^ (a / (2a + 1))
 */
float3 HemisphereImportanceSampleDCharlie(float2 u, float a)    // pdf = DCharlie() * cosTheta
{
    float phi = TwoPi * u.x;
    float sinTheta = pow(u.y, a / (2.0 * a + 1.0));
    float cosTheta = sqrt(1.0 - sinTheta);
    return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

// ------------------------------------------------------------
// coordinate system from a vector
void CoordinateSystem(float3 v0, inout float3 v1, inout float3 v2)
{
	if (abs(v0.x) > abs(v0.y))
		v1 = float3(-v0.z, 0, v0.x);
	else
		v1 = float3(0, v0.z, -v0.y);
	v1 = normalize(v1);
	v2 = cross(v0, v1);
}

// ------------------------------------------------------------
// just for fun
// https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
float2 OctWrap(float2 v)
{
    return (1.0 - abs(v.yx)) * (v.xy >= 0.0 ? 1.0 : -1.0);
}
float2 OctahedronMapping(float3 n)
{
    // projection onto octahedron
    n /= (abs(n.x) + abs(n.y) + abs(n.z));

    // out-folding of the downward faces
    if (n.z < 0.0)
        n.xy = OctWrap(n.xy);

    // mapping to [0,1]^2 texture space
    return n.xy * 0.5 + 0.5;
}

float3 OctahedronUnmapping(float2 f)
{
    f = f * 2.0 - 1.0;

    // https://twitter.com/Stubbesaurus/status/937994790553227264
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += n.xy >= 0.0 ? -t : t;
    return normalize(n);
}

// ------------------------------------------------------------

#endif	// PRECOMPUTING_HLSLI

/**
 * https://google.github.io/filament/Filament.html
 */

/**
 * Importance sampling GGX - Trowbridge-Reitz
 * imporance samples are chosen to integrate Dggx() * cos(theta) over the hemisphere
 * all calculates are made in tangent space, with N = (0, 0, 1)
 * H = Importance_Sample_GGX();
 * V = N
 * NdotH = dot([0, 0, 1], H) = H.z;
 * L = reflect(-V, H) = 2 * VdotH * H - V;
 * NdotL = cos(2 * theta) = cosTheta^2 - sinTheta^2 = 2* NdotH^2 - 1;
 * pdf() = D(h) * <h, n> * |J(h)|
 *     |J(h)| = 1 / 4<v, h>
 *
 * "Real-time Shading with Filtered Importance Sampling", Jaroslav Krivanek
 * "GPU-Based Importance Sampling, GPU Gems 3", Mark Colbert
 *     lod = log4(K* Ws / Wp )
 * log4(K) = 1, works well for box filters  -> K = 4
 * Ws = 1 / (N * pdf), solid angle of an importance sample
 * Wp = 4PI / texel_Count, solid angle of a sample in the base cubemap
 */
