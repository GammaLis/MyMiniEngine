#define RootSig_Cubemap \
	"RootFlags(0)," \
	"RootConstants(b0, num32BitConstants = 2)," \
	"DescriptorTable(SRV(t0, numDescriptors = 1))," \
	"DescriptorTable(UAV(u0, numDescriptors = 1))," \
	"StaticSampler(s0," \
		"AddressU = TEXTURE_ADDRESS_WRAP," \
		"AddressV = TEXTURE_ADDRESS_WRAP," \
		"AddressW = TEXTURE_ADDRESS_WRAP," \
		"Filter = FILTER_MIN_MAG_MIP_LINEAR)"

#include "PreComputing.hlsli"

cbuffer CSConstants	 : register(b0)
{
	uint2 _Dimensions;	// x - width, y - height
};

TextureCube<float4> _EnvironmentMap	: register(t0);
// Texture2D<float4> _EnvironmentMap	: register(t0);
RWTexture2D<float4> Output			: register(u0);

SamplerState s_LinearSampler		: register(s0);

// pdf = cosTheta / Pi (保证pdf半球积分==1)
// c/pi / N * Sigma(Li* cosTheta / (pdf)) = c/N * Sigma(Li)
float3 DiffuseIrradiance(float3 N, float3 T, float3 B)
{
	static const uint NumSamples = 256;

	float3 irradiance = 0.0;
	for (uint i = 0; i < NumSamples; ++i)
	{
		float2 u = Hammersley(i, NumSamples);
		float3 tPos = HemisphereCosSample(u);
		float3 wPos = tPos.x * T + tPos.y * B + tPos.z * N;

		irradiance += _EnvironmentMap.SampleLevel(s_LinearSampler, wPos, 0).rgb;	// 注意：这里没有 *sinTheta
	}
	irradiance *= 1.0 / NumSamples;
	return irradiance;
}

// https://learnopengl.com/PBR/IBL/Diffuse-irradiance
// Inv(Li * sin(theta) * cos(theta)) dtheta * dphi
float3 DiffuseIrradiance_RiemannSum(float3 N, float3 T, float3 B)
{
	float sampleDelta = 0.025;
	uint sampleCount = 0;
	float3 irradiance = 0.0;
	for (float theta = 0.0; theta < 0.5 * Pi; theta += sampleDelta)
	{
		for (float phi = 0.0; phi < 2.0 * Pi; phi += sampleDelta)
		{
			float3 tPos = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
			float3 wPos = T * tPos.x + B * tPos.y + N * tPos.z;

			irradiance += _EnvironmentMap.SampleLevel(s_LinearSampler, wPos, 0).rgb * cos(theta) * sin(theta);
			++sampleCount;
		}
	}
	irradiance *= Pi * (1.0 / sampleCount);
	return irradiance;
}

// pre-filtered importance sampling
// Siggraph 2013 - <<Real Shading in Unreal Engine 4>>
// split sum approximation
// 1/N * Sigma(Li * Fr * cosTheta / pdf) ~= [1/N * Sigma(Li)] * [1/N * Sigma(Fr * cosTheta / pdf)]
// 1/N * Sigma(Li) - prefiltered map
// 
// UE4 - weighting by cosTheta achieves better results
float3 PrefilterEnvMap(float3 N, float3 T, float3 B, float roughness)
{
	static const uint NumSamples = 256;

	float3 V = N;

	float3 prefilteredColor = 0.0;
	float weights = 0.0;
	for (uint i = 0; i < NumSamples; ++i)
	{
		float2 u = Hammersley(i, NumSamples);
		float3 tH = HemisphereImportanceSampleGGX(u, roughness);
		float3 H = tH.x * T + tH.y * B + tH.z * N;
		float3 L = 2 * dot(V, H) * H - V;
		
		float NdotL = saturate(dot(N, L));
		if (NdotL > 0)
		{
			prefilteredColor += _EnvironmentMap.SampleLevel(s_LinearSampler, L, 0).rgb * NdotL;
			weights += NdotL;
		}
	}
	prefilteredColor /= weights;
	return prefilteredColor;
}

/**
 * Filament CubemapIBL.cpp
 * Er() = Inv(Fr * <n, l>) --Importance sampling--> 1/N * Sigma(Fr * <n, l> / pdf)
 * 	= 1/N * Sigma(D*F*V * 4<v.h>/(D*<n,h>) * <n, l>)
 * 	= 4/N * Sigma(F * V * <v,h>/<n,h> * <n, l>)
 */
float2 IntegrateBRDF(float roughness, float NdotV)
{
	static const uint NumSamples = 256;

	float3 V = float3(sqrt(1 - NdotV * NdotV), 0.0, NdotV);

	float A = 0.0, B = 0.0;
	float weights = 0.0;
	for (uint i = 0; i < NumSamples; ++i)
	{
		float2 u = Hammersley(i, NumSamples);
		float3 H = HemisphereImportanceSampleGGX(u, roughness);
		float3 L = 2 * dot(V, H) * H - V;
		float VdotH = saturate(dot(V, H));
		float NdotL = saturate(L.z);
		float NdotH = saturate(H.z);
		if (NdotL > 0)
		{
			float v = V_SmithGGXCorrelated(NdotV, NdotL, roughness) * NdotL * (VdotH / NdotH);
			float Fc = Pow5(1 - VdotH);
			A += v * (1.0 - Fc);
			B += v * Fc;
		}
	}
	
	return float2(A, B) * 4.0 / NumSamples;
}

//
// Filament CubemapIBL.cpp
// 1/N * Sigma(D * V * <n, l> / pdf)
// 1/N * Sigma(D * V * 4<v.h>/(D*<n,h>) * <n, l>)
// 4/N * Sigma(V * <v,h>/<n,h> * <n, l>)
// USEAGE:
// float3 energeCompensation = 1.0 + F0 * (1.0 / dfg.y - 1.0);
// scale the specular lobe to account for multiscattering
// Fr *= energeCompensation;
float2 IntegrateBRDF_Multiscatter(float roughness, float NdotV)
{
	static const uint NumSamples = 256;

	float3 V = float3(sqrt(1 - NdotV * NdotV), 0.0, NdotV);

	float A = 0.0, B = 0.0;
	float weights = 0.0;
	for (uint i = 0; i < NumSamples; ++i)
	{
		float2 u = Hammersley(i, NumSamples);
		float3 H = HemisphereImportanceSampleGGX(u, roughness);
		float3 L = 2 * dot(V, H) * H - V;
		float VdotH = saturate(dot(V, H));
		float NdotL = saturate(L.z);
		float NdotH = saturate(H.z);
		if (NdotL > 0)
		{
			float v = V_SmithGGXCorrelated(NdotV, NdotL, roughness) * NdotL * (VdotH / NdotH);
			float Fc = Pow5(1 - VdotH);
			/**
			 * assuming F90 = 1
			 * 	Fc = (1 - VdotH)^5
			 * 	F(h) = F0 * (1 - Fc) + Fc
			 * 	F0 and F90 are known at runtime, but can be factored out, allowing us to split the integral
			 * 	in 2 terms and store both terms separate in a LUT
			 */
			A += v * Fc;
			B += v;
		}
	}
	return float2(A, B) * 4.0 / NumSamples;	// y - result, why float2 ? -2020-3-27
}

[numthreads(16, 16, 1)]
void main( uint3 dtid : SV_DispatchThreadID )
{
	if (dtid.x >= _Dimensions.x || dtid.y >= _Dimensions.y)
		return;

	float2 uv = float2(dtid.xy + 0.5) / _Dimensions.xy;
	uv = 2 * uv - 1;
	uv.y *= -1;		// [-1, 1]

	float theta0 = uv.y * PiOver2;	// [-pi/2, pi/2]
	float phi0 = uv.x * Pi;	// [-pi, pi]

	float x = cos(theta0) * sin(phi0);
	float y = sin(theta0);
	float z = cos(theta0) * cos(phi0);

	float3 N = float3(x, y, z);
	float3 T, B;
	CoordinateSystem(N, T, B);

	// // irradiance map
	// float4 irradiance = 1;
	// // I.
	// // irradiance.rgb = DiffuseIrradiance_RiemannSum(N, T, B);
	// // II.
	// irradiance.rgb = DiffuseIrradiance(N, T, B);

	// // prefilter map
	// float4 prefilteredColor = 1;
	// float perceptualRoughness = 0.2;
	// float roughness = perceptualRoughness * perceptualRoughness;
	// prefilteredColor.rgb = PrefilterEnvMap(N, T, B, roughness);
	
	// integrate BRDF
	float NdotV = (dtid.x + 0.5) / _Dimensions.x;
	float roughness = 1.0 - (dtid.y + 0.5) / _Dimensions.y;
	float2 envBRDF = IntegrateBRDF(roughness, NdotV);

	// integrate BRDF_Multiscatter
	// float2 envBRDF_multiscatter = IntegrateBRDF_Multiscatter(roughness, NdotV);

	Output[dtid.xy] = float4(envBRDF, 0.0, 1.0);
	// END

	// ------------------------------------------
	// test: Texture2D - uv 采样
	// Output[dtid.xy] = _EnvironmentMap.SampleLevel(s_LinearSampler, float2(dtid.xy + 0.5) / _Dimensions.xy, 0);
	
	// test: Octahedron map
	// uv = float2(dtid.xy + 0.5) / _Dimensions.xy;
	// uv.y = 1.0 - uv.y;
	// N = OctahedronUnmapping(uv);
	// 
	// test: TextureCube - 采样
	// Output[dtid.xy] = _EnvironmentMap.SampleLevel(s_LinearSampler, N, 0);

	// test
	Output[dtid.xy] = float4(dtid.x & dtid.y, (dtid.x & 15) / 15.0, (dtid.y & 15) / 15.0, 1.0);

}

// NOTE: compute shader - use `SampleLevel`, not `Sample` !
