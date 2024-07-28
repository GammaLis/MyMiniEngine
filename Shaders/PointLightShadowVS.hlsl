#include "ModelViewerRS.hlsli"

cbuffer VSConsts	: register(b0)
{
	float4x4 _ModelToProjection[6];
};

struct Attributes
{
	float3 positon	: POSITION;
	float2 uv		: TEXCOORD0;
	
	uint InstanceID : SV_InstanceID;
};

struct Varyings
{
	float4 position : SV_Position;
	uint vpIndex	: SV_ViewportArrayIndex;
};


// TODO: Use camera mask to select effective camera -2024-07-28
Varyings main( Attributes v )
{
	Varyings o = (Varyings) 0;
	
	o.vpIndex = v.InstanceID;
	// o.position = mul( float4(v.position, 1.0f), _ModelToProjection[ o.vpIndex ] );
	// ->
	o.position = mul( _ModelToProjection[ o.vpIndex ], float4(v.positon, 1.0f) );
	
	return o;
}
