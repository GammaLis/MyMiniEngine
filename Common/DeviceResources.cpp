#include "DeviceResources.h"

using namespace MyDirectX;

using Microsoft::WRL::ComPtr;

// constructor for DeviceResources
DeviceResources::DeviceResources(
	DXGI_FORMAT backBufferFormat,
	DXGI_FORMAT depthBufferFormat,
	UINT backBufferCount,
	D3D_FEATURE_LEVEL minFeatureLevel,
	unsigned flags) :
	m_BackBufferFormat{backBufferFormat},
	m_DepthBufferFormat{depthBufferFormat},
	m_BackBufferCount{backBufferCount},
	m_BackBufferIndex{ 0 },
	m_FenceValue{},
	m_RTVDescriptorSize{ 0 },
	m_Viewport{},
	m_ScissorRect{},
	m_D3dMinFeatureLevel{minFeatureLevel},
	m_D3dFeatureLevel{minFeatureLevel},
	m_Options{flags}
{
	if (backBufferCount > MAX_BACK_BUFFER_COUNT)
	{
		throw std::out_of_range("backBufferCount too large");
	}

	if (minFeatureLevel < D3D_FEATURE_LEVEL_11_0)
	{
		throw std::out_of_range("minFeatureLevel too low");
	}
}

// destructor for DeviceResources
DeviceResources::~DeviceResources()
{
	// ensure that the GPU is no longer referencing resources that are about to be destroyed
	WaitForGpu();
	
	CloseHandle(m_FenceEvent);
}

// configures the Direct3D device, and stores handles to it and the device context
void DeviceResources::CreateDeviceResources()
{
	EnableDebugLayer();

	m_Factory = CreateFactory();
	
	CheckTearingSupport();

	m_Adapter = GetAdapter();

	m_Device = CreateDevice();

	CheckFeatureLevel();

	m_CmdQueue = CreateCommandQueue();

	UpdateDescriptorHeap();

	UpdateCommandAllocators();

	m_CmdList = CreateCommandList();

	SetupFence();
}

// these resources need to be recreated every time the window size is changed
void DeviceResources::CreateWindowSizeDependentResources()
{
	if (!m_Window)
	{
		throw std::exception("Call Init with a valid Win32 window handle");
	}

	// wait until all previous GPU work is complete
	WaitForGpu();

	// release resources that are tied to the swap chain and update fence values
	for (UINT k = 0; k < m_BackBufferCount; ++k)
	{
		m_BackBuffer[k].Reset();
		m_FenceValue[k] = m_FenceValue[m_BackBufferIndex];
	}

	// UINT backBufferWidth = std::max<UINT>(m_Width, 1u);
	// UINT backBufferHeight = std::max<UINT>(m_Height, 1u);

	UpdateSwapChain();

	// reset the index to the current back buffer
	m_BackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

	UpdateRenderTargetViews();
	UpdateDepthStencilViews();

	UpdateViewport();
	UpdateScissorRect();
}

// This method is called when the Win32 window is created (or re-created).
bool DeviceResources::Init(HWND window, UINT width, UINT height)
{
	m_Window = window;

	m_Width = std::max<UINT>(width, 1u);
	m_Height = std::max<UINT>(height, 1u);

	m_OutputSize.left = m_OutputSize.top = 0;
	m_OutputSize.right = static_cast<LONG>(m_Width);
	m_OutputSize.bottom = static_cast<LONG>(m_Height);

	CreateDeviceResources();
	CreateWindowSizeDependentResources();

	// 保持和MyWindow::Init一致，返回bool，但是因为DeviceResources都是采用异常报错的方式，这里总是返回true
	return true;
}

// this method is called when the Win32 window changes size
bool DeviceResources::WindowSizeChanged(UINT width, UINT height)
{
	width = std::max<UINT>(width, 1u);
	height = std::max<UINT>(height, 1u);

	if (width == m_Width && height == m_Height)
	{
		return false;
	}

	m_Width = std::max<UINT>(width, 1u);
	m_Height = std::max<UINT>(height, 1u);

	m_OutputSize.left = m_OutputSize.top = 0;
	m_OutputSize.right = static_cast<LONG>(m_Width);
	m_OutputSize.bottom = static_cast<LONG>(m_Height);
	CreateWindowSizeDependentResources();
	return true;
}

// recreate all device resources and set them back to the current state
void DeviceResources::HandleDeviceLost()
{
	for (UINT k = 0; k < m_BackBufferCount; ++k)
	{
		m_CmdAllocators[k].Reset();
		m_BackBuffer[k].Reset();
	}

	m_DepthBuffer.Reset();
	m_CmdList.Reset();
	m_CmdQueue.Reset();
	m_Fence.Reset();
	m_RTVDescHeap.Reset();
	m_DSVDescHeap.Reset();
	m_SwapChain.Reset();
	m_Device.Reset();
	m_Adapter.Reset();
	m_Factory.Reset();

#if defined(_DEBUG)
	{
		ComPtr<IDXGIDebug1> dxgiDebug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
		{
			dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
		}
	}
#endif

	CreateDeviceResources();
	CreateWindowSizeDependentResources();
}

// prepare the command list and render target for rendering
void DeviceResources::Prepare(ID3D12PipelineState* pInitialState, D3D12_RESOURCE_STATES beforeState)
{
	// reset command list and allocator
	ThrowIfFailed(m_CmdAllocators[m_BackBufferIndex]->Reset());
	ThrowIfFailed(m_CmdList->Reset(m_CmdAllocators[m_BackBufferIndex].Get(), pInitialState));

	if (beforeState != D3D12_RESOURCE_STATE_RENDER_TARGET)
	{
		// transition the render target into the correct state to allow for drawing into it
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_BackBuffer[m_BackBufferIndex].Get(),
			beforeState,
			D3D12_RESOURCE_STATE_RENDER_TARGET
			);
		m_CmdList->ResourceBarrier(1, &barrier);
	}
}

void DeviceResources::Clear(DirectX::XMFLOAT4 clearColor)
{
	m_CmdList->RSSetViewports(1, &m_Viewport);
	m_CmdList->RSSetScissorRects(1, &m_ScissorRect);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(
		m_RTVDescHeap->GetCPUDescriptorHandleForHeapStart(),
		static_cast<INT>(m_BackBufferIndex),
		m_RTVDescriptorSize
	);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(m_DSVDescHeap->GetCPUDescriptorHandleForHeapStart());
	m_CmdList->ClearRenderTargetView(rtv, reinterpret_cast<const float*>(&clearColor), 0, nullptr);
	m_CmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	m_CmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
}

// present the contents of the swap chain to the screen
void DeviceResources::Present(D3D12_RESOURCE_STATES beforeState)
{
	if (beforeState != D3D12_RESOURCE_STATE_PRESENT)
	{
		// transition the render target into the correct state that allows it to be presented to the display
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_BackBuffer[m_BackBufferIndex].Get(),
			beforeState,
			D3D12_RESOURCE_STATE_PRESENT
		);
		m_CmdList->ResourceBarrier(1, &barrier);
	}

	// send the command list off to the GPU for processing
	m_CmdList->Close();
	// 1.
	ID3D12CommandList* commandLists[] =
	{
		m_CmdList.Get()
	};
	m_CmdQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
	// 2.
	// m_CmdQueue->ExecuteCommandLists(1,  CommandListCast(m_CmdList.GetAddressOf()));

	HRESULT hr;
	if (m_Options & c_AllowTearing)
	{
		// recommended to always use tearing if supported when using a sync interval of 0.
		// note this will fail if in true 'fullscreen' mode
		hr = m_SwapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
	}
	else
	{
		// The first argument instructs DXGI to block until VSync, putting the application
		// to sleep until the next VSync. This ensures we don't waste any cycles rendering
		// frames that will never be displayed to the screen.
		hr = m_SwapChain->Present(1, 0);
	}

	// if the device was reset we must completely reinitialize the renderer
	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
	{
#if defined(_DEBUG)
		wchar_t buffer[64] = {};
		swprintf_s(buffer, L"Device Lost on Present: Reason code 0x%08X\n", 
			(hr == DXGI_ERROR_DEVICE_REMOVED) ? m_Device->GetDeviceRemovedReason() : hr);
		OutputDebugString(buffer);
#endif
		HandleDeviceLost();
	}
	else
	{
		ThrowIfFailed(hr);

		MoveToNextFrame();

		if (!m_Factory->IsCurrent())
		{
			// output information is cached on the DXGI factory. if it is stale we need to create a new factory
			m_Factory = CreateFactory();
		}
	}
}

// wait for pending GPU work to complete
void DeviceResources::WaitForGpu() noexcept
{
	if (m_CmdQueue && m_Fence && m_FenceEvent)
	{
		UINT64 fenceValue = ++m_FenceValue[m_BackBufferIndex];
		if (SUCCEEDED(m_CmdQueue->Signal(m_Fence.Get(), fenceValue)))
		{
			// wait until the Signal has been processed
			if (SUCCEEDED(m_Fence->SetEventOnCompletion(fenceValue, m_FenceEvent)))
			{
				WaitForSingleObjectEx(m_FenceEvent, INFINITE, FALSE);
			}
		}
	}
}

// enable the debug layer (requires the Graphics Tools "optional feature")
// note: enabling the debug layer after device creating will invalidate the active device
void DeviceResources::EnableDebugLayer()
{
	ComPtr<ID3D12Debug> debugInterface;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugInterface.GetAddressOf()))))
	{
		debugInterface->EnableDebugLayer();
	}
	else
	{
		OutputDebugString(L"WARNING: Direct3D Debug Device is not available!\n");
	}

	ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(dxgiInfoQueue.GetAddressOf()))))
	{
		m_DxgiFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;

		dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
		dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);

		DXGI_INFO_QUEUE_MESSAGE_ID hide[] =
		{
			80	// IDXGISwapChain::GetContainingOutput: the swapchain's adapter does not control the output on which the swapchain's window resides
		};
		DXGI_INFO_QUEUE_FILTER filter = {};
		filter.DenyList.NumIDs = _countof(hide);
		filter.DenyList.pIDList = hide;
		dxgiInfoQueue->AddStorageFilterEntries(DXGI_DEBUG_DXGI, &filter);
	}
}

// create IDXGIFactory
ComPtr<IDXGIFactory4> DeviceResources::CreateFactory()
{
	ComPtr<IDXGIFactory4> dxgiFactory4;
	ThrowIfFailed(CreateDXGIFactory2(m_DxgiFactoryFlags, IID_PPV_ARGS(dxgiFactory4.ReleaseAndGetAddressOf())));
	return dxgiFactory4;
}

// determines whether tearing support is available for fullscreen borderless windows
bool DeviceResources::CheckTearingSupport()
{
	BOOL allowTearing = FALSE;

	if (m_Options & c_AllowTearing)
	{
		// rather than create the DXGIFactory5 directly, we create the DXGIFactory4 and query for DXGIFactory5.
		// this is to enable the graphics debugging tools which will not support the DXGIFactory5 until a future update
		ComPtr<IDXGIFactory5> dxgiFactory5;
		HRESULT hr = m_Factory.As(&dxgiFactory5);
		if (SUCCEEDED(hr))
		{
			hr = dxgiFactory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
		}

		if (FAILED(hr) || !allowTearing)
		{
			m_Options &= ~c_AllowTearing;
#if defined(_DEBUG)
			OutputDebugString(L"WARNING: Variable refresh rate displays not supported!");
#endif
		}

	}
	return allowTearing == TRUE;
}

// This method acquires the first available hardware adapter that supports Direct3D 12.
// If no such adapter can be found, try WARP. Otherwise throw an exception.
ComPtr<IDXGIAdapter1> DeviceResources::GetAdapter()
{
	ComPtr<IDXGIAdapter1> dxgiAdapter1;

#if defined(__dxgi1_6_h__) && defined(NTDDI_WIN10_RS4)
	ComPtr<IDXGIFactory6> dxgiFactory6;
	HRESULT hr = m_Factory.As(&dxgiFactory6);
	if (SUCCEEDED(hr))
	{
		for (UINT adapterIndex = 0;
			SUCCEEDED(dxgiFactory6->EnumAdapterByGpuPreference(
				adapterIndex,
				DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
				IID_PPV_ARGS(dxgiAdapter1.ReleaseAndGetAddressOf())));
			++adapterIndex)
		{
			DXGI_ADAPTER_DESC1 desc;
			ThrowIfFailed(dxgiAdapter1->GetDesc1(&desc));

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				// don't select the Basic Render Driver adapter
				continue;
			}

			// check to see if the adpater supports Direct3D 12, but don't create the actual device yet.
			if (SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(), m_D3dMinFeatureLevel, __uuidof(ID3D12Device), nullptr)))
			{
#if defined(_DEBUG)
				wchar_t buffer[256] = {};
				swprintf_s(buffer, L"Direct3D Adapter *(%u): VID:%04x, PID:%04x - %ls\n", adapterIndex, desc.VendorId, desc.DeviceId, desc.Description);
				OutputDebugString(buffer);
#endif
				break;
			}
		}
	}
#endif

	if (!dxgiAdapter1)
	{
		for (UINT adapterIndex = 0;
			SUCCEEDED(m_Factory->EnumAdapters1(adapterIndex, dxgiAdapter1.ReleaseAndGetAddressOf()));
			++adapterIndex)
		{
			DXGI_ADAPTER_DESC1 desc;
			ThrowIfFailed(dxgiAdapter1->GetDesc1(&desc));

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				// don't select the Basic Render Driver adatper
				continue;
			}
			// check to see if the adapter supports Direct3D 12, but don't create the actual device yet
			if (SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(), m_D3dMinFeatureLevel, _uuidof(ID3D12Device), nullptr)))
			{
#ifdef _DEBUG
				wchar_t buffer[256] = {};
				swprintf_s(buffer, L"Direct3D Adapter (%u): VID:%04X, PID:%04X - %ls\n", adapterIndex, desc.VendorId, desc.DeviceId, desc.Description);
				OutputDebugStringW(buffer);
#endif
				break;
			}
		}
	}

#if !defined(NDEBUG)
	if (!dxgiAdapter1)
	{
		// try WARP12 instead
		if (FAILED(m_Factory->EnumWarpAdapter(IID_PPV_ARGS(dxgiAdapter1.ReleaseAndGetAddressOf()))))
		{
			throw std::exception("WARP12 not available. Enable the 'Graphics Tools' optional feature!");
		}

		OutputDebugString(L"Direct3D Adapter - WARP12\n");
	}
#endif

	if (!dxgiAdapter1)
	{
		throw std::exception("No Direct3D 12 device found");
	}
	
	return dxgiAdapter1;
}

// Create the DX12 API device object.
ComPtr<ID3D12Device> DeviceResources::CreateDevice()
{
	ComPtr<ID3D12Device> d3d12Device;

	ThrowIfFailed(D3D12CreateDevice(
		m_Adapter.Get(),
		m_D3dMinFeatureLevel,
		IID_PPV_ARGS(d3d12Device.ReleaseAndGetAddressOf())
	));

	d3d12Device->SetName(L"DeviceResources");

#if !defined(NDEBUG)
	// configure debug device (if active)
	ComPtr<ID3D12InfoQueue> d3dInfoQueue;
	if (SUCCEEDED(d3d12Device.As(&d3dInfoQueue)))
	{
#if defined(_DEBUG)
		d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
#endif
		D3D12_MESSAGE_ID hide[] =
		{
			D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
			D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
			D3D12_MESSAGE_ID_EXECUTECOMMANDLISTS_WRONGSWAPCHAINBUFFERREFERENCE
		};
		D3D12_INFO_QUEUE_FILTER filter = {};
		filter.DenyList.NumIDs = _countof(hide);
		filter.DenyList.pIDList = hide;
		d3dInfoQueue->AddStorageFilterEntries(&filter);
	}
#endif

	return d3d12Device;
}

// determine maximum supported feature level for this device
void DeviceResources::CheckFeatureLevel()
{
	static const D3D_FEATURE_LEVEL s_featureLevels[] =
	{
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
	};

	D3D12_FEATURE_DATA_FEATURE_LEVELS featureLevels =
	{
		_countof(s_featureLevels), s_featureLevels, D3D_FEATURE_LEVEL_11_0
	};
	HRESULT hr = m_Device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featureLevels, sizeof(featureLevels));
	if (SUCCEEDED(hr))
	{
		m_D3dFeatureLevel = featureLevels.MaxSupportedFeatureLevel;
	}
	else
	{
		m_D3dFeatureLevel = m_D3dMinFeatureLevel;
	}
}

// create the command queue
ComPtr<ID3D12CommandQueue> DeviceResources::CreateCommandQueue()
{
	ComPtr<ID3D12CommandQueue> d3d12CmdQueue;
	
	//
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = m_CmdListType;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	ThrowIfFailed(m_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(d3d12CmdQueue.ReleaseAndGetAddressOf())));

	d3d12CmdQueue->SetName(L"DeviceResources");

	return d3d12CmdQueue;
}

// create descriptor heaps for render target views and depth stencil views
void DeviceResources::UpdateDescriptorHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvDescHeapDesc = {};
	rtvDescHeapDesc.NumDescriptors = m_BackBufferCount;
	rtvDescHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

	ThrowIfFailed(m_Device->CreateDescriptorHeap(&rtvDescHeapDesc, IID_PPV_ARGS(m_RTVDescHeap.ReleaseAndGetAddressOf())));

	m_RTVDescHeap->SetName(L"DeviceResources");

	m_RTVDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	if (m_DepthBufferFormat != DXGI_FORMAT_UNKNOWN)
	{
		D3D12_DESCRIPTOR_HEAP_DESC dsvDescHeapDesc = {};
		dsvDescHeapDesc.NumDescriptors = 1;
		dsvDescHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

		ThrowIfFailed(m_Device->CreateDescriptorHeap(&dsvDescHeapDesc, IID_PPV_ARGS(m_DSVDescHeap.ReleaseAndGetAddressOf())));

		m_DSVDescHeap->SetName(L"DeviceResources");
	}
}

// since there needs to be at least as many allocators as in-flight render frames, an allocator is 
// created for the each frame. However, since a single command list is used to record all rendering
// commands for this simple demo, only a sinlge command list is required. 

// create a command allocator for each back buffer that will be rendered to
void DeviceResources::UpdateCommandAllocators()
{
	for (UINT k = 0; k < m_BackBufferCount; ++k)
	{
		ThrowIfFailed(m_Device->CreateCommandAllocator(m_CmdListType, IID_PPV_ARGS(m_CmdAllocators[k].ReleaseAndGetAddressOf())));

		wchar_t name[25] = {};
		swprintf_s(name, L"Render Target %u", k);
		m_CmdAllocators[k]->SetName(name);
	}
}

// create a command list for recording graphics commands
ComPtr<ID3D12GraphicsCommandList> DeviceResources::CreateCommandList()
{
	ComPtr<ID3D12GraphicsCommandList> d3d12CmdList;

	ThrowIfFailed(m_Device->CreateCommandList(
		0, 
		m_CmdListType, 
		m_CmdAllocators[0].Get(), 
		nullptr, 
		IID_PPV_ARGS(d3d12CmdList.ReleaseAndGetAddressOf()
	)));
	d3d12CmdList->Close();

	d3d12CmdList->SetName(L"DeviceResources");

	return d3d12CmdList;
}

// create a fence for tracking GPU execution progress
void DeviceResources::SetupFence()
{
	ThrowIfFailed(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_Fence.ReleaseAndGetAddressOf())));

	m_Fence->SetName(L"DeviceResources");

	m_FenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
}

// singal fence
UINT64 DeviceResources::Signal(UINT64& fenceValue)
{
	// schedule a singal command in the GPU queue
	// advance the fence value to mark commands up to this fence point
	UINT64 fenceValueForSignal = ++fenceValue;
	// add an instruction to the command queue to set a new fence point. Because we are on the GPU timeline,
	// the new fence point won't be set until the GPU finished processing all the commands prior to this Signal()
	ThrowIfFailed(m_CmdQueue->Signal(m_Fence.Get(), fenceValueForSignal));

	return fenceValueForSignal;
}
// the fence is signaled after all of the commands that have been queued on the command queue have finished executing. The Signal
// function returns the fence value that the CPU thread should wait for before reusing any resources that are "in-flight" for that 
// frame on the GPU.

// wait for fence value
// Writable resource such as render targets do need to be synchronized to protect the resource from being modified by multiple queues 
// at the same time.
void DeviceResources::WaitForFenceValue(UINT64 fenceValue)
{
	// wait until the Signal has been processed
	if (m_Fence->GetCompletedValue() < fenceValue)
	{
		ThrowIfFailed(m_Fence->SetEventOnCompletion(fenceValue, m_FenceEvent));
		WaitForSingleObjectEx(m_FenceEvent, INFINITE, FALSE);
	}
}

// 这里仍有不懂？？？（2019-10-8）
// Prepare to render the next frame.
void DeviceResources::MoveToNextFrame()
{
	// Schedule a Signal command in the queue.
	const UINT64 currentFenceValue = ++m_FenceValue[m_BackBufferIndex];
	ThrowIfFailed(m_CmdQueue->Signal(m_Fence.Get(), currentFenceValue));

	// Update the back buffer index.
	m_BackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

	// If the next frame is not ready to be rendered yet, wait until it is ready.
	if (m_Fence->GetCompletedValue() < m_FenceValue[m_BackBufferIndex])
	{
		ThrowIfFailed(m_Fence->SetEventOnCompletion(m_FenceValue[m_BackBufferIndex], m_FenceEvent));
		WaitForSingleObjectEx(m_FenceEvent, INFINITE, FALSE);
	}

	// Set the fence value for the next frame.
	m_FenceValue[m_BackBufferIndex] = currentFenceValue;
}

void DeviceResources::UpdateSwapChain()
{
	// if the swap chain already exists, resize it, otherwise create one
	if (m_SwapChain)
	{
		// if the swap chain already exists, resize it
		HRESULT hr = m_SwapChain->ResizeBuffers(
			m_BackBufferCount,
			m_Width,
			m_Height,
			m_BackBufferFormat,
			(m_Options & c_AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u
		);

		if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
		{
#if defined(_DEBUG)
			wchar_t buffer[64] = {};
			swprintf_s(buffer, L"Device Lost on ResizeBuffers: Reson code 0x%08x\n",
				(hr == DXGI_ERROR_DEVICE_REMOVED) ? m_Device->GetDeviceRemovedReason() : hr);
			OutputDebugString(buffer);
#endif
			// if the device was removed for any reson, a new device and swap chain will need to be created
			HandleDeviceLost();

			// everything is set up now. Do not continue execution of this method. HandleDeviceLost will reenter this method
			// and correctly set up the new device
			return;
		}
		else
		{
			ThrowIfFailed(hr);
		}
	}
	else
	{
		// create a descriptor for the swap chain
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width = m_Width;
		swapChainDesc.Height = m_Height;
		swapChainDesc.Format = m_BackBufferFormat;
		swapChainDesc.Stereo = FALSE;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferCount = m_BackBufferCount;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		// it is recommended to always allow tearing if tearing support is available
		swapChainDesc.Flags = (m_Options & c_AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;

		DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = {};
		fsSwapChainDesc.Windowed = TRUE;

		// create a swap chain for the window
		ComPtr<IDXGISwapChain1> dxgiSwapChain1;
		ThrowIfFailed(m_Factory->CreateSwapChainForHwnd(
			m_CmdQueue.Get(),
			m_Window,
			&swapChainDesc,
			&fsSwapChainDesc,
			nullptr,
			dxgiSwapChain1.ReleaseAndGetAddressOf()
		));

		ThrowIfFailed(dxgiSwapChain1.As(&m_SwapChain));

		// this class does not support exclusive full-screen and prevents DXGI from responding to the ALT+ENTER shortcut
		ThrowIfFailed(m_Factory->MakeWindowAssociation(m_Window, DXGI_MWA_NO_ALT_ENTER));

	}
}

void DeviceResources::UpdateRenderTargetViews()
{
	// obtain the back buffers for this window which will be the final render targets
	// and create render target views for each of them
	for (UINT k = 0; k < m_BackBufferCount; ++k)
	{
		ThrowIfFailed(m_SwapChain->GetBuffer(k, IID_PPV_ARGS(m_BackBuffer[k].GetAddressOf())));

		wchar_t name[25] = {};
		swprintf_s(name, L"Render Target %u", k);
		m_BackBuffer[k]->SetName(name);

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = m_BackBufferFormat;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvDescriptor(
			m_RTVDescHeap->GetCPUDescriptorHandleForHeapStart(),
			static_cast<INT>(k),
			m_RTVDescriptorSize
		);
		m_Device->CreateRenderTargetView(m_BackBuffer[k].Get(), &rtvDesc, rtvDescriptor);
	}
}

void DeviceResources::UpdateDepthStencilViews()
{
	if (m_DepthBufferFormat != DXGI_FORMAT_UNKNOWN)
	{
		// allocate a 2-D surface as the depth/stencil buffer and create a depth/stencil view
		CD3DX12_HEAP_PROPERTIES depthHeapProperties(D3D12_HEAP_TYPE_DEFAULT);

		D3D12_RESOURCE_DESC depthStencilDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			m_DepthBufferFormat,
			m_Width,
			m_Height,
			1,	// this depht stencil view has only one texture
			1	// use a single mipmap level
		);
		depthStencilDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
		depthOptimizedClearValue.Format = m_DepthBufferFormat;
		depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
		depthOptimizedClearValue.DepthStencil.Stencil = 0;

		ThrowIfFailed(m_Device->CreateCommittedResource(
			&depthHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&depthStencilDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&depthOptimizedClearValue,
			IID_PPV_ARGS(m_DepthBuffer.ReleaseAndGetAddressOf())
		));

		m_DepthBuffer->SetName(L"Depth Stencil");

		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = m_DepthBufferFormat;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

		m_Device->CreateDepthStencilView(m_DepthBuffer.Get(), &dsvDesc, m_DSVDescHeap->GetCPUDescriptorHandleForHeapStart());
	}
}

// set the 3D rendering viewport and scissor rectangle to target the entire window
void DeviceResources::UpdateViewport()
{
	
	m_Viewport.TopLeftX = m_Viewport.TopLeftY = 0.0f;
	m_Viewport.Width = static_cast<float>(m_Width);
	m_Viewport.Height = static_cast<float>(m_Height);
	m_Viewport.MinDepth = 0.0f;
	m_Viewport.MaxDepth = 1.0f;
}

void DeviceResources::UpdateScissorRect()
{
	m_ScissorRect.left = m_ScissorRect.top = 0;
	m_ScissorRect.right = static_cast<LONG>(m_Width);
	m_ScissorRect.bottom = static_cast<LONG>(m_Height);
}
