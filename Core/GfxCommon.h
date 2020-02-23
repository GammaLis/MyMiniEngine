#pragma once
#include "pch.h"
#include "ColorBuffer.h"
#include "DepthBuffer.h"
#include "ShadowBuffer.h"
#include "SamplerManager.h"
#include "CommandSignature.h"

namespace MyDirectX
{
	enum class Resolutions
	{
		k480p,
		k720p,
		k900p,
		k1080p,
		k1440p,
		k1800p,
		k2160p,
		kNumRes
	};
	enum class DebugZoom
	{
		Off,
		k2x,
		k4x,
		k8x,
		k16x,
		kNumZoom
	};
	enum class UpsampleFilter
	{
		kBilinear,
		kBicubic,
		kSharpening,
		kNumFilter
	};

	class GfxStates
	{
	public:
		static uint32_t s_DisplayWidth, s_DisplayHeight;
		static uint32_t s_NativeWidth, s_NativeHeight;

		static void SetNativeResolution(ID3D12Device *pDevice, Resolutions nativeRes);

		static float s_HDRPaperWhite;				// 100.0 - 500.0	stepSize - 50.0
		static float s_MaxDisplayLuminance;			// 500.0 - 10000.0	stepSize - 100.0
		static float s_BicubicUpsampleWeight;		// -1.0  - 0.25		stepSize - 0.25
		static float s_SharpeningSpread;			// 0.7	 - 2.0		stepSize - 0.1
		static float s_SharpeningRotation;			// 0.0	 - 90.0		stepSize - 15.0f
		static float s_SharpeningStrength;			// 0.0	 - 1.0		stepSize - 0.01

		// enums
		static Resolutions s_NativeRes;
		static DebugZoom s_DebugZoom;
		static UpsampleFilter s_UpsampleFilter;

		static DXGI_FORMAT s_DefaultHdrColorFormat;
		static DXGI_FORMAT s_DefaultDSVFormat;
	};

	class BufferManager
	{
	public:
		void InitRenderingBuffers(ID3D12Device *pDevice, uint32_t nativeWidth, uint32_t nativeHeight);
		void ResizeDisplayDependentBuffers(ID3D12Device* pDevice, uint32_t nativeWidth, uint32_t nativeHeight);
		void DestroyRenderingBuffers();

		// resources
		ColorBuffer m_SceneColorBuffer;		// R11G11B10_FLOAT
		DepthBuffer m_SceneDepthBuffer;		// D32_FLOAT_S8_UINT

		//ColorBuffer m_PoseEffectsBuffer;	// R32_UINT (to support Read-Modify-Write with a UAV)
		ColorBuffer m_VelocityBuffer;		// R10G10B10 (3D velocity)
		ColorBuffer m_OverlayBuffer;		// R8G8B8A8_UNORM
		ColorBuffer m_HorizontalBuffer;		// for separable (bicubic) upsampling

		ShadowBuffer m_ShadowBuffer;		// shadow buffer

		// reversed-Z [0, 1] - [far, near]
		// linear-Z f*n / (n + (f-n)*z')
		// 实际计算时，=1 / (1 + (f-n)/n * z')	(忽略分子f)	-20-2-20	[n/f, 1] -> 近似 [0, 1]
		ColorBuffer m_LinearDepth[2];		// normalized planar distance (0 at eye, 1 at far plane) computed from the SceneDepthBuffer

		// temporal effects
		ColorBuffer m_TemporalColor[2];		// 犹豫是将相关资源统一管理还是各个效果各自管理？？？ -20-2-19

		// post effects
		// bloom
		ColorBuffer m_aBloomUAV1[2];	// 1/3
		ColorBuffer m_aBloomUAV2[2];	// 1/6
		ColorBuffer m_aBloomUAV3[2];	// 1/12
		ColorBuffer m_aBloomUAV4[2];	// 1/24
		ColorBuffer m_aBloomUAV5[2];	// 1/48
		ColorBuffer m_LumaLR;

	};

	class ShaderManager
	{
	public:
		void CreateFromByteCode();
		void CompileShadersFromFile();

		CD3DX12_SHADER_BYTECODE m_BasicTriangleVS;
		CD3DX12_SHADER_BYTECODE m_BasicTrianglePS;

		/// screen effects
		// screen quad VS
		CD3DX12_SHADER_BYTECODE m_ScreenQuadVS;

		// present
		CD3DX12_SHADER_BYTECODE m_PresentHDRPS;
		CD3DX12_SHADER_BYTECODE m_PresentSDRPS;
		CD3DX12_SHADER_BYTECODE m_MagnifyPixelsPS;
		CD3DX12_SHADER_BYTECODE m_BilinearUpsamplePS;
		CD3DX12_SHADER_BYTECODE m_BicubicHorizontalUpsamplePS;
		CD3DX12_SHADER_BYTECODE m_BicubicVerticalUpsamplePS;
		CD3DX12_SHADER_BYTECODE m_SharpeningUpsamplePS;
		CD3DX12_SHADER_BYTECODE m_BufferCopyPS;		// blend overlay ui

		/// text 
		CD3DX12_SHADER_BYTECODE m_TextVS;
		CD3DX12_SHADER_BYTECODE m_TextAntialiasPS;
		CD3DX12_SHADER_BYTECODE m_TextShadowPS;

	};

	class CommonStates
	{
	public:
		void InitCommonStates(ID3D12Device *pDevice);
		void DestroyCommonStates();

		SamplerDesc SamplerLinearWrapDesc;
		SamplerDesc SamplerAnisoWrapDesc;
		SamplerDesc SamplerShadowDesc;
		SamplerDesc SamplerLinearClampDesc;
		SamplerDesc SamplerVolumeWrapDesc;
		SamplerDesc SamplerPointClampDesc;
		SamplerDesc SamplerPointBorderDesc;
		SamplerDesc SamplerLinearBorderDesc;

		D3D12_CPU_DESCRIPTOR_HANDLE SamplerLinearWrap;
		D3D12_CPU_DESCRIPTOR_HANDLE SamplerAnisoWrap;
		D3D12_CPU_DESCRIPTOR_HANDLE SamplerShadow;
		D3D12_CPU_DESCRIPTOR_HANDLE SamplerLinearClamp;
		D3D12_CPU_DESCRIPTOR_HANDLE SamplerVolumeWrap;
		D3D12_CPU_DESCRIPTOR_HANDLE SamplerPointClamp;
		D3D12_CPU_DESCRIPTOR_HANDLE SamplerPointBorder;
		D3D12_CPU_DESCRIPTOR_HANDLE SamplerLinearBorder;

		D3D12_RASTERIZER_DESC RasterizerDefault;		// counter-clockwise
		D3D12_RASTERIZER_DESC RasterizerDefaultMsaa;
		D3D12_RASTERIZER_DESC RasterizerDefaultWireframe;
		D3D12_RASTERIZER_DESC RasterizerDefaultCw;		// clockwise winding
		D3D12_RASTERIZER_DESC RasterizerDefaultCwMsaa;	
		D3D12_RASTERIZER_DESC RasterizerTwoSided;
		D3D12_RASTERIZER_DESC RasterizerTwoSidedMsaa;	
		D3D12_RASTERIZER_DESC RasterizerShadow;
		D3D12_RASTERIZER_DESC RasterizerShadowCW;
		D3D12_RASTERIZER_DESC RasterizerShadowTwoSided;

		D3D12_BLEND_DESC BlendNoColorWrite;
		D3D12_BLEND_DESC BlendDisable;
		D3D12_BLEND_DESC BlendPreMultiplied;
		D3D12_BLEND_DESC BlendTraditional;
		D3D12_BLEND_DESC BlendAdditive;
		D3D12_BLEND_DESC BlendTraditionalAdditive;

		D3D12_DEPTH_STENCIL_DESC DepthStateDisabled;
		D3D12_DEPTH_STENCIL_DESC DepthStateReadWrite;
		D3D12_DEPTH_STENCIL_DESC DepthStateReadOnly;
		D3D12_DEPTH_STENCIL_DESC DepthStateReadOnlyReversed;
		D3D12_DEPTH_STENCIL_DESC DepthStateTestEqual;

		CommandSignature DispatchIndirectCommandSignature{ 1 };
		CommandSignature DrawIndirectCommandSignature{ 1 };
	};

}
