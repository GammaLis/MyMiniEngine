#include "Common/DynDescRS.hlsli"
#include "MeshDefines.hlsli"

struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv0 : TEXCOORD0;
	float3 worldPos : TEXCOORD1;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float3 bitangent : BITANGENT;
};

cbuffer CBConstants : register(b0, space1)
{
    float4 _Constants;
};

[RootSignature(DynResource_RootSig)]
float4 main(VSOutput i) : SV_Target
{
	const uint DrawId = uint(_Constants.x);
	const float AlphaCutout = _Constants.y;
	
	StructuredBuffer<MeshInstanceData> meshInstances = ResourceDescriptorHeap[DESCRIPTOR_INDEX_MESH_INSTANCES];
	MeshInstanceData meshInstance = meshInstances[DrawId];
	
	uint materialIndex = meshInstance.materialID;
	uint baseColorTexIndex = DESCRIPTOR_INDEX_MATERIAL_TEXTURES + materialIndex * NUM_TEXTURES_PER_MATERIAL + MATERIAL_TEXTURE_BASECOLOR;
	Texture2D<float4> baseColorTexture = ResourceDescriptorHeap[baseColorTexIndex];

	float4 color = baseColorTexture.Sample(sampler_LinearWrap, i.uv0);
#ifdef _CLIP_ON
	if (color.a < AlphaCutout)
		discard;
#endif
	return color;
}
