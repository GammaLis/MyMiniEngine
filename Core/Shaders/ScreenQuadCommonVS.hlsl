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
// A vertex shader for full-screen effects without a vertex buffer.  The
// intent is to output an over-sized triangle that encompasses the entire
// screen.  By doing so, we avoid rasterization inefficiency that could
// result from drawing two triangles with a shared edge.
//
// Use null input layout
// Draw(3)

#include "CommonRS.hlsli"

[RootSignature(Common_RootSig)]
void main( 
	in  uint vid	: SV_VertexID,
	out float4 pos	: SV_POSITION,
	out float2 uv	: Texcoord0
	)
{
	// texture coordinates range [0, 2], but only [0, 1] appears on screen
	uv = float2(uint2(vid, vid << 1) & 2);
	pos = float4(lerp(float2(-1, 1), float2(1, -1), uv), 0, 1);
}
