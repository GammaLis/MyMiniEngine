// keep in  sync with C code
#define MAX_LIGHTS 128
#define TILE_SIZE (4 + MAX_LIGHTS * 4)

struct LightData
{
	float3 pos;
	float radiusSq;

	float3 color;
	uint type;

	float3 coneDir;
	float2 coneAngles;	// x = 1.0f / (cos(coneInner) - cos(coneOuter)), y = cos(coneOuter)
};

uint2 GetTilePos(float2 pos, float2 invTileDim)
{
	return pos * invTileDim;
}

uint GetTileIndex(uint2 tilePos, uint tileCountX)
{
	return tilePos.x + tilePos.y * tileCountX;
}

uint GetTileOffset(uint tileIndex)
{
	return tileIndex * TILE_SIZE;
}
