#ifndef MATERIAL_DEFINES_H
#define MATERIAL_DEFINES_H

#define ShadingModel_Unlit				0
#define ShadingModel_MetallicRoughness	1
// #define ShadingModel_MetallicAnisoRoughness 2	// reserved for future use
#define ShadingModel_SpecularGlossiness 3

// channel type
#define ChannelTypeUnused	0
#define ChannelTypeConst	1
#define ChannelTypeTexture	2

// normal map type
#define NormalMapUnused	0
#define NormalMapRGB	1
#define NormalMapRG		2

// alpha mode
#define AlphaModeOpqaue	0
#define AlphaModeMask	1

// flags
// low -> high (bits)
// |	3			|	3			|	3			|	3			|	2		|	1			|	2		|	1			|
// | shadingModel	| diffuseType	| specularType	| emissiveType	| normalMap | occlusionMap	| alphaMode	| doubleSided	|
// bit count
#define SHADING_MODEL_BITS	(3)
#define DIFFUSE_TYPE_BITS	(3)
#define SPECULAR_TYPE_BITS	(3)
#define EMISSIVE_TYPE_BITS	(3)
#define NORMAL_MAP_BITS		(2)
#define OCCLUSION_MAP_BITS	(1)
#define ALPHA_MODE_BITS		(2)
#define DOUBLE_SIDED_BITS	(1)

// bit offsets
#define SHADING_MODEL_OFFSET	(0)
#define DIFFUSE_TYPE_OFFSET		(SHADING_MODEL_OFFSET	+ SHADING_MODEL_BITS)
#define SPECULAR_TYPE_OFFSET	(DIFFUSE_TYPE_OFFSET	+ DIFFUSE_TYPE_BITS)
#define EMISSIVE_TYPE_OFFSET	(SPECULAR_TYPE_OFFSET	+ SPECULAR_TYPE_BITS)
#define NORMAL_MAP_OFFSET		(EMISSIVE_TYPE_OFFSET	+ EMISSIVE_TYPE_BITS)
#define OCCLUSION_MAP_OFFSET	(NORMAL_MAP_OFFSET		+ NORMAL_MAP_BITS)
#define ALPHA_MODE_OFFSET		(OCCLUSION_MAP_OFFSET	+ OCCLUSION_MAP_BITS)
#define DOUBLE_SIDED_OFFSET		(ALPHA_MODE_OFFSET		+ ALPHA_MODE_BITS)

// extract bits
#define EXTRACT_BITS(bits, offset, value)	((value >> offset) & ((1 << bits) - 1))
#define EXTRACT_SHADING_MODEL(value)	EXTRACT_BITS(SHADING_MODEL_BITS,	SHADING_MODEL_OFFSET,	value)
#define EXTRACT_DIFFUSE_TYPE(value)		EXTRACT_BITS(DIFFUSE_TYPE_BITS,		DIFFUSE_TYPE_OFFSET,	value)
#define EXTRACT_SPECULAR_TYPE(value)	EXTRACT_BITS(SPECULAR_TYPE_BITS,	SPECULAR_TYPE_OFFSET,	value)
#define EXTRACT_EMISSIVE_TYPE(value)	EXTRACT_BITS(EMISSIVE_TYPE_BITS,	EMISSIVE_TYPE_OFFSET,	value)
#define EXTRACT_NORMAL_MAP(value)		EXTRACT_BITS(NORMAL_MAP_BITS,		NORMAL_MAP_OFFSET,		value)
#define EXTRACT_OCCLUSION_MAP(value)	EXTRACT_BITS(OCCLUSION_MAP_BITS,	OCCLUSION_MAP_OFFSET,	value)
#define EXTRACT_ALPHA_MODE(value)		EXTRACT_BITS(ALPHA_MODE_BITS,		ALPHA_MODE_OFFSET,		value)
#define EXTRACT_DOUBLE_SIDED(value)		EXTRACT_BITS(DOUBLE_SIDED_BITS,		DOUBLE_SIDED_OFFSET,	value)

// pack bits
#define PACK_BITS(bits, offset, flags, value)		( ((value & ((1<<bits)- 1)) << offset) | ( flags & (~( ((1<<bits)-1) << offset)) ) )
#define PACK_SHADING_MODEL(flags, value)    PACK_BITS(SHADING_MODEL_BITS,    SHADING_MODEL_OFFSET,   flags, value)
#define PACK_DIFFUSE_TYPE(flags, value)     PACK_BITS(DIFFUSE_TYPE_BITS,     DIFFUSE_TYPE_OFFSET,    flags, value)
#define PACK_SPECULAR_TYPE(flags, value)    PACK_BITS(SPECULAR_TYPE_BITS,    SPECULAR_TYPE_OFFSET,   flags, value)
#define PACK_EMISSIVE_TYPE(flags, value)    PACK_BITS(EMISSIVE_TYPE_BITS,    EMISSIVE_TYPE_OFFSET,   flags, value)
#define PACK_NORMAL_MAP_TYPE(flags, value)  PACK_BITS(NORMAL_MAP_BITS,       NORMAL_MAP_OFFSET,      flags, value)
#define PACK_OCCLUSION_MAP(flags, value)    PACK_BITS(OCCLUSION_MAP_BITS,    OCCLUSION_MAP_OFFSET,   flags, value)
#define PACK_ALPHA_MODE(flags, value)       PACK_BITS(ALPHA_MODE_BITS,       ALPHA_MODE_OFFSET,      flags, value)
#define PACK_DOUBLE_SIDED(flags, value)     PACK_BITS(DOUBLE_SIDED_BITS,     DOUBLE_SIDED_OFFSET,    flags, value)

#endif	// MATERIAL_DEFINES_H
