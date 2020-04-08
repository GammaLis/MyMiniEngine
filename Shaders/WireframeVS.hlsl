
cbuffer CBConstants	: register(b0)
{
	float4 _Constants;
};
cbuffer CBPerObject	: register(b1)
{
	matrix _WorldMat;
	matrix _InvWorldMat;
};
cbuffer CBPerCamera	: register(b2)
{
	matrix _ViewProjMat;
	float3 _CamPos;
};

struct VSInput
{
	float3 position : POSITION;
};

struct VSOutput
{
	float4 pos 	: SV_POSITION;
};

VSOutput main(VSInput v)
{
	VSOutput o;

	float4 wPos = mul(float4(v.position, 1.0), _WorldMat);
	// wPos = float4(v.position, 1.0);
	float4 cPos = mul(wPos, _ViewProjMat);

	o.pos = cPos;

	return o;
}
