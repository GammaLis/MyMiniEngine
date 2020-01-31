#include "TextRS.hlsli"

cbuffer cbFontParams	: register(b0) 
{
	float2 _Scale;		// scale and offset for transforming coordinates
	float2 _Offset;
	float2 _InvTexDim;	// normalizes texture coordinates
	float _TextSize;	// height of text in destination pixels
	float _TextScale;	// _TextSize / FontHeight
	float _DstBorder;	// extra space around a glyph measured in screen space coordinates
	uint  _SrcBorder;	// extra spacing around glyphs to avoid sampling neighboring glyphs
}

struct VSInput
{
	float2 screenPos: POSITION;		// upper-left position in screen pixel coordinates
	uint4 glyph 	: TEXCOORD0;	// X, Y, Width, Height in texel space
};

struct VSOutput
{
	float4 pos 	: SV_POSITION;		// upper-left and lower-right coordinates in clip space
	float2 uv 	: TEXCOORD0;		// upper-left and lower-right normalized UVs
};

[RootSignature(Text_RootSig)]
VSOutput main(VSInput v, uint vid : SV_VertexID)
{
	const float2 xy0 = v.screenPos - _DstBorder;
	const float2 xy1 = v.screenPos + _DstBorder + float2(_TextScale * v.glyph.z, _TextSize);
	const uint2 uv0 = v.glyph.xy - _SrcBorder;
	const uint2 uv1 = v.glyph.xy + _SrcBorder + v.glyph.zw;

	float2 uv = float2(vid & 1, (vid >> 1) & 1);
	// 0 - (0, 0)
	// 1 - (1, 0)
	// 2 - (0, 1)
	// 3 - (1, 1)

	VSOutput o;
	o.pos = float4(lerp(xy0, xy1, uv) * _Scale + _Offset, 0, 1);
	o.uv = lerp(uv0, uv1, uv) * _InvTexDim;

	return o;
}