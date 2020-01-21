#pragma once
#include "pch.h"
#include "Color.h"
#include "GfxCommon.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "DescriptorHeap.h"
#include "ColorBuffer.h"

#define SWAP_CHAIN_BUFFER_COUNT 3

namespace MyDirectX
{
	class CommandListManager;
	class ContextManager;
	class CommandContext;

	class ShaderManager;
	class ResourceManager;
	
	class Graphics
	{
	public:
		static const uint32_t c_AllowTearing = 0x1;
		static const uint32_t c_EnableHDR = 0x2;

		Graphics(
			DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R10G10B10A2_UNORM,
			D3D_FEATURE_LEVEL minFeatureLevel = D3D_FEATURE_LEVEL_11_0,
			unsigned flags = 0,
			Resolutions nativeRes = Resolutions::k720p);
		Graphics(const Graphics&) = delete;
		Graphics& operator=(const Graphics&) = delete;
		~Graphics() {  }

		void Init(HWND hwnd, UINT width, UINT height);
		void Resize(uint32_t newWidth, uint32_t newHeight);
		void Terminate();
		void Shutdown();
		void Present();

		void Clear(Color clearColor = Color(.2f, .4f, .4f));

		ColorBuffer& GetRenderTarget() { return m_BackBuffer[m_BackBufferIndex]; }
		const ColorBuffer &GetRenderTarget() const { return m_BackBuffer[m_BackBufferIndex]; }

		// static members
		static ID3D12Device* s_Device;
		static CommandListManager s_CommandManager;
		static ContextManager s_ContextManager;

		static ShaderManager s_ShaderManager;
		static ResourceManager s_ResourceManager;
		static CommonStates s_CommonStates;

		inline static UINT GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type)
		{
			return s_Device->GetDescriptorHandleIncrementSize(type);
		}

		// 
		static DescriptorAllocator s_DescriptorAllocator[];

		inline static D3D12_CPU_DESCRIPTOR_HANDLE AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count = 1)
		{
			return s_DescriptorAllocator[type].Allocate(s_Device, count);
		}

	private:
		void CreateDeviceResources();
		void CreateWindowSizeDependentResources();

		// CreateDeviceResources
		void EnableDebugLayer();
		Microsoft::WRL::ComPtr<IDXGIFactory4> CreateFactory();
		bool CheckTearingSupport();
		Microsoft::WRL::ComPtr<IDXGIAdapter1> GetAdapter();
		Microsoft::WRL::ComPtr<ID3D12Device> CreateDevice();
		void CheckFeatures();

		// CreateWindowSizeDependentResources
		void UpdateSwapChain();
		void CheckColorSpace();
		void UpdateBackBuffers();

		void HandleDeviceLost();

		void CustomInit();

		// init RootSignatures
		void InitRootSignatures();

		// init PSOs
		void InitPSOs();

		// prepare present
		void PreparePresentHDR();
		void PreparePresentLDR();

		// class members
		
		// Direct3D objects
		Microsoft::WRL::ComPtr<IDXGIFactory4> m_Factory;
		Microsoft::WRL::ComPtr<IDXGIAdapter1> m_Adapter;
		Microsoft::WRL::ComPtr<ID3D12Device> m_Device;
		Microsoft::WRL::ComPtr<IDXGISwapChain4> m_SwapChain;

		// PSOs
		RootSignature m_EmptyRS;
		RootSignature m_PresentRS;
		GraphicsPSO m_PresentSDRPSO;
		GraphicsPSO m_PresentHDRPSO;
		GraphicsPSO m_MagnifyPixelsPSO;
		// upsample 
		GraphicsPSO m_BilinearUpsamplePSO;
		GraphicsPSO m_BicubicHorizontalUpsamplePSO;
		GraphicsPSO m_BicubicVerticalUpsamplePSO;
		GraphicsPSO m_SharpeningUpsamplePSO;

		RootSignature m_GenerateMipsRS;
		ComputePSO m_GenerateMipsLinearPSO[4];
		ComputePSO m_GenerateMipsGammaPSO[4];

		// resources
		ColorBuffer m_PreDisplayBuffer;
		ColorBuffer m_BackBuffer[SWAP_CHAIN_BUFFER_COUNT];
		UINT m_BackBufferIndex = 0;

		// features
		D3D_FEATURE_LEVEL m_D3DMinFeatureLevel;
		D3D_FEATURE_LEVEL m_D3DFeatureLevel;
		bool m_bTypedUAVLoadSupport_R11G11B10_FLOAT = false;
		bool m_bTypedUAVLoadSupport_R16G16B16A16_FLOAT = false;
		bool m_bEnableHDROutput = false;
		bool m_bEnableVSync = false;
		
		// settings
		uint32_t m_DisplayWidth = 1280;
		uint32_t m_DisplayHeight = 720;
		DXGI_FORMAT m_SwapChainFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
		DWORD m_DxgiFactoryFlags = 0;

		Resolutions m_CurNativeRes;



		HWND m_hWindow;

		// options
		uint32_t m_Options = 0;
	};

}
