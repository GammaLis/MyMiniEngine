#include "Common/glTFCommon.hlsli"

#ifndef USE_DESCRIPTOR_HEAP_INDEX
#define USE_DESCRIPTOR_HEAP_INDEX 1
#endif

#if USE_DESCRIPTOR_HEAP_INDEX
#include "Common/DynDescRS.hlsli"
#endif

#include "Common/Visualization.hlsli"
#include "PBRUtility.hlsli"

#if !USE_DESCRIPTOR_HEAP_INDEX 

cbuffer CBPerMaterial	: register(b3)
{
	float4 _BaseColorFactor;
	float3 _EmissiveFactor;
	float _AlphaCutout;
	uint4 _Texcoords[2];	// 0-baseColor, 1-metallicRoughness, 2-normal, 3-occlusion, 4-emissive,...

	#if defined(SHADING_MODEL_METALLIC_ROUGHNESS)
		float _Metallic;
		float _Roughness;
		float _F0;			// default to 0.04
		float _Padding;
	#elif defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
		float3 _SpecularColor;
		float _Glossiness;
	#endif
	float _NormalScale;
	float _OcclusionStrength;
};

Texture2D<float4> _TexBaseColor			: register(t0);
	#if defined(SHADING_MODEL_METALLIC_ROUGHNESS)
		Texture2D<float4> _TexMetallicRoughness	: register(t1);
	#elif defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
		Texture2D<float4> _TexSpecularGlossiness: register(t1);
	#endif
Texture2D<float3> _TexNormal 			: register(t2);
Texture2D<float> _TexOcclusion			: register(t3);
Texture2D<float4> _TexEmissive			: register(t4);

StructuredBuffer<FLight> _Lights		: register(t1, space1);
StructuredBuffer<SH9Color> _SHCoefs		: register(t2, space1);

SamplerState s_LinearRSamper: register(s0);
SamplerState s_PointCSampler: register(s1);

#endif

static const float3 kDebugColor[] =
{
	float3(0, 0, 0),
	float3(1, 0, 0),
	float3(0, 1, 0),
	float3(0, 0, 1),
	float3(1, 1, 0),
	float3(1, 0, 1),
	float3(0, 1, 1),
	float3(1, 1, 1),
};

// Entry
float4 main(VSOutput i) : SV_TARGET
{
	float2 uvs[] = {i.uv0, i.uv1};

	TMaterial mat;

	const uint DrawId = GetDrawId();
	
	const uint SlotInstanceBuffer = GetSlotInstanceBuffer();
	const uint SlotMaterialBuffer = GetSlotMaterialBuffer();
	const uint SlotCamera = GetSlotCamera();
	const uint slotLightBuffer = GetSlotLightBuffer();
	const uint slotSHCoefficients = GetSlotSHCoefficients();
	
	StructuredBuffer<CBPerObject> cbObjects = ResourceDescriptorHeap[SlotInstanceBuffer];
	StructuredBuffer<CBPerMaterial> cbMaterials = ResourceDescriptorHeap[SlotMaterialBuffer];
	ConstantBuffer<CBPerCamera> cbPerCamera = ResourceDescriptorHeap[SlotCamera];

	StructuredBuffer<FLight> LightBuffer = ResourceDescriptorHeap[slotLightBuffer];
	StructuredBuffer<SH9Color> SHCoefs = ResourceDescriptorHeap[slotSHCoefficients];

	CBPerObject cbPerObject = cbObjects[DrawId];
	const uint MaterialId = cbPerObject.materialIndex;
	CBPerMaterial cbPerMaterial = cbMaterials[MaterialId];

	// Material data
	const float4 BaseColorFactor = cbPerMaterial.baseColorFactor;
	const float AlphaCutout = cbPerMaterial.alphaCutout;
	const float F0 = cbPerMaterial.f0;
	const float OcclusionStrength = cbPerMaterial.occlusionStrength;

	// Material textures
	const uint materialTextureStart = GetSlotMaterialTextureStart(cbPerMaterial);
	Texture2D<float4> TexBaseColor = ResourceDescriptorHeap[materialTextureStart];
	Texture2D<float4> TexMetallicRoughness = ResourceDescriptorHeap[materialTextureStart+1];
	Texture2D<float3> TexNormal = ResourceDescriptorHeap[materialTextureStart+2];
	Texture2D<float4> TexEmissive = ResourceDescriptorHeap[materialTextureStart+3];
	Texture2D<float > TexOcclusion = ResourceDescriptorHeap[materialTextureStart+4];

	// base color
	float4 baseColor = TexBaseColor.Sample(sampler_LinearWrap, uvs[0]);
	baseColor *= BaseColorFactor;
	mat.baseColor = baseColor;

	if (step(baseColor.a, AlphaCutout))
		discard;

	// emissive
	float4 emissive = TexEmissive.Sample(sampler_LinearWrap, uvs[0]);
	mat.emissive = emissive;	// float4(_EmissiveFactor, 0.0);

	// occlusion
	float occlusion = TexOcclusion.Sample(sampler_LinearWrap, uvs[0]);
	occlusion *= OcclusionStrength;
	mat.occlusion = occlusion;	// _OcclusionStrength

	#if defined(SHADING_MODEL_METALLIC_ROUGHNESS)
		float4 metallicRoughness = TexMetallicRoughness.Sample(sampler_LinearWrap, uvs[0]);
		float metallic = metallicRoughness.r;
		float perceptualRoughness = metallicRoughness.g;
		mat.metallic = metallic;	// _Metallic
		mat.perceptualRoughness = perceptualRoughness;	// _Roughness
		mat.f0 = F0;
	#elif defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
		mat.specularColor = _SpecularColor;
		mat.glossiness = _Glossiness;
	#endif

	float3 worldPos = i.worldPos;
	float3 wNormal = normalize(i.normal);
	// normal
	#if USE_SIMPLE_VERTEX
		float3 normal = wNormal;	
	#else
		float3 wTangent = normalize(i.tangent);
		float3 wBitangent = normalize(i.bitangent);
		float3 normal = _TexNormal.Sample(s_LinearRSamper, uvs[_Texcoords[0].z]);
		// debug normal
		// baseColor.rgb = normal;
		// debug end
		normal = normalize((2.0 * normal - 1) * float3(_NormalScale, _NormalScale, 1.0));
		normal = wTangent * normal.x + wBitangent * normal.y + wNormal * normal.z;
	#endif

	// view direction
	float3 viewDir = normalize(cbPerCamera.camPos - worldPos);

	float4 color = baseColor;
	float3 lighting = 0;
	// direct lighting
	// [unroll]	// '_LightNum' is not a compile time variable, cannot unroll
	// FIXME: LightNum = 2;
	for (uint idx = 0; idx < 2; ++idx) 
	{
		FLight curLight = LightBuffer[idx];
		lighting += DirectLighting(curLight, mat, worldPos, normal, viewDir);
	}

	// indirect lighting
	float3 indirectLighting = 0;
	// 
	// irradiance
	float3 diffuseColor = baseColor.rgb * (1 - metallic);
	float3 irradiance = 0;
	irradiance = ApproximateDiffuseSH(SHCoefs[0], normal, diffuseColor);

	indirectLighting += irradiance;

	// specular
	// ...
	
	//
	color.rgb = emissive.rgb + lighting * occlusion + indirectLighting;
	// baseColor.rgb *= baseColor.a;	// premultiplied color
	
	// ** debug indirectLighting **
	// color.rgb = indirectLighting;

	FLight curLight = LightBuffer[0];
	float diffuse = saturate( dot(normal, curLight.positionOrDirection.xyz) );

	color.rgb = diffuse.xxx * baseColor.rgb;
	
	// color.rgb = normal.xyz * 0.5f + 0.5f;
	
	return color;
}
