// Ref: HLSL Dynamic Resources
// https://microsoft.github.io/DirectX-Specs/d3d/HLSL_SM_6_6_DynamicResources.html

/**
 * HLSL Dynamic Resources
 * Shader Model 6.6 introduces the ability to create resources from descriptors by directly indexing into
 * the CBV_SRV_UAV heap or the Sampler heap. No root signature descriptor table mapping is required
 * for this resource creation method, but new global root signature flags are used to indicate the use of
 * each heap from the shader.
 * 'resource variable = ResourceDescriptorHeap[uint index];'
 * 'sampler variable  = SamplerDescriptorHeap[uint index];'
 * Texture2D<float4> texture = ResourceDescriptorHeap[texIdx];
 * By default, indexing into the resource or sampler heap is considered uniform. If the index is not uniform,
 * you must use the 'NonUniformResourceIndex' intrinsics on the index.
 *
 * Two new root signature flags are introduced, which are only allowed on a device supporting this feature.
 * These new flags are not allowed on local root signature.
 * 'D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED'
 * 'D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED'
 * Root signature flags as used in a root signature defined in HLSL:
 * 'RoofFlags( CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED )'
 *
 * There is a new ordering constraint between 'SetDescriptorHeaps' and 'SetGraphicsRootSignature' or
 * 'SetComputeRootSignature'. SetDescriptorHeaps must be called, passing the corresponding heaps, before
 * a call to 'SetGraphicsRootSignature' that use either 'CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED' or
 * 'SAMPLER_HEAP_DIRECTLY_INDEXED' flags. This is in order to make sure the correct heap pointers are
 * available when the root signature is set.
 */

#define USE_DYNAMIC_RESOURCE 1

#if USE_DYNAMIC_RESOURCE
#include "Common/DynDescRS.hlsli"
#include "MeshDefines.hlsli"
#endif

cbuffer CBConstants : register(b0, space1)
{
	float4 _Constants;
};

#if USE_DYNAMIC_RESOURCE
struct CBPerObject
{
	float4x4 worldMat;
	float4x4 invWorldMat;
};
struct CBPerCamera
{
	float4x4 viewProjMatrix;
	float3 camPos;
};

#else

cbuffer CBPerObject : register(b1)
{
	matrix _WorldMat;
	matrix _InvWorldMat;
};
cbuffer CBPerCamera : register(b2)
{
	matrix _ViewProjMat;
	float3 _CamPos;
};
#endif

struct VSInput
{
	float3 position : POSITION;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float3 bitangent : BITANGENT;
	float2 uv0 : TEXCOORD0;
};

struct VSOutput
{
	float4 pos : SV_POSITION;
	float2 uv0 : TEXCOORD0;
	float3 worldPos : TEXCOORD1;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float3 bitangent : BITANGENT;
};

#if USE_DYNAMIC_RESOURCE
[RootSignature(DynResource_RootSig)]
#endif
VSOutput main(VSInput v)
{
	VSOutput o;

#if USE_DYNAMIC_RESOURCE
	const uint DrawId = uint(_Constants.x);
	
	StructuredBuffer<MeshInstanceData> meshInstances = ResourceDescriptorHeap[DESCRIPTOR_INDEX_MESH_INSTANCES];
	StructuredBuffer<GlobalMatrix> globalMatrices = ResourceDescriptorHeap[DESCRIPTOR_INDEX_MATRICES];
	ConstantBuffer<FViewUniformBufferParameters> viewParameters = ResourceDescriptorHeap[DESCRIPTOR_INDEX_VIEW];
	
	MeshInstanceData meshInstance = meshInstances[DrawId];

	// FIXME:
	GlobalMatrix perObject = globalMatrices[meshInstance.globalMatrixID];

	float4 wPos = mul(float4(v.position, 1.0), perObject.worldMat);
	// wPos = float4(v.position, 1.0);
	float4 cPos = mul(wPos, viewParameters.viewProjMat);

	float3 wNormal = normalize(mul((float3x3) perObject.invWorldMat, v.normal));
	float3 wTangent = normalize(mul(v.tangent.xyz, (float3x3) perObject.worldMat));
	float3 wBitangent = normalize(mul(v.bitangent.xyz, (float3x3) perObject.worldMat));
#else
	float4 wPos = mul(float4(v.position, 1.0), _WorldMat);
	// wPos = float4(v.position, 1.0);
	float4 cPos = mul(wPos, _ViewProjMat);

	float3 wNormal = normalize(mul((float3x3) _InvWorldMat, v.normal));
	float3 wTangent = normalize(mul(v.tangent.xyz, (float3x3) _WorldMat));
	float3 wBitangent = normalize(mul(v.bitangent.xyz, (float3x3) _WorldMat));
#endif

	o.pos = cPos;
	o.worldPos = wPos.xyz;
	o.normal = wNormal;
	o.tangent = wTangent;
	o.bitangent = wBitangent;
	o.uv0 = v.uv0;

	return o;
}
