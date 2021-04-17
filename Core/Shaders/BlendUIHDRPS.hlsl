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

#include "PresentRS.hlsli"
#include "ColorSpaceUtility.hlsli"

cbuffer CBConstants	: register(b0)
{
	float _PaperWhiteRatio;	// PaperWhite / 10000.0
	float _MaxBrightness;
}

Texture2D<float4> _Overlay	: register(t0);

[RootSignature(Present_RootSig)]
float4 main(float4 position : SV_Position) : SV_TARGET
{
	float4 UI = _Overlay[(int2)position.xy];
	UI.rgb = RemoveSRGBCurve(UI.rgb);
	UI.rgb = REC709toREC2020(UI.rgb / (UI.a == 0.0 ? 1.0 : UI.a));
	UI.rgb = ApplyREC2084Curve(UI.rgb * _PaperWhiteRatio);
	return float4(UI.rgb, 1.0f) * UI.a;
}
