//
// DeviceResources.h - A wrapper for the Direct3D 12 device and swapchain
//

#pragma once
#include "pch.h"

namespace MyDirectX
{
	// controls all the DirectX device resources
	class DeviceResources
	{
	public:
		static const unsigned c_AllowTearing	= 0x1;
		static const unsigned c_EnableHDR		= 0x2;

		DeviceResources(
			DXGI_FORMAT backBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM,
			DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_D24_UNORM_S8_UINT,
			UINT backBufferCount = 2,
			D3D_FEATURE_LEVEL minFeatureLevel = D3D_FEATURE_LEVEL_11_0,
			unsigned flags = 0
			);
		DeviceResources(const DeviceResources&) = delete;
		DeviceResources& operator=(const DeviceResources&) = delete;
		~DeviceResources();

		void CreateDeviceResources();
		void CreateWindowSizeDependentResources();
		bool Init(HWND window, UINT width, UINT height);

		bool WindowSizeChanged(UINT width, UINT height);
		void HandleDeviceLost();
		void Prepare(ID3D12PipelineState* pInitialState = nullptr, D3D12_RESOURCE_STATES beforeState = D3D12_RESOURCE_STATE_PRESENT);
		void Clear(DirectX::XMFLOAT4 clearColor = DirectX::XMFLOAT4(0.3f, 0.6f, 0.9f, 1.0f));
		void Present(D3D12_RESOURCE_STATES beforeState = D3D12_RESOURCE_STATE_RENDER_TARGET);
		void WaitForGpu() noexcept;

		// device accessors
		RECT GetOutputSize() const { return m_OutputSize; }

		// Direct3D accessors
		ID3D12Device*				GetD3DDevice() const			{ return m_Device.Get(); }
		IDXGISwapChain4*			GetSwapChain() const			{ return m_SwapChain.Get(); }
		IDXGIFactory4*				GetDXGIFactory() const			{ return m_Factory.Get(); }
		D3D_FEATURE_LEVEL			GetDeviceFeatureLevel() const	{ return m_D3dFeatureLevel; }
		ID3D12Resource*				GetRenderTarget() const			{ return m_BackBuffer[m_BackBufferIndex].Get(); }
		ID3D12Resource*				GetDepthStencil() const			{ return m_DepthBuffer.Get(); }
		ID3D12CommandQueue*			GetCommandQueue() const			{ return m_CmdQueue.Get(); }
		ID3D12CommandAllocator*		GetCommandAllocator() const		{ return m_CmdAllocators[m_BackBufferIndex].Get(); }
		ID3D12GraphicsCommandList*	GetCommandList() const			{ return m_CmdList.Get(); }
		DXGI_FORMAT					GetBackBufferFormat() const		{ return m_BackBufferFormat; }
		DXGI_FORMAT					GetDepthBufferFormat() const	{ return m_DepthBufferFormat; }
		D3D12_VIEWPORT				GetScreenViewport() const		{ return m_Viewport; }
		D3D12_RECT					GetScissorRect() const			{ return m_ScissorRect; }
		UINT						GetCurrentFrameIndex() const	{ return m_BackBufferIndex; }
		UINT						GetBackBufferCount() const		{ return m_BackBufferCount; }
		unsigned					GetDeviceOptions() const		{ return m_Options; }

		CD3DX12_CPU_DESCRIPTOR_HANDLE GetRenderTargetView() const
		{
			return CD3DX12_CPU_DESCRIPTOR_HANDLE(
				m_RTVDescHeap->GetCPUDescriptorHandleForHeapStart(),
				static_cast<INT>(m_BackBufferIndex),
				m_RTVDescriptorSize
			);
		}
		CD3DX12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() const
		{
			return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_DSVDescHeap->GetCPUDescriptorHandleForHeapStart());
		}

	private:
		// CreateDeviceResources
		void EnableDebugLayer();
		Microsoft::WRL::ComPtr<IDXGIFactory4> CreateFactory();
		bool CheckTearingSupport();
		Microsoft::WRL::ComPtr<IDXGIAdapter1> GetAdapter();
		Microsoft::WRL::ComPtr<ID3D12Device> CreateDevice();
		void CheckFeatureLevel();
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> CreateCommandQueue();

		void UpdateDescriptorHeap();
		void UpdateCommandAllocators();
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> CreateCommandList();

		void SetupFence();
		UINT64 Signal(UINT64& fenceValue);
		void WaitForFenceValue(UINT64 fenceValue);
		void MoveToNextFrame();

		// CreateWindowSizeDependentResources
		void UpdateSwapChain();
		void UpdateRenderTargetViews();
		void UpdateDepthStencilViews();
		void UpdateViewport();
		void UpdateScissorRect();

		static const size_t MAX_BACK_BUFFER_COUNT = 3;

		UINT m_BackBufferIndex;

		// Direct3D objects
		Microsoft::WRL::ComPtr<ID3D12Device>				m_Device;
		Microsoft::WRL::ComPtr<ID3D12CommandQueue>			m_CmdQueue;
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>	m_CmdList;
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator>		m_CmdAllocators[MAX_BACK_BUFFER_COUNT];
		D3D12_COMMAND_LIST_TYPE								m_CmdListType = D3D12_COMMAND_LIST_TYPE_DIRECT;

		// Swap chain objects
		Microsoft::WRL::ComPtr<IDXGIFactory4>				m_Factory;
		Microsoft::WRL::ComPtr<IDXGIAdapter1>				m_Adapter;
		Microsoft::WRL::ComPtr<IDXGISwapChain4>				m_SwapChain;
		Microsoft::WRL::ComPtr<ID3D12Resource>				m_BackBuffer[MAX_BACK_BUFFER_COUNT];
		Microsoft::WRL::ComPtr<ID3D12Resource>				m_DepthBuffer;

		// Direct3D rendering objects
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>		m_RTVDescHeap;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>		m_DSVDescHeap;
		UINT m_RTVDescriptorSize;

		// Presentation fence objects
		Microsoft::WRL::ComPtr<ID3D12Fence>	m_Fence;
		UINT64								m_FenceValue[MAX_BACK_BUFFER_COUNT];
		HANDLE								m_FenceEvent;

		// Direct3D properties
		DXGI_FORMAT							m_BackBufferFormat;
		DXGI_FORMAT							m_DepthBufferFormat;
		UINT								m_BackBufferCount;
		D3D_FEATURE_LEVEL					m_D3dMinFeatureLevel;

		D3D12_VIEWPORT						m_Viewport;
		D3D12_RECT							m_ScissorRect;

		// Cached device properties
		HWND								m_Window;
		UINT								m_Width = 1;
		UINT								m_Height = 1;
		RECT								m_OutputSize = { 0, 0, 1, 1 };
		D3D_FEATURE_LEVEL					m_D3dFeatureLevel;
		DWORD								m_DxgiFactoryFlags = 0;

		// DeviceResources options (flags)
		unsigned							m_Options;

	};
}

