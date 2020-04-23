#include "MaterialDefines.hlsli"
#include "MeshDefines.hlsli"

#define CommonIndirect_RootSig \
	"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)," \
	"RootConstants(b0, num32BitConstants = 4)," \
	"CBV(b1)," \
	"CBV(b2)," \
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
 * RootFlags - ¿ÉÑ¡µÄRootFlags²ÉÓÃ0£¨Ä¬ÈÏÖµ£¬±íÊ¾ÎÞ±êÖ¾£©£¬»òÒ»¸ö»ò¶à¸öÔ¤¶¨ÒåµÄ¸ù±êÖ¾Öµ£¨Í¨¹ýOR"|"Á¬½Ó£©
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

 *	RootConstants - 2¸ö±ØÐèµÄ²ÎÊýÊÇcbufferµÄnum32BitConstantsºÍbReg, space ºÍvisibilityÊÇ¿ÉÑ¡µÄ
 *	RootCosntants(num32BitConstants = N, bReg[, space = 0, visibility = SHADER_VISIBILITY_ALL])
 *
 * 	Visibility - ¿ÉÑ¡²ÎÊý
 * 	SHADER_VISIBILITY_ALL½«¸ù²ÎÊý¹ã²¥µ½ËùÓÐ×ÅÉ«Æ÷¡£ÔÚÄ³Ð©Ó²¼þÉÏ£¬´Ë²Ù×÷²»»áÔì³É¿ªÏú£¬µ«ÔÚÆäËûÓ²¼þÉÏ£¬
 *½«Êý¾Ý·Ö²æµ½ËùÓÐ×ÅÉ«Æ÷½×¶Î»áÔì³É¿ªÏú¡£ÉèÖÃÆäÖÐÒ»¸öÑ¡Ïî£¨ÀýÈçSHADER_VISIBILITY_VERTEX£©»á½«¸ù²ÎÊý
 *ÏÞÖÆµ½µ¥¸ö×ÅÉ«Æ÷
 *	½«¸ú²ÎÊýÉèÖÃµ½µ¥¸ö×ÅÉ«Æ÷½×¶Î¿ÉÔÚ²»Í¬µÄ½×¶ÎÊ¹ÓÃÏàÍ¬µÄ°ó¶¨Ãû³Æ¡£ ÀýÈç£¬t0, SHADER_VISIBILITY_VERTEX SRV°ó¶¨
 *ºÍt0, SHADER_VISIBILITY_PIXEL SRV °ó¶¨ÊÇÓÐÐ§µÄ¡£
 *
 * 	CBV(bReg[, space = 0, visibility = ...])
 * 	SRV(tReg[, space = 0, visibliity = ...])
 * 	UAV(uReg[, space = 0, visibility = ...])
 *
 * 	DescriptorTable(DTClause1[, DTClause2, ..., DTClauseN, visibility = ...])
 * 		CBV(bReg, [numDescriptors = 1, space = 0, offset = DESCRIPTOR_RANGE_OFFSET_APPEND, flags = ...])
 *   	SRV(tReg, [numDescriptors = 1, space = 0, offset = DESCRIPTOR_RANGE_OFFSET_APPEND, flags = ...])
 *   	UAV(uReg, [numDescriptors = 1, space = 0, offset = DESCRIPTOR_RANGE_OFFSET_APPEND, flags = ...])
 * 	µ±numDescriptorsÎªÊý×ÖÊ±£¬¸ÃÌõÄ¿ÉùÃ÷cbuffer·¶Î§[Reg, Reg + numDescriptors - 1]
 * 	Èç¹ûnumDescriptorsµÈÓÚ"unbounded"£¬Ôò·¶Î§Îª[Reg, UINT_MAX],ÕâÒâÎ¶×Å£¬Ó¦ÓÃ±ØÐëÈ·±£Ëü²»»á
 * 	ÒýÓÃ½çÍâÇøÓò
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
