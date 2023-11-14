#include "IndirectGBufferRS.hlsli"
#include "Quaternion.hlsli"
#include "../Scenes/MaterialDefines.h"

struct VSOutput
{
	float4 pos 	: SV_POSITION;
	float2 uv0 	: TEXCOORD0;
	float3 worldPos	: TEXCOORD1;
	float  depthVS	: TEXCOORD2;
	uint materialID	: TEXCOORD3;
	float3 normal 	: NORMAL;
	float3 tangent 	: TANGENT;
	float3 bitangent: BITANGENT;

	uint drawId	: DRAWID;
};

struct PSOutput
{
	float4 TangentFrame : SV_TARGET0;
    float4 UV 			: SV_TARGET1;
	float4 UVGradients 	: SV_TARGET2;
	uint MaterialID 	: SV_TARGET3;
};

Texture2D _MaterialTextures[] : register(t4, space1);

[RootSignature(IndirectGBuffer_RootSig)]
PSOutput main(VSOutput i)
{
	PSOutput o;

	float3 normal = normalize(i.normal);
	float3 tangent = normalize(i.tangent);
	float3 bitangent = normalize(i.bitangent);

	// the tangent frame can have arbitrary handedness, so we force it to be left-handed and then
	// pack the handedness into the material ID
	float handedness = dot(bitangent, cross(normal, tangent)) > 0.0f ? 1.0f : -1.0f;
	bitangent *= handedness;

	Quaternion tangentFrame = QuatFrom3x3(float3x3(tangent, bitangent, normal));

	// Alpha clip
#ifdef ALPHA_CLIP
	MeshInstanceData meshInstance = _MeshInstanceBuffer[i.drawId];
	uint materialId = meshInstance.materialID;
	MaterialData material = _MaterialBuffer[materialId];
	float4 _BaseColorFactor = material.baseColor;
	float _AlphaCutout = material.alphaCutout;
	uint _Flags = material.flags;

	uint texbase = MATERIAL_TEXTURE_NUM * materialId;
	Texture2D _TexBaseColor = _MaterialTextures[texbase + 0];

	float4 baseColor = _BaseColorFactor;

	uint diffuseType = EXTRACT_DIFFUSE_TYPE(_Flags);
	uint alphaMode = EXTRACT_ALPHA_MODE(_Flags);
	if (diffuseType == ChannelTypeTexture)
		baseColor = _TexBaseColor.Sample(s_LinearRSampler, i.uv0);

	// alpha test
	if (alphaMode == AlphaModeMask)
	{
		if (baseColor.a < _AlphaCutout)
			discard;
	}
#endif

	o.TangentFrame = PackQuaternion(tangentFrame);
	o.UV.xy = frac(i.uv0 / DeferredUVScale);
	o.UV.zw = float2(ddx_fine(i.pos.z), ddy_fine(i.pos.z));
	o.UV.zw = sign(o.UV.zw) * pow(abs(o.UV.zw), 1 / 2.0f);
	o.MaterialID = ((i.materialID+1) & 0x7f);	// materialIDTarget 元素默认为0，这里+1用以区分无材质部分
	if (handedness == -1.0f)
		o.MaterialID |= 0x80;
	o.UVGradients = float4(ddx_fine(i.uv0), ddy_fine(i.uv0));

	return o;
}
