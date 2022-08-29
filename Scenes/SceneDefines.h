#pragma once

#include "Shaders/HlslDefines.h"

struct MeshInstanceData
{
	uint globalMatrixID;
	uint materialID;
	uint meshID;
	uint flags;		// MeshInstanceFlags
};

struct StaticVertexData
{
	float3 position;
	float3 normal;
	float3 tangent;		// assimp tangent - float3
	float3 bitangent;
	float2 uv;
};

struct DynamicVertexData
{
	uint4 boneIDs;
	float4 boneWeights;
	uint staticIndex;	// the index in the static vertex buffer
	uint globalMatrixID;
};
