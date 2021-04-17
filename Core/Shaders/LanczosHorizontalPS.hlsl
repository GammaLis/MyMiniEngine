//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author(s):  James Stanard
//             Jack Elliott
//

#include "LanczosFunctions.hlsli"
#include "PresentRS.hlsli"
#include "ShaderUtility.hlsli"

cbuffer CBConstants	: register(b0)
{
	float2 _SrcResolution;
};

Texture2D<float3> _SrcTex	: register(t0);

float4x3 LoadSamples(int2 ST, uint2 stride)
{
	int2 st0 = ST, st1 = ST + stride, st2 = ST + 2 * stride, st3 = ST + 3 * stride;
	return float4x3(_SrcTex[st0], _SrcTex[st1], _SrcTex[st2], _SrcTex[st3]);
}

[RootSignature(Present_RootSig)]
float3 main(float4 pos : SV_Position, float2 uv : Texcoord0) : SV_TARGET
{
	/**
	 * we subtract 0.5 because that represents the center of the pixel. We need to know where 
	 * we lie between 2 pixel centers, and we will use frac() for that. We subtract another 1.0
	 * so that our start index is 1 pixel to the left.
	 */
	float2 topLeft = uv * _SrcResolution - 1.5;

#ifdef LANCZOS_VERTICAL
	float4 weights = GetUpscaleFilterWeights(frac(topLeft.y));
	float4x3 samples = LoadSamples(int2(floor(pos.x), topLeft.y), uint2(0, 1));
#else 
	float4 weights = GetUpscaleFilterWeights(frac(topLeft.x));
	float4x3 samples = LoadSamples(int2(topLeft.x, floor(pos.y)), uint2(1, 0));
#endif
	
	float3 result = mul(weights, samples);

#ifdef LANCZOS_VERTICAL
	// transform to display settings
	result = RemoveDisplayProfile(result, LDR_COLOR_FORMAT);
	result = ApplyDisplayProfile(result, DISPLAY_PLANE_FORMAT);
#endif

	return result;
}
