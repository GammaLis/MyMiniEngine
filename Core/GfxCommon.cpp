#include "GfxCommon.h"
#include "Graphics.h"
#include "CommandListManager.h"
#include "CommandContext.h"
#include <d3dcompiler.h>

#include "Effects.h"
#include "TemporalAA.h"

#include "ScreenQuadVS.h"

/**
	present
*/
#include "PresentHDRPS.h"
#include "PresentSDRPS.h"
#include "MagnifyPixelsPS.h"
// bicubic upsample
#include "BicubicHorizontalUpsamplePS.h"
#include "BicubicVerticalUpsamplePS.h"
// sharpening upsample
#include "SharpeningUpsamplePS.h"
// bilinear upsample
#include "BilinearUpsamplePS.h"
// blend overlay ui
#include "BufferCopyPS.h"

/**
	text
*/
#include "TextVS.h"
#include "TextAntialiasingPS.h"
#include "TextShadowPS.h"

// test
#include "BasicTriangleVS.h"
#include "BasicTrianglePS.h"

#pragma comment(lib, "d3dcompiler.lib")

using namespace MyDirectX;

using Microsoft::WRL::ComPtr;

uint32_t GfxStates::s_DisplayWidth = 1280, GfxStates::s_DisplayHeight = 720;
uint32_t GfxStates::s_NativeWidth = 0, GfxStates::s_NativeHeight = 0;

bool GfxStates::s_bEnableHDROutput = false;

bool GfxStates::s_bTypedUAVLoadSupport_R11G11B10_FLOAT = false;
bool GfxStates::s_bTypedUAVLoadSupport_R16G16B16A16_FLOAT = false;

float GfxStates::s_HDRPaperWhite = 200.0f;
float GfxStates::s_MaxDisplayLuminance = 1000.0f;

// https://en.wikipedia.org/wiki/Bicubic_interpolation
// BicubicUpsample A - 通常 -0.5 -0.75
float GfxStates::s_BicubicUpsampleWeight = -0.75f;		// -1.0 - 0.25		stepSize - 0.25
float GfxStates::s_SharpeningSpread = 1.0f;				// 0.7 - 2.0		stepSize - 0.1
float GfxStates::s_SharpeningRotation = 45.0f;			// 0.0 - 90.0		stepSize - 15.0f
float GfxStates::s_SharpeningStrength = 0.1f;			// 0.0 - 1.0

// enums
Resolutions GfxStates::s_NativeRes = (Resolutions)-1;
DebugZoom GfxStates::s_DebugZoom = DebugZoom::Off;
UpsampleFilter GfxStates::s_UpsampleFilter = UpsampleFilter::kBilinear;

DXGI_FORMAT GfxStates::s_DefaultHdrColorFormat = DXGI_FORMAT_R11G11B10_FLOAT;
DXGI_FORMAT GfxStates::s_DefaultDSVFormat = DXGI_FORMAT_D32_FLOAT;

void GfxStates::SetNativeResolution(ID3D12Device* pDevice, Resolutions nativeRes)
{
	if (s_NativeRes == nativeRes)
		return;

	switch (nativeRes)
	{
	default:
	case Resolutions::k480p:
		s_NativeWidth = 640;
		s_NativeHeight = 480;
		break;

	case Resolutions::k720p:
		s_NativeWidth = 1280;
		s_NativeHeight = 720;
		break;

	case Resolutions::k900p:
		s_NativeWidth = 1600;
		s_NativeHeight = 900;
		break;

	case Resolutions::k1080p:
		s_NativeWidth = 1920;
		s_NativeHeight = 1080;
		break;

	case Resolutions::k1440p:
		s_NativeWidth = 2560;
		s_NativeHeight = 1440;
		break;

	case Resolutions::k1800p:
		s_NativeWidth = 3200;
		s_NativeHeight = 1800;
		break;

	case Resolutions::k2160p:
		s_NativeWidth = 3840;
		s_NativeHeight = 720;
		break;
	}

	DEBUGPRINT("Changing native resolution to %ux%u", s_NativeWidth, s_NativeHeight);

	s_NativeRes = nativeRes;

	Graphics::s_CommandManager.IdleGPU();

	Graphics::s_BufferManager.InitRenderingBuffers(pDevice, s_NativeWidth, s_NativeHeight);

}

void BufferManager::InitRenderingBuffers(ID3D12Device* pDevice, uint32_t bufferWidth, uint32_t bufferHeight)
{
	GraphicsContext& initContext = GraphicsContext::Begin();

	const uint32_t bufferWidth1 = (bufferWidth + 1) / 2;
	const uint32_t bufferWidth2 = (bufferWidth + 3) / 4;
	const uint32_t bufferWidth3 = (bufferWidth + 7) / 8;
	const uint32_t bufferWidth4 = (bufferWidth + 15) / 16;
	const uint32_t bufferWidth5 = (bufferWidth + 31) / 32;
	const uint32_t bufferWidth6 = (bufferWidth + 63) / 64;
	const uint32_t bufferHeight1 = (bufferHeight + 1) / 2;
	const uint32_t bufferHeight2 = (bufferHeight + 3) / 4;
	const uint32_t bufferHeight3 = (bufferHeight + 7) / 8;
	const uint32_t bufferHeight4 = (bufferHeight + 15) / 16;
	const uint32_t bufferHeight5 = (bufferHeight + 31) / 32;
	const uint32_t bufferHeight6 = (bufferHeight + 63) / 64;

	// 
	m_SceneColorBuffer.Create(pDevice, L"Main Color Buffer", bufferWidth, bufferHeight, 1, GfxStates::s_DefaultHdrColorFormat);
	m_SceneColorBuffer.SetClearColor(Color(0.2f, 0.4f, 0.4f));
	m_SceneDepthBuffer.Create(pDevice, L"Scene Depth Buffer", bufferWidth, bufferHeight, GfxStates::s_DefaultDSVFormat);

	m_VelocityBuffer.Create(pDevice, L"Motion Vectors", bufferWidth, bufferHeight, 1, DXGI_FORMAT_R32_UINT);

	m_PoseEffectsBuffer.Create(pDevice, L"Post Effects Buffer", bufferWidth, bufferHeight, 1, DXGI_FORMAT_R32_UINT);

	m_LinearDepth[0].Create(pDevice, L"Linear Depth 0", bufferWidth, bufferHeight, 1, DXGI_FORMAT_R16_UNORM);
	m_LinearDepth[1].Create(pDevice, L"Linear Depth 1", bufferWidth, bufferHeight, 1, DXGI_FORMAT_R16_UNORM);

	m_ShadowBuffer.Create(pDevice, L"Shadow Map", 2048, 2048);

	/// effects
	
	// temporal effects
	m_TemporalColor[0].Create(pDevice, L"Temporal Color 0", bufferWidth, bufferHeight, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_TemporalColor[1].Create(pDevice, L"Temporal Color 1", bufferWidth, bufferHeight, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
	Effects::s_TemporalAA.ClearHistory(initContext);		// 清空

	// post effects
	// this is useful for storing per-pixel weights such as motion strength or pixel luminance
	m_LumaBuffer.Create(pDevice, L"Luminance", bufferWidth, bufferHeight, 1, DXGI_FORMAT_R8_UNORM);
	m_Histogram.Create(pDevice, L"Histogram", 256, 4);

	// bloom and tone mapping
	// MS-MiniEngine 自己注释
	// divisible by 128 so that after dividing by 16, we still have multiples of 8x8 tiles
	// the bloom dimensions must be at least 1/4 natives resolution to avoid undersampling
	//uint32_t kBloomWidth = bufferWidth > 2560 ? Math::AlignUp(bufferWidth / 4, 128) : 640;
	//uint32_t kBloomHeight = bufferHeight > 1440 ? Math::AlignUp(bufferHeight / 4, 128) : 384;
	uint32_t kBloomWidth = bufferWidth > 2560 ? 1280 : 640;
	uint32_t kBloomHeight = bufferHeight > 1440 ? 768 : 384;
	m_LumaLR.Create(pDevice, L"Luma Buffer", kBloomWidth, kBloomHeight, 1, DXGI_FORMAT_R8_UINT);

	m_aBloomUAV1[0].Create(pDevice, L"Bloom Buffer 1a", kBloomWidth,	kBloomHeight,	 1, GfxStates::s_DefaultHdrColorFormat);
	m_aBloomUAV1[1].Create(pDevice, L"Bloom Buffer 1b", kBloomWidth,	kBloomHeight,	 1, GfxStates::s_DefaultHdrColorFormat);
	m_aBloomUAV2[0].Create(pDevice, L"Bloom Buffer 2a", kBloomWidth/2,	kBloomHeight/2,  1, GfxStates::s_DefaultHdrColorFormat);
	m_aBloomUAV2[1].Create(pDevice, L"Bloom Buffer 2b", kBloomWidth/2,	kBloomHeight/2,  1, GfxStates::s_DefaultHdrColorFormat);
	m_aBloomUAV3[0].Create(pDevice, L"Bloom Buffer 3a", kBloomWidth/4,	kBloomHeight/4,  1, GfxStates::s_DefaultHdrColorFormat);
	m_aBloomUAV3[1].Create(pDevice, L"Bloom Buffer 3b", kBloomWidth/4,	kBloomHeight/4,  1, GfxStates::s_DefaultHdrColorFormat);
	m_aBloomUAV4[0].Create(pDevice, L"Bloom Buffer 4a", kBloomWidth/8,	kBloomHeight/8,  1, GfxStates::s_DefaultHdrColorFormat);
	m_aBloomUAV4[1].Create(pDevice, L"Bloom Buffer 4b", kBloomWidth/8,	kBloomHeight/8,  1, GfxStates::s_DefaultHdrColorFormat);
	m_aBloomUAV5[0].Create(pDevice, L"Bloom Buffer 5a", kBloomWidth/16, kBloomHeight/16, 1, GfxStates::s_DefaultHdrColorFormat);
	m_aBloomUAV5[1].Create(pDevice, L"Bloom Buffer 5b", kBloomWidth/16, kBloomHeight/16, 1, GfxStates::s_DefaultHdrColorFormat);

	// UI overlay
	m_OverlayBuffer.Create(pDevice, L"UI Overlay", GfxStates::s_DisplayWidth, GfxStates::s_DisplayHeight, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
	// for debug
	// m_OverlayBuffer.SetClearColor(Color(0.6f, 0.4f, 0.2f, 0.2f));

	// bicubic horizontal upsample intermediate buffer
	m_HorizontalBuffer.Create(pDevice, L"Bicubic Intermediate", GfxStates::s_DisplayWidth, bufferHeight, 1, GfxStates::s_DefaultHdrColorFormat);

	initContext.Finish();
}

void BufferManager::ResizeDisplayDependentBuffers(ID3D12Device* pDevice, uint32_t bufferWidth, uint32_t bufferHeight)
{
	// UI overlay
	m_OverlayBuffer.Create(pDevice, L"UI Overlay", GfxStates::s_DisplayWidth, GfxStates::s_DisplayHeight, 1, DXGI_FORMAT_R8G8B8A8_UNORM);

	// for presenting - bicubic horizontal upsample intermediate buffer
	m_HorizontalBuffer.Create(pDevice, L"Bicubic Intermediate", GfxStates::s_DisplayWidth, bufferHeight, 1, GfxStates::s_DefaultHdrColorFormat);
}

void BufferManager::DestroyRenderingBuffers()
{
	m_SceneColorBuffer.Destroy();
	m_SceneDepthBuffer.Destroy();

	m_VelocityBuffer.Destroy();

	m_PoseEffectsBuffer.Destroy();

	m_LinearDepth[0].Destroy();
	m_LinearDepth[1].Destroy();

	// shadow buffer
	m_ShadowBuffer.Destroy();

	// temporal effects
	m_TemporalColor[0].Destroy();
	m_TemporalColor[1].Destroy();

	// post effects
	m_LumaBuffer.Destroy();
	m_Histogram.Destroy();

	// bloom and tone mapping
	m_LumaLR.Destroy();
	m_aBloomUAV1[0].Destroy();
	m_aBloomUAV1[1].Destroy();
	m_aBloomUAV2[0].Destroy();
	m_aBloomUAV2[1].Destroy();
	m_aBloomUAV3[0].Destroy();
	m_aBloomUAV3[1].Destroy();
	m_aBloomUAV4[0].Destroy();
	m_aBloomUAV4[1].Destroy();
	m_aBloomUAV5[0].Destroy();
	m_aBloomUAV5[1].Destroy();

	// UI overlay
	m_OverlayBuffer.Destroy();

	// bicubic horizontal upsample intermediate buffer
	m_HorizontalBuffer.Destroy();
}

void ShaderManager::CreateFromByteCode()
{
	m_ScreenQuadVS = CD3DX12_SHADER_BYTECODE(ScreenQuadVS, sizeof(ScreenQuadVS));

	/**
		present
	*/
	m_PresentHDRPS = CD3DX12_SHADER_BYTECODE(PresentHDRPS, sizeof(PresentHDRPS));
	m_PresentSDRPS = CD3DX12_SHADER_BYTECODE(PresentSDRPS, sizeof(PresentSDRPS));
	m_MagnifyPixelsPS = CD3DX12_SHADER_BYTECODE(MagnifyPixelsPS, sizeof(MagnifyPixelsPS));

	// bicubic upsample
	m_BicubicHorizontalUpsamplePS = CD3DX12_SHADER_BYTECODE(BicubicHorizontalUpsamplePS, sizeof(BicubicHorizontalUpsamplePS));
	m_BicubicVerticalUpsamplePS = CD3DX12_SHADER_BYTECODE(BicubicVerticalUpsamplePS, sizeof(BicubicVerticalUpsamplePS));

	// sharpening upsample
	m_SharpeningUpsamplePS = CD3DX12_SHADER_BYTECODE(SharpeningUpsamplePS, sizeof(SharpeningUpsamplePS));

	// bilinear upsample
	m_BilinearUpsamplePS = CD3DX12_SHADER_BYTECODE(BilinearUpsamplePS, sizeof(BilinearUpsamplePS));

	// 
	m_BufferCopyPS = CD3DX12_SHADER_BYTECODE(BufferCopyPS, sizeof(BufferCopyPS));

	/**
		text
	*/
	m_TextVS = CD3DX12_SHADER_BYTECODE(TextVS, sizeof(TextVS));
	m_TextAntialiasPS = CD3DX12_SHADER_BYTECODE(TextAntialiasingPS, sizeof(TextAntialiasingPS));
	m_TextShadowPS = CD3DX12_SHADER_BYTECODE(TextShadowPS, sizeof(TextShadowPS));

	// basic triangle
	m_BasicTriangleVS = CD3DX12_SHADER_BYTECODE(BasicTriangleVS, sizeof(BasicTriangleVS));
	m_BasicTrianglePS = CD3DX12_SHADER_BYTECODE(BasicTrianglePS, sizeof(BasicTrianglePS));
}

// 暂时不用这个函数，无法解决#include问题 -20-1-21
void ShaderManager::CompileShadersFromFile()
{
#ifdef _DEBUG
	UINT compileFlag = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	UINT compileFlag = 0;
#endif
	
	{
		ComPtr<ID3DBlob> pBasicTriangleVS;
		ComPtr<ID3DBlob> pBasicTrianglePS;
		ASSERT_SUCCEEDED(D3DCompileFromFile(L"Shaders\\BasicTriangle.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
			"vert", "vs_5_1", compileFlag, 0, &pBasicTriangleVS, nullptr));
		ASSERT_SUCCEEDED(D3DCompileFromFile(L"Shaders\\BasicTriangle.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
			"frag", "ps_5_1", compileFlag, 0, &pBasicTrianglePS, nullptr));
		m_BasicTriangleVS = CD3DX12_SHADER_BYTECODE(pBasicTriangleVS.Get());
		m_BasicTrianglePS = CD3DX12_SHADER_BYTECODE(pBasicTrianglePS.Get());
	}

	{
		//ComPtr<ID3DBlob> pScreenQuadVS, pVSError;
		//ComPtr<ID3DBlob> pPresentHDRPS, pPSError;
		//ASSERT_SUCCEEDED(D3DCompileFromFile(L"Shaders\\ScreenQuadVS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		//	"main", "vs_5_1", compileFlag, 0, &pScreenQuadVS, &pVSError));
		//ASSERT_SUCCEEDED(D3DCompileFromFile(L"Shaders\\PresentHDRPS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		//	"main", "ps_5_1", compileFlag, 0, &pPresentHDRPS, &pPSError));

		//m_ScreenQuadVS = CD3DX12_SHADER_BYTECODE(pScreenQuadVS.Get());
		//m_PresentHDRPS = CD3DX12_SHADER_BYTECODE(pPresentHDRPS.Get());
	}
}

// CommonStates
void CommonStates::InitCommonStates(ID3D12Device* pDevice)
{
	// Sampler Desc
	{
		// SamplerLinearWrap
		SamplerLinearWrapDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		SamplerLinearWrap = SamplerLinearWrapDesc.CreateDescriptor(pDevice);

		// SamplerAnisoWrap
		SamplerAnisoWrapDesc.MaxAnisotropy = 4;
		SamplerAnisoWrap = SamplerAnisoWrapDesc.CreateDescriptor(pDevice);

		// SamplerShadow
		SamplerShadowDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
		SamplerShadowDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;		// reversed-Z
		SamplerShadowDesc.SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
		SamplerShadow = SamplerShadowDesc.CreateDescriptor(pDevice);

		// SamplerLinearClamp
		SamplerLinearClampDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		SamplerLinearClampDesc.SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
		SamplerLinearClamp = SamplerLinearClampDesc.CreateDescriptor(pDevice);

		// SamplerVolumeWrap
		SamplerVolumeWrapDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		SamplerVolumeWrap = SamplerVolumeWrapDesc.CreateDescriptor(pDevice);

		// SamplerPointClamp
		SamplerPointClampDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		SamplerPointClampDesc.SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
		SamplerPointClamp = SamplerPointClampDesc.CreateDescriptor(pDevice);

		// SamplerLinearBorder
		SamplerLinearBorderDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		SamplerLinearBorderDesc.SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE_BORDER);
		SamplerLinearBorderDesc.SetBorderColor(Color(0.0f, 0.0f, 0.0f, 0.0f));
		SamplerLinearBorder = SamplerLinearBorderDesc.CreateDescriptor(pDevice);

		// SamplerPointerBorder
		SamplerPointBorderDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		SamplerPointBorderDesc.SetTextureAddressMode(D3D12_TEXTURE_ADDRESS_MODE_BORDER);
		SamplerPointBorderDesc.SetBorderColor(Color(0.0f, 0.0f, 0.0f, 0.0f));
		SamplerPointBorder = SamplerPointBorderDesc.CreateDescriptor(pDevice);
	}

	// Rasterizer States
	{
		// default rasterizer states
		{
			RasterizerDefault.CullMode = D3D12_CULL_MODE_BACK;
			RasterizerDefault.FillMode = D3D12_FILL_MODE_SOLID;
			RasterizerDefault.FrontCounterClockwise = TRUE;
			RasterizerDefault.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
			RasterizerDefault.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
			RasterizerDefault.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
			RasterizerDefault.DepthClipEnable = TRUE;
			RasterizerDefault.ForcedSampleCount = 0;
			RasterizerDefault.MultisampleEnable = FALSE;
			RasterizerDefault.AntialiasedLineEnable = FALSE;
			RasterizerDefault.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
		}
		// RasterizerDefaultMsaa
		{
			RasterizerDefaultMsaa = RasterizerDefault;
			RasterizerDefaultMsaa.MultisampleEnable = TRUE;
		}
		// RasterizerDefaultWireframe
		{
			RasterizerDefaultWireframe = RasterizerDefault;
			RasterizerDefaultWireframe.FillMode = D3D12_FILL_MODE_WIREFRAME;
		}
		// RasterizerDefaultCw
		{
			RasterizerDefaultCw = RasterizerDefault;
			RasterizerDefaultCw.FrontCounterClockwise = FALSE;
		}
		// RasterizerDefaultCwMsaa
		{
			RasterizerDefaultCwMsaa = RasterizerDefaultCw;
			RasterizerDefaultCwMsaa.MultisampleEnable = TRUE;
		}
		// RasterizerTwoSided
		{
			RasterizerTwoSided = RasterizerDefault;
			RasterizerTwoSided.CullMode = D3D12_CULL_MODE_NONE;
		}
		// 
		{
			RasterizerTwoSidedMsaa = RasterizerTwoSided;
			RasterizerTwoSidedMsaa.MultisampleEnable = TRUE;
		}

		// shadows need their own rasterizer state so we can reserve the winding of faces 
		{
			RasterizerShadow = RasterizerDefault;
			// RasterizerShadow.CullMode = D3D12_CULL_FRONT;	// hacked here rather fixing the content
			RasterizerShadow.SlopeScaledDepthBias = -1.5f;
			RasterizerShadow.DepthBias = -100;
		}
		// RasterizerShadowTwoSided
		{
			RasterizerShadowTwoSided = RasterizerShadow;
			RasterizerShadowTwoSided.CullMode = D3D12_CULL_MODE_NONE;
		}
		//
		{
			RasterizerShadowCW = RasterizerShadow;
			RasterizerShadowCW.FrontCounterClockwise = FALSE;
		}
	}

	// Blend States
	{
		D3D12_BLEND_DESC alphaBlend = {};
		alphaBlend.IndependentBlendEnable = FALSE;
		alphaBlend.RenderTarget[0].BlendEnable = FALSE;
		alphaBlend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		alphaBlend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		alphaBlend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		alphaBlend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		alphaBlend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		alphaBlend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		alphaBlend.RenderTarget[0].RenderTargetWriteMask = 0;

		// BlendNoColorWrite
		BlendNoColorWrite = alphaBlend;

		// BlendDisable
		alphaBlend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		BlendDisable = alphaBlend;

		// blendTraditional
		alphaBlend.RenderTarget[0].BlendEnable = TRUE;
		BlendTraditional = alphaBlend;

		// BlendPremultiplied
		alphaBlend.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
		BlendPreMultiplied = alphaBlend;

		// BlendAdditive
		alphaBlend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
		BlendAdditive = alphaBlend;

		// BlendTraditionalAdditive
		alphaBlend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		BlendTraditionalAdditive = alphaBlend;
	}

	// Depth Stencil States
	{
		// DepthStateDisabled
		DepthStateDisabled.DepthEnable = FALSE;
		DepthStateDisabled.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		DepthStateDisabled.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		DepthStateDisabled.StencilEnable = FALSE;
		DepthStateDisabled.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
		DepthStateDisabled.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
		DepthStateDisabled.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		DepthStateDisabled.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		DepthStateDisabled.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		DepthStateDisabled.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		DepthStateDisabled.BackFace = DepthStateDisabled.FrontFace;

		// DepthStateReadWrite
		DepthStateReadWrite = DepthStateDisabled;
		DepthStateReadWrite.DepthEnable = TRUE;
		DepthStateReadWrite.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;	// reversed-Z
		DepthStateReadWrite.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

		// DepthStateReadOnly
		DepthStateReadOnly = DepthStateReadWrite;
		DepthStateReadOnly.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

		// DepthStateReadOnlyReversed 
		DepthStateReadOnlyReversed = DepthStateReadOnly;
		DepthStateReadOnlyReversed.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

		// DepthStateTestEqual
		DepthStateTestEqual = DepthStateReadOnly;
		DepthStateTestEqual.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
	}	

	// Command Root Signatures
	{
		DispatchIndirectCommandSignature[0].Dispatch();
		DispatchIndirectCommandSignature.Finalize(pDevice);

		DrawIndirectCommandSignature[0].Draw();
		DrawIndirectCommandSignature.Finalize(pDevice);
	}

}

void CommonStates::DestroyCommonStates()
{
	DispatchIndirectCommandSignature.Destroy();
	DrawIndirectCommandSignature.Destroy();
}
