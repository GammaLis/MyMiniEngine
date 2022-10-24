#ifndef MONTE_CARLO_INCLUDED
#define MONTE_CARLO_INCLUDED

// Ref: UE - MonteCarlo.ush

#include "Common.hlsl"

/*=============================================================================
	MonteCarlo.usf: Monte Carlo integration of distributions
=============================================================================*/

// [ Duff et al. 2017, "Building an Orthonormal Basis, Revisited" ]
// Discontinuity at TangentZ.z == 0
float3x3 GetTangentBasis( float3 TangentZ )
{
	const float Sign = TangentZ.z >= 0 ? 1 : -1;
	const float a = -rcp( Sign + TangentZ.z );
	const float b = TangentZ.x * TangentZ.y * a;
	
	float3 TangentX = { 1 + Sign * a * Pow2( TangentZ.x ), Sign * b, -Sign * TangentZ.x };
	float3 TangentY = { b,  Sign + a * Pow2( TangentZ.y ), -TangentZ.y };

	return float3x3( TangentX, TangentY, TangentZ );
}

// [Frisvad 2012, "Building an Orthonormal Basis from a 3D Unit Vector Without Normalization"]
// Discontinuity at TangentZ.z < -0.9999999f
float3x3 GetTangentBasisFrisvad(float3 TangentZ)
{
	float3 TangentX;
	float3 TangentY;

	if (TangentZ.z < -0.9999999f)
	{
		TangentX = float3(0, -1, 0);
		TangentY = float3(-1, 0, 0);
	}
	else
	{
		float A = 1.0f / (1.0f + TangentZ.z);
		float B = -TangentZ.x * TangentZ.y * A;
		TangentX = float3(1.0f - TangentZ.x * TangentZ.x * A, B, -TangentZ.x);
		TangentY = float3(B, 1.0f - TangentZ.y * TangentZ.y * A, -TangentZ.y);
	}

	return float3x3( TangentX, TangentY, TangentZ );
}

float3 TangentToWorld( float3 Vec, float3 TangentZ )
{
	return mul( Vec, GetTangentBasis( TangentZ ) );
}

float3 WorldToTangent(float3 Vec, float3 TangentZ)
{
	return mul(GetTangentBasis(TangentZ), Vec);
}


float2 Hammersley( uint Index, uint NumSamples, uint2 Random )
{
	float E1 = frac( (float)Index / NumSamples + float( Random.x & 0xffff ) / (1<<16) );
	float E2 = float( reversebits(Index) ^ Random.y ) * 2.3283064365386963e-10;
	return float2( E1, E2 );
}

float2 Hammersley16( uint Index, uint NumSamples, uint2 Random )
{
	float E1 = frac( (float)Index / NumSamples + float( Random.x ) * (1.0 / 65536.0) );
	float E2 = float( ( reversebits(Index) >> 16 ) ^ Random.y ) * (1.0 / 65536.0);
	return float2( E1, E2 );
}

// http://extremelearning.com.au/a-simple-method-to-construct-isotropic-quasirandom-blue-noise-point-sequences/
float2 R2Sequence( uint Index )
{
	const float Phi = 1.324717957244746;
	const float2 a = float2( 1.0 / Phi, 1.0 / Pow2(Phi) );
	return frac( a * Index );
}

// R2 Jittered point set
// These seem to be garbage so use at your own risk. Jitter is not large enough for low sample counts. Larger jitter overlaps neighboring samples unevenly.
float2 JitteredR2( uint Index, uint NumSamples, float2 Jitter, float JitterAmount = 0.5 )
{
	const float Phi = 1.324717957244746;
	const float2 a = float2( 1.0 / Phi, 1.0 / Pow2(Phi) );
	const float d0 = 0.76;
	const float i0 = 0.7;

	return frac( a * float(Index) + ( JitterAmount * 0.5 * d0 * sqrt(PI) * rsqrt( float(NumSamples) ) ) * Jitter );
}

// R2 Jittered point sequence. Progressive
float2 JitteredR2( uint Index, float2 Jitter, float JitterAmount = 0.5 )
{
	const float Phi = 1.324717957244746;
	const float2 a = float2( 1.0 / Phi, 1.0 / Pow2(Phi) );
	const float d0 = 0.76;
	const float i0 = 0.7;

	return frac( a * Index + ( JitterAmount * 0.25 * d0 * sqrt(PI) * rsqrt( Index - i0 ) ) * Jitter );
}


///

float2 UniformSampleDisk( float2 E )
{
	float Theta = 2 * PI * E.x;
	float Radius = sqrt( E.y );
	return Radius * float2( cos( Theta ), sin( Theta ) );
}

// Returns a point on the unit circle and a radius in z
float3 ConcentricDiskSamplingHelper(float2 E)
{
	float2 p = 2 * E - 1;
	float2 a = abs(p);
	float Lo = min(a.x, a.y);
	float Hi = max(a.x, a.y);
	float Epsilon = 5.42101086243e-20; // 2^-64 (this avoids 0/0 without changing the rest of the mapping)
	float Phi = (PI / 4) * (Lo / (Hi + Epsilon) + 2 * float(a.y >= a.x));
	float Radius = Hi;
	// copy sign bits from p
	const uint SignMask = 0x80000000;
	float2 Disk = asfloat((asuint(float2(cos(Phi), sin(Phi))) & ~SignMask) | (asuint(p) & SignMask));
	// return point on the circle as well as the radius
	return float3(Disk, Radius);
}

float2 UniformSampleDiskConcentric( float2 E )
{
	float3 Result = ConcentricDiskSamplingHelper(E);
	return Result.xy * Result.z; // uniform sampling
}

// based on the approximate equal area transform from
// http://marc-b-reynolds.github.io/math/2017/01/08/SquareDisc.html
float2 UniformSampleDiskConcentricApprox( float2 E )
{
	float2 sf = E * sqrt(2.0) - sqrt(0.5);	// map 0..1 to -sqrt(0.5)..sqrt(0.5)
	float2 sq = sf*sf;
	float root = sqrt(2.0*max(sq.x, sq.y) - min(sq.x, sq.y));
	if (sq.x > sq.y)
	{
		sf.x = sf.x > 0 ? root : -root;
	}
	else
	{
		sf.y = sf.y > 0 ? root : -root;
	}
	return sf;
}

// PDF = 1 / (4 * PI)
float4 UniformSampleSphere( float2 E )
{
	float Phi = 2 * PI * E.x;
	float CosTheta = 1 - 2 * E.y;
	float SinTheta = sqrt( 1 - CosTheta * CosTheta );

	float3 H;
	H.x = SinTheta * cos( Phi );
	H.y = SinTheta * sin( Phi );
	H.z = CosTheta;

	float PDF = 1.0 / (4 * PI);

	return float4( H, PDF );
}

// PDF = 1 / (2 * PI)
float4 UniformSampleHemisphere( float2 E )
{
	float Phi = 2 * PI * E.x;
	float CosTheta = E.y;
	float SinTheta = sqrt( 1 - CosTheta * CosTheta );

	float3 H;
	H.x = SinTheta * cos( Phi );
	H.y = SinTheta * sin( Phi );
	H.z = CosTheta;

	float PDF = 1.0 / (2 * PI);

	return float4( H, PDF );
}

// PDF = NoL / PI
float4 CosineSampleHemisphere( float2 E )
{
	float Phi = 2 * PI * E.x;
	float CosTheta = sqrt(E.y);
	float SinTheta = sqrt(1 - CosTheta * CosTheta);

	float3 H;
	H.x = SinTheta * cos(Phi);
	H.y = SinTheta * sin(Phi);
	H.z = CosTheta;

	float PDF = CosTheta * (1.0 / PI);

	return float4(H, PDF);
}

// PDF = NoL / PI
float4 CosineSampleHemisphereConcentric(float2 E)
{
	float3 Result = ConcentricDiskSamplingHelper(E);
	float SinTheta = Result.z;
	float CosTheta = sqrt(1 - SinTheta * SinTheta);
	return float4(Result.xy * SinTheta, CosTheta, CosTheta * (1.0 / PI));
}

// PDF = NoL / PI
float4 CosineSampleHemisphere( float2 E, float3 N ) 
{
	float3 H = UniformSampleSphere( E ).xyz;
	H = normalize( N + H );

	float PDF = dot(H, N) * (1.0 /  PI);

	return float4( H, PDF );
}

float4 UniformSampleCone( float2 E, float CosThetaMax )
{
	float Phi = 2 * PI * E.x;
	float CosTheta = lerp( CosThetaMax, 1, E.y );
	float SinTheta = sqrt( 1 - CosTheta * CosTheta );

	float3 L;
	L.x = SinTheta * cos( Phi );
	L.y = SinTheta * sin( Phi );
	L.z = CosTheta;

	float PDF = 1.0 / ( 2 * PI * (1 - CosThetaMax) );

	return float4( L, PDF );
}

// Same as the function above, but uses SinThetaMax^2 as the parameter
// so that the solid angle can be computed more accurately for very small angles
// The caller is expected to ensure that SinThetaMax2 is <= 1
float4 UniformSampleConeRobust(float2 E, float SinThetaMax2)
{
	float Phi = 2 * PI * E.x;
	// The expression 1-sqrt(1-x) is susceptible to catastrophic cancelation.
	// Instead, use a series expansion about 0 which is accurate within 10^-7
	// and much more numerically stable.
	float OneMinusCosThetaMax = SinThetaMax2 < 0.01 ? SinThetaMax2 * (0.5 + 0.125 * SinThetaMax2) : 1 - sqrt(1 - SinThetaMax2);

	float CosTheta = 1 - OneMinusCosThetaMax * E.y;
	float SinTheta = sqrt(1 - CosTheta * CosTheta);

	float3 L;
	L.x = SinTheta * cos(Phi);
	L.y = SinTheta * sin(Phi);
	L.z = CosTheta;
	float PDF = 1.0 / (2 * PI * OneMinusCosThetaMax);

	return float4(L, PDF);
}

float UniformConeSolidAngle(float SinThetaMax2)
{
	float OneMinusCosThetaMax = SinThetaMax2 < 0.01 ? SinThetaMax2 * (0.5 + 0.125 * SinThetaMax2) : 1 - sqrt(1 - SinThetaMax2);
	return 2 * PI * OneMinusCosThetaMax;
}

// Same as the function above, but uses a concentric mapping
float4 UniformSampleConeConcentricRobust(float2 E, float SinThetaMax2)
{
	// The expression 1-sqrt(1-x) is susceptible to catastrophic cancelation.
	// Instead, use a series expansion about 0 which is accurate within 10^-7
	// and much more numerically stable.
	float OneMinusCosThetaMax = SinThetaMax2 < 0.01 ? SinThetaMax2 * (0.5 + 0.125 * SinThetaMax2) : 1 - sqrt(1 - SinThetaMax2);
	float3 Result = ConcentricDiskSamplingHelper(E);
	float SinTheta = Result.z * sqrt(SinThetaMax2);
	float CosTheta = sqrt(1 - SinTheta * SinTheta);

	float3 L = float3(Result.xy * SinTheta, CosTheta);
	float PDF = 1.0 / (2 * PI * OneMinusCosThetaMax);

	return float4(L, PDF);
}

// PDF = D * NoH / (4 * VoH)
float4 ImportanceSampleGGX( float2 E, float a2 )
{
	float Phi = 2 * PI * E.x;
	float CosTheta = sqrt( (1 - E.y) / ( 1 + (a2 - 1) * E.y ) );
	float SinTheta = sqrt( 1 - CosTheta * CosTheta );

	float3 H;
	H.x = SinTheta * cos( Phi );
	H.y = SinTheta * sin( Phi );
	H.z = CosTheta;
	
	float d = ( CosTheta * a2 - CosTheta ) * CosTheta + 1;
	float D = a2 / ( PI*d*d );
	float PDF = D * CosTheta;

	return float4( H, PDF );
}

float VisibleGGXPDF(float3 V, float3 H, float a2)
{
	float NoV = V.z;
	float NoH = H.z;
	float VoH = dot(V, H);

	float d = (NoH * a2 - NoH) * NoH + 1;
	float D = a2 / (PI*d*d);

	float PDF = 2 * VoH * D / (NoV + sqrt(NoV * (NoV - NoV * a2) + a2));
	return PDF;
}

float VisibleGGXPDF_aniso(float3 V, float3 H, float2 Alpha)
{
	float NoV = V.z;
	float NoH = H.z;
	float VoH = dot(V, H);
	float a2 = Alpha.x * Alpha.y;
	float3 Hs = float3(Alpha.y * H.x, Alpha.x * H.y, a2 * NoH);
	float S = dot(Hs, Hs);
	float D = (1.0f / PI) * a2 * Square(a2 / S);
	float LenV = length(float3(V.x * Alpha.x, V.y * Alpha.y, NoV));
	float Pdf = (2 * D * VoH) / (NoV + LenV);
	return Pdf;
}

// [ Heitz 2018, "Sampling the GGX Distribution of Visible Normals" ]
// http://jcgt.org/published/0007/04/01/
// PDF = G_SmithV * VoH * D / NoV / (4 * VoH)
// PDF = G_SmithV * D / (4 * NoV)
float4 ImportanceSampleVisibleGGX( float2 DiskE, float a2, float3 V )
{
	// NOTE: See below for anisotropic version that avoids this sqrt
	float a = sqrt(a2);

	// stretch
	float3 Vh = normalize( float3( a * V.xy, V.z ) );

	// Stable tangent basis based on V
	// Tangent0 is orthogonal to N
	float LenSq = Vh.x * Vh.x + Vh.y * Vh.y;
	float3 Tangent0 = LenSq > 0 ? float3(-Vh.y, Vh.x, 0) * rsqrt(LenSq) : float3(1, 0, 0);
	float3 Tangent1 = cross(Vh, Tangent0);

	float2 p = DiskE;
	float s = 0.5 + 0.5 * Vh.z;
	p.y = (1 - s) * sqrt( 1 - p.x * p.x ) + s * p.y;

	float3 H;
	H  = p.x * Tangent0;
	H += p.y * Tangent1;
	H += sqrt( saturate( 1 - dot( p, p ) ) ) * Vh;

	// unstretch
	H = normalize( float3( a * H.xy, max(0.0, H.z) ) );

	return float4(H, VisibleGGXPDF(V, H, a2));
}

// [ Heitz 2018, "Sampling the GGX Distribution of Visible Normals" ]
// http://jcgt.org/published/0007/04/01/
// PDF = G_SmithV * VoH * D / NoV / (4 * VoH)
// PDF = G_SmithV * D / (4 * NoV)
float4 ImportanceSampleVisibleGGX_aniso(float2 DiskE, float2 Alpha, float3 V)
{
	// stretch
	float3 Vh = normalize(float3(Alpha * V.xy, V.z));

	// Stable tangent basis based on V
	float LenSq = Vh.x * Vh.x + Vh.y * Vh.y;
	float3 Tx = LenSq > 0 ? float3(-Vh.y, Vh.x, 0) * rsqrt(LenSq) : float3(1, 0, 0);
	float3 Ty = cross(Vh, Tx);

	float2 p = DiskE;
	float s = 0.5 + 0.5 * Vh.z;
	p.y = lerp(sqrt(1 - p.x * p.x), p.y, s);

	float3 H = p.x * Tx + p.y * Ty + sqrt(saturate(1 - dot(p, p))) * Vh;

	// unstretch
	H = normalize(float3(Alpha * H.xy, max(0.0, H.z)));

	return float4(H, VisibleGGXPDF_aniso(V, H, Alpha));
}

// Multiple importance sampling power heuristic of two functions with a power of two. 
// [Veach 1997, "Robust Monte Carlo Methods for Light Transport Simulation"]
float MISWeight( uint Num, float PDF, uint OtherNum, float OtherPDF )
{
	float Weight = Num * PDF;
	float OtherWeight = OtherNum * OtherPDF;
	return Weight * Weight / (Weight * Weight + OtherWeight * OtherWeight);
}

// Multiple importance sampling power heuristic of two functions with a power of two. 
float MISWeightRobust(float Pdf, float OtherPdf) 
{
	// The straightforward implementation above is prone to numerical overflow and divisions by 0
	// and does not work well with +inf inputs.

	// We want this function to have the following properties:
	//  0 <= w(a,b) <= 1 for all possible positive floats a and b (including 0 and +inf)
	//  w(a, b) + w(b, a) == 1.0
	
	// The formulation below is much more stable across the range of all possible inputs
	// and guarantees the sum always adds up to 1.0.

	if (Pdf == OtherPdf)
	{
		// Catch potential NaNs from (0,0) and (+inf, +inf)
		return 0.5f;
	}

	// Evaluate the expression using the ratio of the smaller value to the bigger one for greater
	// numerical stability. The math would also work using the ratio of bigger to smaller value,
	// which would underflow less but would make the weights asymmetric. Underflow to 0 is not a
	// bad property to have in rendering application as it ensures more weights are exactly 0
	// which allows some evaluations to be skipped.
	if (OtherPdf < Pdf)
	{
		float x = OtherPdf / Pdf;
		return 1.0 / (1.0 + x * x);
	}
	else
	{
		// this form guarantees the weights add back up to one when arguments are swapped
		float x = Pdf / OtherPdf;
		return 1.0 - 1.0 / (1.0 + x * x);
	}
}

// Compute PDF of reflection (2 * dot( V, H ) * H - V).
// [Heitz 2014, "Importance Sampling Microfacet-Based BSDFs using the Distribution of Visible Normals"]
float RayPDFToReflectionRayPDF(float VoH, float RayPDF)
{
	float ReflectPDF = RayPDF / (4.0 * saturate(VoH));

	return ReflectPDF;
}

#endif // MONTE_CARLO_INCLUDED
