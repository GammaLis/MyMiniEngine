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

#include "ToneMappingUtility.hlsli"
#include "PresentRS.hlsli"

Texture2D<float3> _MainBuffer	: register(t0);
Texture2D<float4> _OverlayBuffer: register(t1);

cbuffer CBConstants	: register(b0)
{
	float _PaperWhiteRatio;	// PaperWhite / 10000.0
	float _MaxBrightness;
}

[RootSignature(Present_RootSig)]
float3 main(float4 position : SV_Position, float2 uv : Texcoord0) : SV_TARGET
{
	int2 ST = (int2)position.xy;
	
	float3 mainColor = ApplyREC2084Curve(_MainBuffer[ST] / 10000.0);

	float4 overlayColor = _OverlayBuffer[ST];

	overlayColor.rgb = RemoveSRGBCurve(overlayColor.rgb);
	overlayColor.rgb = REC709toREC2020(overlayColor.rgb / (overlayColor.a == 0.0 ? 1.0 : overlayColor.a));
	overlayColor.rgb = ApplyREC2084Curve(overlayColor.rgb * _PaperWhiteRatio);

	return lerp(mainColor, overlayColor.rgb, overlayColor.a);
}
