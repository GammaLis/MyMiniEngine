#define SHADER_VS

#include "VisibilityBufferCommon.hlsli"

StructuredBuffer<FVertex> _VertexBuffer 		: register(t4, space1);

#define USE_LOCAL_ATTRIBUTES 0

Varyings main(Attributes v, uint VertexID : SV_VertexID, uint InstanceID : SV_InstanceID)
{
	const uint drawId = v.drawId;

	MeshInstanceData meshInstance = _MeshInstanceBuffer[drawId];
	uint meshId = meshInstance.meshID;
	MeshDesc meshDesc = _MeshBuffer[meshId];

	const uint vertexIndex = VertexID + meshDesc.vertexOffset;
	FVertex vertex = _VertexBuffer[vertexIndex];

	const uint indexOffset = meshDesc.indexByteOffset / meshDesc.indexStrideSize;

	uint globalMatrixID = meshInstance.globalMatrixID;
	GlobalMatrix globalMatrix = _MatrixBuffer[globalMatrixID];
	matrix _WorldMat = globalMatrix.worldMat;
	matrix _InvWorldMat = globalMatrix.invWorldMat;

	float4 posWS = mul(float4(vertex.position, 1.0), _WorldMat);
	float3 normal = mul(vertex.normal, (float3x3)_WorldMat);
#if USE_LOCAL_ATTRIBUTES
	posWS = float4(vertex.position, 1.0);
	normal = vertex.normal;
#endif
	float4 posHS = mul(posWS, _View.viewProjMat);
	float2 uv0 = vertex.uv0;

	Varyings o = (Varyings) 0;
	o.position = posHS;
	o.uv0 = uv0;
	o.normal = normal;
	o.drawId = drawId;

	return o;
}
