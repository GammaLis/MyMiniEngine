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
// Author:  James Stanard 
//

#include "ShaderUtility.hlsli"
#include "PresentRS.hlsli"

cbuffer CBConstants	: register(b0)
{
	float _PaperWhiteRatio;	// PaperWhite / 10000.0
	float _MaxBrightness;
	float2 _UVOffset;
}

Texture2D<float3> _MainBuffer	: register(t0);
Texture2D<float4> _OverlayBuffer: register(t1);

SamplerState s_BilinearSampler	: register(s0);

float3 SampleColor(float2 uv)
{
	return _MainBuffer.SampleLevel(s_BilinearSampler, uv, 0);
}

/**
* Sample Mode
*	x	x
*	  c
*	x	x
*/
float3 ScaleBuffer(float2 uv)
{
	return 1.4 * SampleColor(uv) - 0.1f * (
		SampleColor(uv + float2(+_UVOffset.x, +_UVOffset.y)) +
		SampleColor(uv + float2(+_UVOffset.x, -_UVOffset.y)) +
		SampleColor(uv + float2(-_UVOffset.x, +_UVOffset.y)) +
		SampleColor(uv + float2(-_UVOffset.x, -_UVOffset.y))
		);
}

[RootSignature(Present_RootSig)]
float3 main(float4 position : SV_Position, float2 uv : Texcoord0) : SV_TARGET
{
	float3 mainColor = ApplyREC2084Curve(ScaleBuffer(uv) / 10000.0);

	float4 overlayColor = _OverlayBuffer[(int2)position.xy];
	overlayColor.rgb = RemoveSRGBCurve(overlayColor.rgb);
	overlayColor.rgb = REC709toREC2020(overlayColor.rgb / (overlayColor.a == 0.0 ? 1.0 : overlayColor.a));
	overlayColor.rgb = ApplyREC2084Curve(overlayColor.rgb * _PaperWhiteRatio);
	
	return lerp(mainColor, overlayColor.rgb, overlayColor.a);
}
