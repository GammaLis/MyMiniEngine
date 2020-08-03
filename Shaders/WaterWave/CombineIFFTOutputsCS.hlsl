#include "WaterWaveRS.hlsli"

cbuffer CSConstants	: register(b0)
{
	uint _N;
	float _L;
	float _HeightScale;	// 高度伸缩
	float _Lambda;	// 水平位移参数
};

Texture2D<float2> _HeightMap: register(t0);
Texture2D<float2> _DxMap	: register(t1);
Texture2D<float2> _DyMap	: register(t2);	// 水平面 方向，也有用z表示，这里y并非指 y轴

RWTexture2D<float3> DisplacementMap	: register(u0);
RWTexture2D<float4> NormalAndFoldMap	: register(u1);

int2 WrapCoordinate(int2 coord)
{
	// if (coord.x >= _N)
	// {
	// 	coord.x -= _N;
	// }
	// else if (coord.x < 0)
	// {
	// 	coord.x += _N;
	// }

	// if (coord.y >= _N)
	// {
	// 	coord.y -= _N;
	// }
	// else if (coord.y < 0)
	// {
	// 	coord.y += _N;
	// }
	return (coord + _N) % _N;
}

[numthreads(16, 16, 1)]
void main( uint3 dtid : SV_DispatchThreadID )
{
	int2 coord = int2(dtid.xy);
	float Dh = _HeightMap[coord].r * _HeightScale;
	float Dx = _DxMap[coord].r * _Lambda;
	float Dy = _DyMap[coord].r * _Lambda;

	// displacement
	DisplacementMap[dtid.xy] = float3(Dx, Dh, Dy);

	float xyScale = _L / _N;

	int2 lcoord = WrapCoordinate(coord + int2(-1,  0));
	int2 rcoord = WrapCoordinate(coord + int2(+1,  0));
	int2 ucoord = WrapCoordinate(coord + int2( 0, -1));
	int2 dcoord = WrapCoordinate(coord + int2( 0, +1));

	// normal
	float lh = _HeightMap[lcoord].r;
	float rh = _HeightMap[rcoord].r;
	float uh = _HeightMap[ucoord].r;
	float dh = _HeightMap[dcoord].r;
	float hx = (rh - lh) * _HeightScale / (2.0f * xyScale);
	float hy = (dh - uh) * _HeightScale / (2.0f * xyScale);
	float3 normal = normalize(float3(-hx, 1.0f, -hy));

	// fold
	float lx = _DxMap[lcoord].r, ly = _DyMap[lcoord].r;
	float rx = _DxMap[rcoord].r, ry = _DyMap[rcoord].r;
	float ux = _DxMap[ucoord].r, uy = _DyMap[ucoord].r;
	float dx = _DxMap[dcoord].r, dy = _DyMap[dcoord].r;
	float jxx = 1.0f + _Lambda * (rx - lx) / (2.0f * xyScale);
	float jyy = 1.0f + _Lambda * (dy - uy) / (2.0f * xyScale);
	float jxy = _Lambda * (dx - ux) / (2.0f * xyScale);
	float jyx = jxy;
	float j = jxx * jyy - jxy * jyx;

	NormalAndFoldMap[dtid.xy] = float4(normal, j);

}
