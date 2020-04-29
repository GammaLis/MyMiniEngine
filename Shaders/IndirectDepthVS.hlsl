#include "CommonIndirectRS.hlsli"

cbuffer CBConstants	: register(b0)
{
	float4 _Constants;
};
cbuffer CBPerCamera	: register(b1)
{
	matrix _ViewProjMat;
	float3 _CamPos;
};

StructuredBuffer<GlobalMatrix> _MatrixBuffer            : register(t0, space1);
StructuredBuffer<MaterialData> _MaterialBuffer          : register(t1, space1);
StructuredBuffer<MeshDesc> _MeshBuffer                  : register(t2, space1);
StructuredBuffer<MeshInstanceData> _MeshInstanceBuffer  : register(t3, space1);

struct VSInput
{
	// per vertex data
	float3 position : POSITION;
	float3 normal 	: NORMAL;
	float3 tangent	: TANGENT;
	float3 bitangent: BITANGENT;
	float2 uv0		: TEXCOORD0;
	// per instance data
	uint drawId		: DRAWID;
};

struct VSOutput
{
	float4 pos 	: SV_POSITION;
	float2 uv0 	: TEXCOORD0;

	uint drawId	: DRAWID;
};

[RootSignature(CommonIndirect_RootSig)]
VSOutput main(VSInput v)
{
	VSOutput o;

	MeshInstanceData meshInstance = _MeshInstanceBuffer[v.drawId];
	uint globalMatrixID = meshInstance.globalMatrixID;
	GlobalMatrix globalMatrix = _MatrixBuffer[globalMatrixID];
	matrix _WorldMat = globalMatrix.worldMat;
	matrix _InvWorldMat = globalMatrix.invWorldMat;

	float4 wPos = mul(float4(v.position, 1.0), globalMatrix.worldMat);
	// wPos = float4(v.position, 1.0);
	float4 cPos = mul(wPos, _ViewProjMat);

	o.pos = cPos;
	o.uv0 = v.uv0;
	o.drawId = v.drawId;

	return o;
}
