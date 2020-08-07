#include "VoronoiRS.hlsli"
#include "../Utility.hlsli"

cbuffer CSConstants	: register(b0)
{
	uint _Size;
	float4 _Time;
};

// https://docs.microsoft.com/zh-cn/windows/win32/direct3dhlsl/sm5-object-rwtexture2d
// a RWTexture2D object requires an element type in a declaration statement
// for the object.
// e.g. RWTexture2D Texture;	- ERROR!
RWTexture2D<float4> Output	: register(u0);

[RootSignature(RootSig_VoronoiTexture)]
[numthreads(8, 8, 1)]
void main( uint3 dtid : SV_DispatchThreadID, uint gtIndex : SV_GroupIndex )
{
	uint randState = gtIndex + _Time.y;
	uint x = wang_hash(randState);
	uint y = wang_hash(randState);
	uint2 coord = uint2(x, y);
	coord = coord % _Size;

	Output[coord] = PackCoord(coord);
}
