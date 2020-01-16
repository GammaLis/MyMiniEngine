#include "Graphics.h"
#include "CommandListManager.h"
#include "CommandContext.h"

namespace MyDirectX
{    
    using Microsoft::WRL::ComPtr;

    ID3D12Device* Graphics::s_Device = nullptr;
    CommandListManager Graphics::s_CommandManager;
    ContextManager Graphics::s_ContextManager;

    DescriptorAllocator Graphics::s_DescriptorAllocator[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] =
    {
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
    };

    Graphics::Graphics(DXGI_FORMAT backBufferFormat, D3D_FEATURE_LEVEL minFeatureLevel, unsigned flags)
        : m_SwapChainFormat{ backBufferFormat },
        m_BackBufferIndex{ 0 },
        m_D3DMinFeatureLevel{ minFeatureLevel },
        m_D3DFeatureLevel{ minFeatureLevel },
        m_Options{ flags }
    {
        if (minFeatureLevel < D3D_FEATURE_LEVEL_11_0)
        {
            throw std::out_of_range("minFeatureLevel too low");
        }
    }

    void Graphics::Init(HWND hwnd, UINT width, UINT height)
    {
        ASSERT(hwnd != nullptr);

        ASSERT(m_SwapChain == nullptr, "Graphics has already been initialized");

        m_hWindow = hwnd;

        m_DisplayWidth = std::max<UINT>(width, 1);
        m_DisplayHeight = std::max<UINT>(height, 1);

        CreateDeviceResources();
        CreateWindowSizeDependentResources();
    }

    void Graphics::Resize(uint32_t newWidth, uint32_t newHeight)
    {
        ASSERT(m_SwapChain != nullptr);

        // check for invalid window dimensions
        if (newWidth == 0 || newHeight == 0)
            return;

        // check for an unneeded resize
        if (newWidth == m_DisplayWidth && newHeight == m_DisplayHeight)
            return;

        s_CommandManager.IdleGPU();

        m_DisplayWidth = newWidth;
        m_DisplayHeight = newHeight;

        DEBUGPRINT("Changing display resolution to %ux%u", newWidth, newHeight);

        // 以下，可以直接调用 CreateWindowSizeDependentResources
        // ...
        m_PreDisplayBuffer.Create(m_Device.Get(), L"PreDisplay Buffer", newWidth, newHeight, 1, m_SwapChainFormat);

        for (uint32_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
            m_BackBuffer[i].Destroy();

        UpdateSwapChain();
        //==>
        //ASSERT_SUCCEEDED(m_SwapChain->ResizeBuffers(SWAP_CHAIN_BUFFER_COUNT, newWidth, newHeight,
        //    m_SwapChainFormat, (m_Options & c_AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u));

        UpdateBackBuffers();
        //==>
        //for (uint32_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
        //{
        //    ComPtr<ID3D12Resource> displayPlane;
        //    ASSERT_SUCCEEDED(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(displayPlane.ReleaseAndGetAddressOf())));
        //    m_BackBuffer[i].CreateFromSwapChain(m_Device.Get(), L"Primary SwapChain Buffer", displayPlane.Detach());
        //}

        // m_BackBufferIndex = 0;
        m_BackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

        // TO DO
        // s_CommandManager.IdleGPU();
        // ResizeDisplayDependentBuffers(g_NativeWidth, g_NativeHeight);
    }

    void Graphics::Terminate()
    {
        s_CommandManager.IdleGPU();
        m_SwapChain->SetFullscreenState(FALSE, nullptr);
    }

    void Graphics::Shutdown()
    {
        CommandContext::DestroyAllContexts();
        s_CommandManager.Shutdown();

        // m_SwapChain->Release();  // 报错！ 不要直接调用 ComPtr->Release()
        m_SwapChain.Reset();
        // or
        // m_SwapChain = nullptr;
        
        PSO::DestroyAll();
        RootSignature::DestroyAll();
        DescriptorAllocator::DestroyAll();

        // TO DO
        // resources

        // back buffers
        for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
        {
            m_BackBuffer[i].Destroy();
        }
        m_PreDisplayBuffer.Destroy();

#if defined(_DEBUG)
        ID3D12DebugDevice* debugInterface;
        if (SUCCEEDED(m_Device->QueryInterface(&debugInterface)))
        {
            debugInterface->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
            debugInterface->Release();
        }
#endif

        // m_Device->Release();     // ComPtr->Release()报错, 不要直接调用 ComPtr->Release()
        m_Device.Reset();
        // or
        // m_Device = nullptr;
        s_Device = nullptr;
    }

    void Graphics::Present()
    {
        m_SwapChain->Present(1, 0);
        m_BackBufferIndex = (m_BackBufferIndex + 1) % SWAP_CHAIN_BUFFER_COUNT;
    }

    void Graphics::Clear(Color clearColor)
    {
        auto& context = GraphicsContext::Begin(L"Clear Color");

        context.TransitionResource(m_BackBuffer[m_BackBufferIndex], D3D12_RESOURCE_STATE_RENDER_TARGET);

        context.FlushResourceBarriers();
        context.ClearColor(m_BackBuffer[m_BackBufferIndex]);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] =
        {
            m_BackBuffer[m_BackBufferIndex].GetRTV()
        };
        context.SetRenderTargets(_countof(rtvs), rtvs);
        context.SetViewportAndScissor(0, 0, m_DisplayWidth, m_DisplayHeight);
        
        context.TransitionResource(m_BackBuffer[m_BackBufferIndex], D3D12_RESOURCE_STATE_PRESENT);

        context.Finish();
    }

    void Graphics::CreateDeviceResources()
    {
        EnableDebugLayer();

        m_Factory = CreateFactory();

        CheckTearingSupport();

        m_Adapter = GetAdapter();

        m_Device = CreateDevice();

        CheckFeatures();

        //
        s_CommandManager.Create(m_Device.Get());
    }

    void Graphics::CreateWindowSizeDependentResources()
    {
        // wait until all previous GPU work is complete
        s_CommandManager.IdleGPU();

        m_PreDisplayBuffer.Create(m_Device.Get(), L"PreDisplay Buffer", m_DisplayWidth, m_DisplayHeight, 1, m_SwapChainFormat);

        for (uint32_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
            m_BackBuffer[i].Destroy();

        UpdateSwapChain();

        UpdateBackBuffers();

        m_BackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

        // TO DO
        // create display dependent buffers
    }

    void Graphics::EnableDebugLayer()
    {
#if defined(DEBUG) || defined(_DEBUG)
        ComPtr<ID3D12Debug> debugInterface;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugInterface.GetAddressOf()))))
        {
            debugInterface->EnableDebugLayer();
        }
        else
        {
            Utility::Print(L"WARNING: Direct3D Debug Device is not available!\n");
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
#endif
    }
   
    // obtain the DXGI factory
    ComPtr<IDXGIFactory4> Graphics::CreateFactory()
    {
        ComPtr<IDXGIFactory4> dxgiFactory4;
        ASSERT_SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(dxgiFactory4.GetAddressOf())));
        return dxgiFactory4;
    }

    // determines whether tearing support is available for fullscreen borderless windows
    bool Graphics::CheckTearingSupport()
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
                Utility::Print(L"WARNING: Variable refresh rate displays not supported!");
#endif
            }

        }
        return allowTearing == TRUE;
    }

    // This method acquires the first available hardware adapter that supports Direct3D 12.
    // If no such adapter can be found, try WARP. Otherwise throw an exception.
    ComPtr<IDXGIAdapter1> Graphics::GetAdapter()
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
                ASSERT_SUCCEEDED(dxgiAdapter1->GetDesc1(&desc));

                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                {
                    // don't select the Basic Render Driver adapter
                    continue;
                }

                // check to see if the adpater supports Direct3D 12, but don't create the actual device yet.
                if (SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(), m_D3DMinFeatureLevel, __uuidof(ID3D12Device), nullptr)))
                {
#if defined(_DEBUG)
                    Utility::Printf(L"Direct3D Adapter *(%u): VID:%04x, PID:%04x - %ls\n", adapterIndex,
                        desc.VendorId, desc.DeviceId, desc.Description);
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
                ASSERT_SUCCEEDED(dxgiAdapter1->GetDesc1(&desc));

                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                {
                    // don't select the Basic Render Driver adatper
                    continue;
                }
                // check to see if the adapter supports Direct3D 12, but don't create the actual device yet
                if (SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(), m_D3DMinFeatureLevel, _uuidof(ID3D12Device), nullptr)))
                {
#ifdef _DEBUG
                    Utility::Printf(L"Direct3D Adapter *(%u): VID:%04x, PID:%04x - %ls\n", adapterIndex,
                        desc.VendorId, desc.DeviceId, desc.Description);
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

            Utility::Print(L"Direct3D Adapter - WARP12\n");
        }
#endif

        if (!dxgiAdapter1)
        {
            throw std::exception("No Direct3D 12 device found");
        }

        return dxgiAdapter1;
    }

    // Create the DX12 API device object.
    ComPtr<ID3D12Device> Graphics::CreateDevice()
    {
        ComPtr<ID3D12Device> d3d12Device;

        ASSERT_SUCCEEDED(D3D12CreateDevice(
            m_Adapter.Get(),
            m_D3DMinFeatureLevel,
            IID_PPV_ARGS(d3d12Device.GetAddressOf())
        ));

        d3d12Device->SetName(L"D3D12Device");

#if !defined(NDEBUG)
        // configure debug device (if active)
        ComPtr<ID3D12InfoQueue> d3dInfoQueue;
        if (SUCCEEDED(d3d12Device.As(&d3dInfoQueue)))   
        // (d3d12Device->QueryInterface(IID_PPV_ARGS(d3dInfoQueue.GetAddressOf())));
        {            
#if defined(_DEBUG)
            d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
            d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
#endif
            // suppress individual messages by their ID
            D3D12_MESSAGE_ID denyIds[] =
            {
                D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
                D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
                D3D12_MESSAGE_ID_EXECUTECOMMANDLISTS_WRONGSWAPCHAINBUFFERREFERENCE,

                // This occurs when there are uninitialized descriptors in a descriptor table, even when a
                // shader does not access the missing descriptors.  I find this is common when switching
                // shader permutations and not wanting to change much code to reorder resources.
                D3D12_MESSAGE_ID_INVALID_DESCRIPTOR_HANDLE,

                // Triggered when a shader does not export all color components of a render target, such as
                // when only writing RGB to an R10G10B10A2 buffer, ignoring alpha.
                D3D12_MESSAGE_ID_CREATEGRAPHICSPIPELINESTATE_PS_OUTPUT_RT_OUTPUT_MISMATCH,

                // This occurs when a descriptor table is unbound even when a shader does not access the missing
                // descriptors.  This is common with a root signature shared between disparate shaders that
                // don't all need the same types of resources.
                D3D12_MESSAGE_ID_COMMAND_LIST_DESCRIPTOR_TABLE_NOT_SET,

                // RESOURCE_BARRIER_DUPLICATE_SUBRESOURCE_TRANSITIONS
                (D3D12_MESSAGE_ID)1008,
            };
            D3D12_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.NumIDs = _countof(denyIds);
            filter.DenyList.pIDList = denyIds;
            d3dInfoQueue->AddStorageFilterEntries(&filter);
        }
#endif
        // 缓存静态pDevice
        s_Device = d3d12Device.Get();

        return d3d12Device;
    }

    void Graphics::CheckFeatures()
    {
        // feature options
        // We like to do read-modify-write operations on UAVs during post processing.  To support that, we
        // need to either have the hardware do typed UAV loads of R11G11B10_FLOAT or we need to manually
        // decode an R32_UINT representation of the same buffer.  This code determines if we get the hardware
        // load support.
        D3D12_FEATURE_DATA_D3D12_OPTIONS featureData = {};
        if (SUCCEEDED(m_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &featureData, sizeof(featureData))))
        {
            if (featureData.TypedUAVLoadAdditionalFormats)
            {
                D3D12_FEATURE_DATA_FORMAT_SUPPORT support = 
                {
                    DXGI_FORMAT_R11G11B10_FLOAT, D3D12_FORMAT_SUPPORT1_NONE, D3D12_FORMAT_SUPPORT2_NONE
                };

                if (SUCCEEDED(m_Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) &&
                    (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0)
                {
                    m_bTypedUAVLoadSupport_R11G11B10_FLOAT = true;
                }
                support.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                if (SUCCEEDED(m_Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) &&
                    (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0)
                {
                   m_bTypedUAVLoadSupport_R16G16B16A16_FLOAT = true;
                }
            }
        }

        // 这个可以略去...
        // feature level
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
            m_D3DFeatureLevel = featureLevels.MaxSupportedFeatureLevel;
        }
        else
        {
            m_D3DFeatureLevel = m_D3DMinFeatureLevel;
        }
    }

    void Graphics::UpdateSwapChain()
    {
        // if the swap chain already exists, resize it, otherwise create one
        if (m_SwapChain)
        {
            // if the swap chain already exists, resize it
            HRESULT hr = m_SwapChain->ResizeBuffers(
                SWAP_CHAIN_BUFFER_COUNT,
                m_DisplayWidth,
                m_DisplayHeight,
                m_SwapChainFormat,
                (m_Options & c_AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u
            );

            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            {
#if defined(_DEBUG)
                Utility::Printf(L"Device Lost on ResizeBuffers: Reson code 0x%08x\n", 
                    (hr == DXGI_ERROR_DEVICE_REMOVED) ? m_Device->GetDeviceRemovedReason() : hr);
#endif
                // if the device was removed for any reson, a new device and swap chain will need to be created
                HandleDeviceLost();

                // everything is set up now. Do not continue execution of this method. HandleDeviceLost will reenter this method
                // and correctly set up the new device
                return;
            }
            else
            {
                ASSERT_SUCCEEDED(hr);
            }
        }
        else
        {
            // create a descriptor for the swap chain
            DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
            swapChainDesc.Width = m_DisplayWidth;
            swapChainDesc.Height = m_DisplayHeight;
            swapChainDesc.Format = m_SwapChainFormat;
            swapChainDesc.Stereo = FALSE;
            swapChainDesc.SampleDesc.Count = 1;
            swapChainDesc.SampleDesc.Quality = 0;
            swapChainDesc.BufferCount = SWAP_CHAIN_BUFFER_COUNT;
            swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swapChainDesc.Scaling = DXGI_SCALING_STRETCH;       // DXGI_SCALING_NONE DXGI_SCALING_STRETCH
            swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;   // DXGI_SWAP_EFFECT_FLIP_DISCARD DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL
            swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
            // it is recommended to always allow tearing if tearing support is available
            swapChainDesc.Flags = (m_Options & c_AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;

            DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = {};
            fsSwapChainDesc.Windowed = TRUE;

            // create a swap chain for the window
            ComPtr<IDXGISwapChain1> dxgiSwapChain1;
            ASSERT_SUCCEEDED(m_Factory->CreateSwapChainForHwnd(
                s_CommandManager.GetCommandQueue(),
                m_hWindow,
                &swapChainDesc,
                &fsSwapChainDesc,
                nullptr,
                dxgiSwapChain1.ReleaseAndGetAddressOf()
            ));

            ASSERT_SUCCEEDED(dxgiSwapChain1.As(&m_SwapChain));

            // this class does not support exclusive full-screen and prevents DXGI from responding to the ALT+ENTER shortcut
            ASSERT_SUCCEEDED(m_Factory->MakeWindowAssociation(m_hWindow, DXGI_MWA_NO_ALT_ENTER));

            CheckColorSpace();
        }
    }

    void Graphics::CheckColorSpace()
    {
#if defined(NTDDI_WIN10_RS2) && (NTDDI_VERSION >= NTDDI_WIN10_RS2)
        IDXGISwapChain4* swapChain4 = m_SwapChain.Get();
        ComPtr<IDXGIOutput> output;
        ComPtr<IDXGIOutput6> output6;
        DXGI_OUTPUT_DESC1 outputDesc;
        UINT colorSpaceSupport;

        //  query for ST.2084 on the display and set the color space accordingly
        if (SUCCEEDED(swapChain4->GetContainingOutput(&output)) &&
            SUCCEEDED(output.As(&output6)) &&
            SUCCEEDED(output6->GetDesc1(&outputDesc)) &&
            outputDesc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 &&
            SUCCEEDED(swapChain4->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, &colorSpaceSupport)) &&
            (colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) &&
            SUCCEEDED(swapChain4->SetColorSpace1(DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020)))
        {
            m_bEnableHDROutput = true;
        }
#endif
    }

    void Graphics::UpdateBackBuffers()
    {
        for (uint32_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
        {
            ComPtr<ID3D12Resource> displayPlane;
            ASSERT_SUCCEEDED(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(displayPlane.ReleaseAndGetAddressOf())));
            m_BackBuffer[i].CreateFromSwapChain(m_Device.Get(), L"Primary SwapChain Buffer", displayPlane.Detach());
            m_BackBuffer[i].SetClearColor(Color(.2f, .4f, .4f));
        }
    }

    void Graphics::HandleDeviceLost()
    {
        Shutdown();

        CreateDeviceResources();
        CreateWindowSizeDependentResources();
    }

    void Graphics::InitRootSignatures()
    {
        // basic triangle
        // m_BasicTriangleRS.Reset()
    }

    void Graphics::InitPSOs()
    {

    }
}