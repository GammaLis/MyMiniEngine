#include "ParticleRS.hlsli"

#define MAX_PARTICLES_PER_BIN 2014
#define BIN_SIZE_X 128
#define BIN_SIZE_Y 64
#define TILE_SIZE 16

#define TILES_PER_BIN_X (BIN_SIZE_X / TILE_SIZE)
#define TILES_PER_BIN_Y (BIN_SIZE_Y / TILE_SIZE)
#define TILES_PER_BIN (TILES_PER_BIN_X * TILES_PER_BIN_Y)

#define MaxTextureSize 64

SamplerState s_LinearBorderSampler	: register(s0);
SamplerState s_PointBorderSampler	: register(s1);
SamplerState s_LinearClampSampler	: register(s2);

cbuffer CBChangesPerDraw	: register(b1)
{
	float4x4 _InvViewMat;
	float4x4 _ViewProjMat;

	float _VertCotangent;
	float _AspectRatio;
	float _RcpFarZ;
	float _InvertZ;

	float2 _BufferDim;
	float2 _RcpBufferDim;

	uint _BinsPerRow;
	uint _TileRowPitch;
	uint _TilesPerRow;
	uint _TilesPerCol;
};

struct ParticleVertex
{
	float3 position;
	float4 color;
	float size;
	uint textureId;
};

// intentionally left unpacked to allow scalar register loads and ops
struct ParticleScreenData
{
	float2 corner;	// top-left location
	float2 rcpSize;	// 1/width, 1/height
	float4 color;
	float depth;
	float textureIndex;
	float textureLevel;
	uint bounds;
};

// bitIndex 
uint InsertZeroBit(uint value, uint bitIndex)
{
	uint bitVal = 1 << bitIndex;
	uint mask = bitVal - 1;
	return ((value & ~mask) << 1) | (value & mask);
}
