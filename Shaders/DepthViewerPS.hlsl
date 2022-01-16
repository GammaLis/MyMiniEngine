#include "ModelViewerRS.hlsli"

cbuffer PSConstants	: register(b0)
{
	float _Cutout;
};

Texture2D<float4> _TexDiffuse	: register(t0);

struct VSOutput
{
	float4 pos	: SV_POSITION;
	float2 uv 	: TEXCOORD0;
};

[RootSignature(ModelViewer_RootSig)]
void main(VSOutput i)
{
	if (_TexDiffuse.Sample(s_DefaultSampler, i.uv).a < _Cutout)
		discard;
}