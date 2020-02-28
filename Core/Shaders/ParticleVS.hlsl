#include "ParticleUpdateCommon.hlsli"
#include "ParticleUtility.hlsli"

StructuredBuffer<ParticleVertex> _VertexBuffer 	: register(t0);
StructuredBuffer<uint> _IndexBuffer	: register(t1);

[RootSignature(Particle_RootSig)]
ParticleVSOutput main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
#ifdef DISABLE_PARTICLE_SORT
	ParticleVertex v = _VertexBuffer[iid];
#else
	ParticleVertex v = _VertexBuffer[_IndexBuffer[iid] & 0x3FFFF];
#endif

	ParticleVSOutput o;
	// (0, 0), (0, 1), (1, 0), (1, 1)
	// 0 - 2
	// 1 - 3
	o.uv = float2((vid >> 1) & 1, vid & 1);
	o.color = v.color;
	o.texId = v.textureId;

	float2 corner = lerp(float2(-1, 1), float2(1, -1), o.uv);
	// p = v.position + (ViewMat.x, ViewMat.y, ViewMat.z)^T * (x_v, y_v, 0)
	float3 position = mul(float3(corner * v.size, 0), (float3x3)_InvViewMat) + v.position;

	o.pos = mul(float4(position, 1.0), _ViewProjMat);
	o.linearZ = o.pos.w * _RcpFarZ;

	return o;
}
