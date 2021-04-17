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

Texture2D<float3> _MainBuffer	: register(t0);
Texture2D<float4> _OverlayBuffer: register(t1);

[RootSignature(Present_RootSig)]
float3 main(float4 position : SV_Position) : SV_TARGET
{
	float3 mainColor = ApplyDisplayProfile(_MainBuffer[(int2)position.xy], DISPLAY_PLANE_FORMAT);
	float4 overlayColor = _OverlayBuffer[(int2)position.xy];
	return overlayColor.rgb + mainColor.rgb * (1.0f - overlayColor.a);
}
