#ifndef MODELVIEWER_RAYTRACING_INPUTS_INCLUDED
#define MODELVIEWER_RAYTRACING_INPUTS_INCLUDED

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
// Author(s):    James Stanard, Christopher Wallis
//

#define HLSL
#include "../../Game/ModelViewerRaytracing.h"
#include "../../Core/RayTracing/RayTracingHlslCompat.h"
#include "../../Core/Shaders/PixelPacking_Velocity.hlsli"

cbuffer CBMaterial : register(b3, space1)
{
	uint _MaterialID;
}

StructuredBuffer<RayTraceMeshInfo> _MeshInfo : register(t1);
ByteAddressBuffer _Indices		: register(t2);
ByteAddressBuffer _Attributes	: register(t3);

StructuredBuffer<LightData> _LightBuffer : register(t4);

Texture2D<float> _TexShadow		: register(t6);
Texture2D<float> _TexSSAO		: register(t7);

Texture2D<float4> _LocalTexture : register(t6, space1);
Texture2D<float4> _LocalNormal	: register(t7, space1);

Texture2D<float>  _TexDepth		: register(t12);
Texture2D<float4> _TexNormal	: register(t13);
Texture2D<packed_velocity_t> _TexVelocity	: register(t14);

SamplerState _S0 : register(s0); // Default sampler
SamplerState SamplerLinearClamp : register(s1);
SamplerState SamplerPointClamp	: register(s2);
SamplerComparisonState SamplerShadow : register(s3);

#endif // MODELVIEWER_RAYTRACING_INPUTS_INCLUDED
