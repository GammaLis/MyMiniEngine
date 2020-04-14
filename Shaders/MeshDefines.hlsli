#ifndef MESH_DEFINES_HLSLI
#define MESH_DEFINES_HLSLI

struct MeshDesc
{
	uint vertexOffset;
	uint vertexByteSize;
	uint vertexStrideSize;
	uint vertexCount;

	uint indexByteOffset;
	uint indexByteSize;
	uint indexStrideSize;	// uint16_t
	uint indexCount;

	uint materialID;
};

struct MeshInstanceData
{
	uint globalMatrixID;
	uint materialID;
	uint meshID;
	uint flags;		// MeshInstanceFlags
};

struct GlobalMatrix
{
	matrix worldMat;
	matrix invWorldMat;
};

#endif	// MESH_DEFINES_HLSLI
