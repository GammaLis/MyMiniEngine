#ifndef MATERIAL_DEFINES_HLSLI
#define MATERIAL_DEFINES_HLSLI

struct MaterialData
{
	float4 baseColor;		// base color (RGB) and opacity (A)

	float4 metalRough;		// occlusion (R), metallic (G), roughness (B)
		// specular color (RGB), glossiness (A)

	float3 emissive;	// emissive color (RGB)
	float emissiveFactor;
	float alphaCutout;	// alpha threshold, only used in case the alpha mode is mask
	float normalScale;
	float occlusionStrength;
	float f0;	// or IoR index of refraction
		// f0 = (ior - 1)^2 / (ior + 1)^2
	float specularTransmission;
	uint flags;
	uint2 padding;
};

#endif	// MATERIAL_DEFINES_HLSLI
