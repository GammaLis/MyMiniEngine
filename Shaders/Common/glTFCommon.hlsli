#define Common_RootSig \
	"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)," \
	"RootConstants(b0, num32BitConstants = 4)," \
	"CBV(b1)," \
	"CBV(b2)," \
	"CBV(b3, visibility = SHADER_VISIBILITY_PIXEL)," \
	"DescriptorTable(SRV(t0, numDescriptors = 8), visibility = SHADER_VISIBILITY_PIXEL)," \
	"SRV(t1, space = 1)," \
	"StaticSampler(s0, " \
		"addressU = TEXTURE_ADDRESS_WRAP," \
		"addressV = TEXTURE_ADDRESS_WRAP," \
		"addressW = TEXTURE_ADDRESS_WRAP," \
		"filter = FILTER_MIN_MAG_MIP_LINEAR)," \
	"StaticSampler(s1, " \
		"addressU = TEXTURE_ADDRESS_CLAMP," \
		"addressV = TEXTURE_ADDRESS_CLAMP," \
		"addressW = TEXTURE_ADDRESS_CLAMP," \
		"filter = FILTER_MIN_MAG_MIP_POINT)"

#define GL_UV_STARTS_AT_BOTTOMLEFT
#define USE_SIMPLE_VERTEX 1
#define USE_DESCRIPTOR_HEAP_INDEX 1

#define SHADING_MODEL_METALLIC_ROUGHNESS

#if USE_SIMPLE_VERTEX

struct VSInput
{
	float3 position : POSITION;
	float2 uv0 : TEXCOORD0;
	float3 normal : NORMAL;
};

// TODO: optimization
struct VSOutput
{
	float4 pos : SV_POSITION;
	float2 uv0 : TEXCOORD0;
	float2 uv1 : TEXCOORD1;
	float3 worldPos : TEXCOORD2;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float3 bitangent : TEXCOORD3;
	float3 color : COLOR0;
};

#else

struct VSInput
{
	float3 position : POSITION;
	float2 uv0		: TEXCOORD0;
	float2 uv1		: TEXCOORD1;
	float3 normal 	: NORMAL;
	float4 tangent	: TANGENT;
	float3 color	: COLOR0;
};

struct VSOutput
{
	float4 pos 	: SV_POSITION;
	float2 uv0 	: TEXCOORD0;
	float2 uv1	: TEXCOORD1;
	float3 worldPos	: TEXCOORD2;
	float3 normal 	: NORMAL;
	float3 tangent 	: TANGENT;
	float3 bitangent: TEXCOORD3;
	float3 color 	: COLOR0;
};
#endif

#if !USE_DESCRIPTOR_HEAP_INDEX

cbuffer CBConstants : register(b0)
{
	float4 _Constants;
};
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

static const uint kInstanceBufferMaxNum = 4096u;
static const uint kMaterialBufferMaxNum = 1024u;

struct CBPerObject
{
	float4x4 worldMat;
	float4x4 invWorldMat;
	uint materialIndex;
	float padding0[3];
};

struct CBObjects
{
	CBPerObject objs[kInstanceBufferMaxNum];
};

struct CBPerCamera
{
	float4x4 viewProjMat;
	float3   camPos;
};

struct CBPerMaterial
{
	float4 baseColorFactor;
	float3 emissiveFactor;
	float alphaCutout;
	uint4 textureIndices[2];
	#if defined(SHADING_MODEL_METALLIC_ROUGHNESS)
		float metallic;
		float roughness;
		float f0;
		float padding;
	#elif defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
		float3 specularColor;
		float glossiness;
	#endif
	float normalScale;
	float occlusionStrength;
	float padding2[2];
	
	void Reset()
	{
		baseColorFactor = float4(1, 1, 1, 1);
		emissiveFactor = float3(1, 1, 1);
		alphaCutout = 0.5;
		
		#if defined(SHADING_MODEL_METALLIC_ROUGHNESS)
			metallic = 0.5;
			roughness = 0.5;
			f0 = 0.04;
		#elif defined(SHADING_MODEL_SPECULAR_GLOSSINESS)
			specularColor;
			glossiness;
		#endif
		normalScale = 1.0;
		occlusionStrength = 1.0;
	}
};

struct CBMaterials
{
	CBPerMaterial materials[kMaterialBufferMaxNum];
};

cbuffer CBConstants : register(b0, space1)
{
	float4 _Miscs;
	float4 _LightAndSH;
}

// static const uint kSlotLightBuffer = 0;
// static const uint kSlotSHCoefficients = 1;

// Total 2048
static const uint kSlotTemporaryStart = 1024; 
static const uint kSlotCamera = kSlotTemporaryStart + 0;
static const uint kSlotMaterialBuffer = kSlotTemporaryStart + 1;
static const uint kSlotInstanceBuffer = kSlotTemporaryStart + 2;

uint GetDrawId() { return uint(_Miscs.x); }
uint GetSlotLightBuffer() { return uint(_LightAndSH.x); }
uint GetSlotSHCoefficients() { return uint(_LightAndSH.y); }
uint GetSlotCamera() { 	return kSlotCamera; }
uint GetSlotMaterialBuffer() { 	return kSlotMaterialBuffer; }
uint GetSlotInstanceBuffer() { return kSlotInstanceBuffer; }
uint GetSlotMaterialTextureStart(CBPerMaterial material) { return material.textureIndices[0].x; }

// cbuffer CBConstants	: register(b0)
// {
// 	float4 _X;
// };
// cbuffer CBPerObject	: register(b1)
// {
// 	matrix _ObjectToClip;
// };

// Texture2D<float4> _BaseColor	: register(t0);
// Texture2D<float4> _NormalMap	: register(t1);

// SamplerState s_LinearRSampler: register(s0);
// SamplerState s_PointCSampler	: register(s1);

/**
 * https://docs.microsoft.com/zh-cn/windows/win32/direct3d12/specifying-root-signatures-in-hlsl
 * RootSignature
 * RootFlags - 可选的RootFlags采用0（默认值，表示无标志），或一个或多个预定义的根标志值（通过OR"|"连接）
 * 
 * typedef enum D3D12_ROOT_SIGNATURE_FLAGS {
	  D3D12_ROOT_SIGNATURE_FLAG_NONE,
	  D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
	  D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS,
	  D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS,
	  D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS,
	  D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS,
	  D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS,
	  D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT,
	  D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE
	} ;
	
 *	RootConstants - 2个必需的参数是cbuffer的num32BitConstants和bReg, space 和visibility是可选的
 *	RootConstants(num32BitConstants = N, bReg[, space = 0, visibility = SHADER_VISIBILITY_ALL])
 *
 * 	Visibility - 可选参数
 * 	SHADER_VISIBILITY_ALL将根参数广播到所有着色器。在某些硬件上，此操作不会造成开销，但在其他硬件上，
 *将数据分叉到所有着色器阶段会造成开销。设置其中一个选项（例如SHADER_VISIBILITY_VERTEX）会将根参数
 *限制到单个着色器
 *	将跟参数设置到单个着色器阶段可在不同的阶段使用相同的绑定名称。 例如，t0, SHADER_VISIBILITY_VERTEX SRV绑定
 *和t0, SHADER_VISIBILITY_PIXEL SRV 绑定是有效的。
 *
 * 	CBV(bReg[, space = 0, visibility = ...])
 * 	SRV(tReg[, space = 0, visibliity = ...])
 * 	UAV(uReg[, space = 0, visibility = ...])
 *
 * 	DescriptorTable(DTClause1[, DTClause2, ..., DTClauseN, visibility = ...])
 * 		CBV(bReg, [numDescriptors = 1, space = 0, offset = DESCRIPTOR_RANGE_OFFSET_APPEND, flags = ...])
 *   	SRV(tReg, [numDescriptors = 1, space = 0, offset = DESCRIPTOR_RANGE_OFFSET_APPEND, flags = ...])
 *   	UAV(uReg, [numDescriptors = 1, space = 0, offset = DESCRIPTOR_RANGE_OFFSET_APPEND, flags = ...])
 * 	当numDescriptors为数字时，该条目声明cbuffer范围[Reg, Reg + numDescriptors - 1]
 * 	如果numDescriptors等于"unbounded"，则范围为[Reg, UINT_MAX],这意味着，应用必须确保它不会
 * 	引用界外区域
 * 
 *  StaticSampler(sReg[,
 *  	filter = FILTER_ANISOTROPIC,
 *  	addressU = TEXTURE_ADDRESS_WRAP,
 *  	addressV = TEXTURE_ADDRESS_WRAP,
 *  	addressW = TEXTURE_ADDRESS_WARP,
 *  	mipLODBias = 0.5,
 *  	maxAnisotropy = 16,
 *  	comparisonFunc = COMPARISON_LESS_EQUAL,
 *  	borderColor = STATIC_BORDER_COLOR_OPAQUE_WHITE,
 *  	minLOD = 0.f,
 *  	maxLOD = ...,
 *  	space = 0,
 *  	visibility = ...])
 *
 * [RootSignature(MyRS)]
 * Output main(input i) {...}
 */
