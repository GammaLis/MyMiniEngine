#include "ModelViewerRS.hlsli"
#include "LightGrid.hlsli"

// outdated warning about for-loop variable scope
#pragma warning (disable: 3078)
// single-iteration loop
#pragma warning (disable: 3557)

cbuffer PSConstants	: register(b0)
{
	float3 _SunDirection;		// 0 - 4 floats
	float3 _SunColor;			// 4 - 4 floats
	float3 _AmbientColor;		// 8 - 4 floats
	float2 _ShadowTexelSize;	// 12 - 4 floats

	float4 _InvTileDim;
	uint4 _TileCount;
	uint4 _FirstLightIndex;
};

Texture2D<float3> _TexDiffuse	: register(t0);
Texture2D<float3> _TexSpecular 	: register(t1);
Texture2D<float3> _TexNormal 	: register(t2);
// Texture2D<float3> _TexEmissive 	: register(t3);
// Texture2D<float3> _TexLightMap	: register(t4);
// Texture2D<float3> _TexReflection: register(t5);

Texture2D<float> _TexSSAO 	: register(t64);
Texture2D<float> _TexShadow : register(t65);	// directional light shadowmap

// light
StructuredBuffer<LightData> _LightBuffer: register(t66);
ByteAddressBuffer _LightGrid			: register(t67);
ByteAddressBuffer _LightGridBitMask		: register(t68);
Texture2DArray<float> _LightShadowArray	: register(t69);	// non-directional light shadowmaps

SamplerState s_DefaultSampler			: register(s0);
SamplerComparisonState s_ShadowSampler	: register(s1);

struct VSOutput
{
	float4 position : SV_POSITION;
	float3 worldPos : WorldPos;
	float2 uv 		: TEXCOORD0;
	float3 viewDir	: TEXCOORD1;
	float3 shadowCoord	: TEXCOORD2;
	float3 normal 	: NORMAL;
	float3 tangent 	: TANGENT;
	float3 bitangent: BITANGENT;
};

/**
 * rsqrt - returns the reciprocal of the square root of the specified value
 * 平方根倒数
 * 1 / sqrt(x)
 *
 * rcp - component-wise reciprocal
 * 1 / x
 */
void AntiAliasSpecular(inout float3 texNormal, inout float gloss)
{
	float normalLenSq = dot(texNormal, texNormal);
	float invNormalLen = rsqrt(normalLenSq);
	texNormal *= invNormalLen;
	gloss = lerp(1, gloss, rcp(invNormalLen));
}

// apply fresnel to modulate the specular albedo
void FSchlick(inout float3 specular, inout float3 diffuse, float3 lightDir, float3 halfVec)
{
	float fresnel = pow(1.0 - saturate(dot(lightDir, halfVec)), 5.0);
	specular = lerp(specular, 1, fresnel);
	diffuse = lerp(diffuse, 0, fresnel);
}

// diffuse - Diffuse albedo
// ao - Pre-computed ambient-occlusion
// lightColor - Radiance of ambient light
float3 ApplyAmbientLight(float3 diffuse, float ao, float3 lightColor)
{
	return ao * diffuse * lightColor;
}

float GetShadow(float3 shadowCoord)
{
#ifdef SINGLE_SAMPLE
	float result = _TexShadow.SampleCmpLevelZero(s_ShadowSampler, shadowCoord.xy, shadowCoord.z);
#else
	const float dilation = 2.0;
	float2 d1 = dilation * _ShadowTexelSize.xy * 0.125;
	float2 d2 = dilation * _ShadowTexelSize.xy * 0.875;
	float2 d3 = dilation * _ShadowTexelSize.xy * 0.625;
	float2 d4 = dilation * _ShadowTexelSize.xy * 0.375;
	float result = (
		2.0 * _TexShadow.SampleCmpLevelZero(s_ShadowSampler, shadowCoord.xy, shadowCoord.z) +
		_TexShadow.SampleCmpLevelZero(s_ShadowSampler, shadowCoord.xy + float2(-d2.x,  d1.y), shadowCoord.z) +
		_TexShadow.SampleCmpLevelZero(s_ShadowSampler, shadowCoord.xy + float2(-d1.x, -d2.y), shadowCoord.z) +
		_TexShadow.SampleCmpLevelZero(s_ShadowSampler, shadowCoord.xy + float2( d2.x, -d1.y), shadowCoord.z) +
		_TexShadow.SampleCmpLevelZero(s_ShadowSampler, shadowCoord.xy + float2( d1.x,  d2.y), shadowCoord.z) +
		_TexShadow.SampleCmpLevelZero(s_ShadowSampler, shadowCoord.xy + float2(-d4.x,  d3.y), shadowCoord.z) +
		_TexShadow.SampleCmpLevelZero(s_ShadowSampler, shadowCoord.xy + float2(-d3.x, -d4.y), shadowCoord.z) +
		_TexShadow.SampleCmpLevelZero(s_ShadowSampler, shadowCoord.xy + float2( d4.x, -d3.y), shadowCoord.z) +
		_TexShadow.SampleCmpLevelZero(s_ShadowSampler, shadowCoord.xy + float2( d3.x,  d4.y), shadowCoord.z)
		) / 10.0;
#endif	// SINGLE_SAMPLE
	return result * result;
}

float GetShadowConeLight(uint lightIndex, float3 shadowCoord)
{
	float result = _LightShadowArray.SampleCmpLevelZero(
		s_ShadowSampler, float3(shadowCoord.xy, lightIndex), shadowCoord.z);
	return result * result;
}

float3 ApplyLightCommon(
	float3 diffuseColor,	// diffuse albedo
	float3 specularColor,	// specular color
	float specularMask,		// where is it shiny or dingy?
	float gloss,			// specular power
	float3 normal,			// world-space normal
	float3 viewDir,			// world-space vector from eye to point
	float3 lightDir,		// world-space vector from point to light
	float3 lightColor		// radiance of directional light
	)
{
	float3 halfVec = normalize(lightDir - viewDir);
	float NdotH = saturate(dot(halfVec, normal));

	FSchlick(diffuseColor, specularColor, lightDir, halfVec);

	float specularFactor = specularMask * pow(NdotH, gloss) * (gloss + 2) / 8;

	float NdotL = saturate(dot(normal, lightDir));

	return NdotL * lightColor * (diffuseColor + specularFactor * specularColor);
}

float3 ApplyDirectionalLight(
	float3 diffuseColor,	// diffuse albedo
	float3 specularColor,	// specular color
	float specularMask,		// where is it shiny or dingy?
	float gloss,			// specular power
	float3 normal,			// world-space normal
	float3 viewDir,			// world-space vector from eye to point
	float3 lightDir,		// world-space vector from point to light
	float3 lightColor,		// radiance of directional light
	float3 shadowCoord		// shadow coordinate (shadow map UV & light-relative Z)
	)
{
	float shadow = 1.0f;
	shadow = GetShadow(shadowCoord);
	
	return shadow * ApplyLightCommon(diffuseColor, specularColor, 
		specularMask, gloss, normal, viewDir, lightDir, lightColor);
}

float3 ApplyPointLight(
	float3 diffuseColor,	// Diffuse albedo
	float3 specularColor,	// Specular albedo
	float specularMask, 	// Where is it shiny or dingy?
	float gloss,			// Specular power
	float3 normal,			// World-space normal
	float3 viewDir,			// World-space vector from eye to point
	float3 worldPos,		// World-space fragment position
	float3 lightPos,		// World-space light position
	float lightRadiusSq,	// 
	float3 lightColor		// Radiance of directional light
	)
{
	float3 lightDir = lightPos - worldPos;
	float lightDistSq = dot(lightDir, lightDir);
	float invLightDist = rsqrt(lightDistSq);
	lightDir *= invLightDist;

	// modify 1/d^2 * R^2 to fall off at a fixed radius
	// (R/d)^2 - d/R = [(1/d^2) - (1/R^2)*(d/R)] * R^2
	float distanceFalloff = lightRadiusSq * (invLightDist * invLightDist);
	distanceFalloff = max(0, distanceFalloff - rsqrt(distanceFalloff));

	return distanceFalloff * ApplyLightCommon(
		diffuseColor,
		specularColor,
		specularMask,
		gloss,
		normal,
		viewDir,
		lightDir,
		lightColor
		);
}

float3 ApplyConeLight(
	float3 diffuseColor,	// Diffuse albedo
	float3 specularColor, 	// Specular albedo
	float specularMask,		// Where is it shiny or dingy?
	float gloss,			// Specular power
	float3 normal,			// World-space normal
	float3 viewDir,			// World-space vector from eye to point
	float3 worldPos,		// World-space fragment position
	float3 lightPos,		// World-space light position
	float lightRadiusSq,
	float3 lightColor,		// Radiance of directional light
	float3 coneDir,	
	float2 coneAngles
	)
{
	float3 lightDir = lightPos - worldPos;
	float lightDistSq = dot(lightDir, lightDir);
	float invLightDist = rsqrt(lightDistSq);
	lightDir *= invLightDist;

	// modify 1/d^2 * R^2 to fall off at a fixed radius
	// (R/d)^2 - d/R = [(1/d^2) - (1/R^2)*(d/R)] * R^2
	float distanceFalloff = lightRadiusSq * (invLightDist * invLightDist);
	distanceFalloff = max(0, distanceFalloff - rsqrt(distanceFalloff));

	float coneFalloff = dot(-lightDir, coneDir);
	coneFalloff = saturate((coneFalloff - coneAngles.y) * coneAngles.x);

	return (coneFalloff * distanceFalloff) * ApplyLightCommon(
		diffuseColor,
		specularColor,
		specularMask,
		gloss,
		normal,
		viewDir,
		lightDir,
		lightColor);
}

float3 ApplyConeShadowedLight(
    float3 diffuseColor, 	// Diffuse albedo
    float3 specularColor,	// Specular albedo
    float specularMask,		// Where is it shiny or dingy?
    float gloss,			// Specular power
    float3 normal,			// World-space normal
    float3 viewDir,			// World-space vector from eye to point
    float3 worldPos,		// World-space fragment position
    float3 lightPos,		// World-space light position
    float lightRadiusSq,
    float3 lightColor,		// Radiance of directional light
    float3 coneDir,
    float2 coneAngles,
    float4x4 shadowTextureMatrix,
    uint lightIndex
    )
{
	float4 shadowCoord = mul(float4(worldPos, 1.0), shadowTextureMatrix);
	shadowCoord.xyz *= rcp(shadowCoord.w);
	float shadow = GetShadowConeLight(lightIndex, shadowCoord.xyz);
	
	return shadow * ApplyConeLight(
		diffuseColor,
        specularColor,
        specularMask,
        gloss,
        normal,
        viewDir,
        worldPos,
        lightPos,
        lightRadiusSq,
        lightColor,
        coneDir,
        coneAngles
        );
}

// helper function for iterating over a sparse list of bits. Gets the offset of the next
// set bit, clears it, and returns the offset
uint PullNextBit(inout uint bits)
{
	uint bitIndex = firstbitlow(bits);
	bits ^= 1 << bitIndex;
	return bitIndex;
}

[RootSignature(ModelViewer_RootSig)]
float3 main(VSOutput i) : SV_TARGET
{
	uint2 pixelPos = i.position.xy;
	float3 worldPos = i.worldPos;

	float3 colorSum = float3(0.0, 0.0, 0.0);

	float3 diffuseAlbedo = _TexDiffuse.Sample(s_DefaultSampler, i.uv);

	// ao
	{
		colorSum += 0.4 * diffuseAlbedo;
	}

	float gloss = 128.0f;
	float3 normal;
	{
		normal = _TexNormal.Sample(s_DefaultSampler, i.uv) * 2.0 - 1.0;
		AntiAliasSpecular(normal, gloss);
		float3x3 tbn = float3x3(normalize(i.tangent), normalize(i.bitangent), normalize(i.normal));
		normal = normalize(mul(normal, tbn));
	}

	float3 specularAlbedo = float3(0.56, 0.56, 0.56);
	float specularMask = _TexSpecular.Sample(s_DefaultSampler, i.uv).g;
	float3 viewDir = normalize(i.viewDir);

	float3 shadowCoord = i.shadowCoord;
	colorSum += ApplyDirectionalLight(diffuseAlbedo, specularAlbedo, 
		specularMask, gloss, normal, viewDir, _SunDirection, _SunColor, shadowCoord);

	// lighting
	uint2 tilePos = GetTilePos(pixelPos, _InvTileDim.xy);
	uint tileIndex = GetTileIndex(tilePos, _TileCount.x);
	uint tileOffset = GetTileOffset(tileIndex);

	// light grid preloading setup
	uint lightBitMaskGroups[4] = {0, 0, 0, 0};
#if defined(LIGHT_GRID_PRELOADING)
	uint4 lightBitMask = lightGridBitMask.Load4(tileIndex * 16);

	lightBitMaskGroups[0] = lightBitMask.x;
	lightBitMaskGroups[1] = lightBitMask.y;
	lightBitMaskGroups[2] = lightBitMask.z;
	lightBitMaskGroups[3] = lightBitMask.w;
#endif

#define POINT_LIGHT_ARGS \
	diffuseAlbedo, \
	specularAlbedo, \
	specularMask, \
	gloss, \
	normal, \
	viewDir, \
	worldPos, \
	lightData.pos, \
	lightData.radiusSq, \
	lightData.color

#define CONE_LIGHT_ARGS \
	POINT_LIGHT_ARGS, \
	lightData.coneDir, \
	lightData.coneAngles

// #define SHADOWED_LIGHT_ARGS
// 	CONE_LIGHT_ARGS, \
// 	lightData.shadowTextureMatrix, \
// 	lightIndex

	// SM 5.0 (no wave intrinsics)
	{
		// 4 bytes - light count 
		uint tileLightCount = _LightGrid.Load(tileOffset + 0);
		uint tileLightCountSphere = (tileLightCount >> 0) & 0xff;
		uint tileLightCountCone = (tileLightCount >> 8) & 0xff;
		uint tileLightCountConeShadowed = (tileLightCount >> 16) & 0xff;

		uint tileLightLoadOffset = tileOffset + 4;

		// sphere
		for (uint n = 0; n < tileLightCountSphere; ++n, tileLightLoadOffset += 4)
		{
			uint lightIndex = _LightGrid.Load(tileLightLoadOffset);
			LightData lightData = _LightBuffer[lightIndex];
			colorSum += ApplyPointLight(POINT_LIGHT_ARGS);
		}

		// cone 
		for (uint n = 0; n < tileLightCountCone; ++n, tileLightLoadOffset += 4)
		{
			uint lightIndex = _LightGrid.Load(tileLightLoadOffset);
			LightData lightData = _LightBuffer[lightIndex];
			colorSum += ApplyConeLight(CONE_LIGHT_ARGS);
		}

		// cone w/ shadow map
		// for (uint n = 0; n < tileLightCountCone; ++n, tileLightLoadOffset += 4)
		// {
		// 	uint lightIndex = _LightGrid.Load(tileLightLoadOffset);
		// 	LightData lightData = _LightBuffer[lightIndex];
		// 	colorSum += ApplyConeShadowedLight(SHADOWED_LIGHT_ARGS);
		// }

		// debug
		// float countFactor = (tileLightCountSphere + tileLightCountCone + tileLightCountConeShadowed) / 32.0;
		// colorSum = lerp(float3(0, 1, 0), float3(1, 0, 0), countFactor);
	}

	return colorSum;
}

/**
 * https://docs.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-per-component-math
 * float2x2 fMatrix = { 
 * 		0.0f, 0.1f,	// row 1
 * 		2.0f, 2.2f, // row 2
 * };
 * or matrix<float, 2, 2> fMatrix = ...
 * 
 * a matrix contains values organized in rows and columns, which
 *can be accessed using the structure operator "." followed by one 
 *of the 2 naming sets:
 *		the 0-based row-column position:
 *		_m00, _m01, _m02, _m03,
 *		_m10, _m11, _m12, _m13,
 *		_m20, _m21, _m22, _m23,
 *		_m30, _m31, _m32, _m33,
 *		the 1-based row-column position:
 *		_11, _12, _13, _14,
 *		_21, _22, _23, _24,
 *		_31, _32, _33, _34,
 *		_41, _42, _43, _44,
 *		
 *  a matrix can also be accessed using array access notation, which
 * is a 0-based set of indices. Each index is inside of square brackets
 * 		[0][0], [0][1], [0][2], [0][3],
 * 		[1][0], [1][1], [1][2], [1][3],
 * 		[2][0], [2][1], [2][2], [2][3],
 * 		[3][0], [3][1], [3][2], [3][3],
 *
 *	>> Matrix Ordering
 *		Matrix packing order for uniform parameters is set to column-major
 *	by default. This means each column of the matrix is stored in a single
 *	constant register. On the other hand, a row-major matrix packs each row
 *	of the matrix in a single constant register. Matrix packing can be changed
 *	with the #pragmapack_matrix directive, or with the row_major or
 *	the column_major keyword.
 *		the data in a matrix is loaded into shader constant registers before
 *	a shader runs. There are 2 choices for how the matrix data is read: in 
 *	row-major order or in column-major order. 
 *	A row-major matrix is laid out like the following:
 *		11	12	13	14
 *		21	22	23	24
 *		31	32	33	34
 *		41	42	43	44
 *	A column-major matrix is laid out like the following:
 *		11 	21	31	41	
 *		12 	22	32	42
 *		13 	23	33	43
 *		14 	24	34	44
 *		
 *	Row-major and column-major matrix ordering determine the order the matrix
 *components are read from shader inputs. Once the data is written into constant
 *registers, matrix order has no effect on how the data is used or accessed from 
 *within shader code.	
 *	Row-major and column-major packing order has no influence on the packing
 *order of constructors (which always follows row-major ordering)
 *	the order of the data in a matrix can be declared at compile time or the compiler
 *will order the data at runtime for the most effcient use.	
 */