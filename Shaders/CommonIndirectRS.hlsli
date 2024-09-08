#include "MaterialDefines.hlsli"
#include "MeshDefines.hlsli"

#define CommonIndirect_RootSig \
	"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)," \
	"RootConstants(b0, num32BitConstants = 16)," \
	"CBV(b1)," \
	"CBV(b2)," \
	"CBV(b3)," \
	"SRV(t0, space = 1)," \
	"SRV(t1, space = 1)," \
	"SRV(t2, space = 1)," \
	"SRV(t3, space = 1)," \
	"DescriptorTable(SRV(t4, space = 1, numDescriptors = unbounded), visibility = SHADER_VISIBILITY_PIXEL)," \
	"DescriptorTable(SRV(t0, numDescriptors = 8), visibility = SHADER_VISIBILITY_PIXEL)," \
	"DescriptorTable(UAV(u0, numDescriptors = 2))," \
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

#define MATERIAL_TEXTURE_NUM 5

#if 0

cbuffer CBConstants : register(b0)
{
	float4 _X;
};
cbuffer CBPerObject : register(b1)
{
	matrix _ObjectToClip;
};

Texture2D<float4> _BaseColor	: register(t0);
Texture2D<float4> _NormalMap	: register(t1);

SamplerState s_LinearRSampler	: register(s0);
SamplerState s_PointCSampler	: register(s1);

#endif

struct ViewUniformParameters
{
	float4x4 viewProjMat;
	float4x4 invViewProjMat;
	float4x4 viewMat;
	float4x4 projMat;
	float4 bufferSizeAndInvSize;
	float4 camPos;
	float4 cascadeSplits;
	float nearClip, farClip;
};
#define USE_VIEW_UNIFORMS 1

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
 *	RootCosntants(num32BitConstants = N, bReg[, space = 0, visibility = SHADER_VISIBILITY_ALL])
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
 * Output main(intput i) {...}
 */
