#pragma once
#include "pch.h"
#include "Color.h"
#include "GfxCommon.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "DescriptorHeap.h"

#define SWAP_CHAIN_BUFFER_COUNT 3

namespace MyDirectX
{
	class CommandListManager;
	class ContextManager;
	class CommandContext;

	class TextureManager;

	class CommonCompute
	{
	public:
		static const uint32_t c_AllowTearing = 0x1;
		static const uint32_t c_EnableHDR = 0x2;

		CommonCompute(D3D_FEATURE_LEVEL minFeatureLevel = D3D_FEATURE_LEVEL_11_0, unsigned flags = 0);
		CommonCompute(const CommonCompute&) = delete;
		CommonCompute& operator=(const CommonCompute&) = delete;
		~CommonCompute() {  }

		void Init();
		void Terminate();
		void Shutdown();

		// static members
		static ID3D12Device* s_Device;
		static CommandListManager s_CommandManager;
		static ContextManager s_ContextManager;

		static TextureManager s_TextureManager;

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

		// CreateDeviceResources
		void EnableDebugLayer();
		Microsoft::WRL::ComPtr<IDXGIFactory4> CreateFactory();

		Microsoft::WRL::ComPtr<IDXGIAdapter1> GetAdapter();
		Microsoft::WRL::ComPtr<ID3D12Device> CreateDevice();
		void CheckFeatures();

		void HandleDeviceLost();

		void CustomInit();

		// init RootSignatures
		void InitRootSignatures();

		// init PSOs
		void InitPSOs();

		// class members

		// Direct3D objects
		Microsoft::WRL::ComPtr<IDXGIFactory4> m_Factory;
		Microsoft::WRL::ComPtr<IDXGIAdapter1> m_Adapter;
		Microsoft::WRL::ComPtr<ID3D12Device> m_Device;

		// PSOs
		RootSignature m_EmptyRS;

		RootSignature m_GenerateMipsRS;
		ComputePSO m_GenerateMipsLinearPSO[4];
		ComputePSO m_GenerateMipsGammaPSO[4];

		// features
		D3D_FEATURE_LEVEL m_D3DMinFeatureLevel;
		D3D_FEATURE_LEVEL m_D3DFeatureLevel;

		DWORD m_DxgiFactoryFlags = 0;

		// options
		uint32_t m_Options = 0;
	};

}
