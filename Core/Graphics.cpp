#include "Graphics.h"
#include "CommandListManager.h"
#include "CommandContext.h"
#include "TextureManager.h"
#include "Effect.h"


namespace MyDirectX
{    
    using Microsoft::WRL::ComPtr;
    
    // managers
    ID3D12Device* Graphics::s_Device = nullptr;
    CommandListManager Graphics::s_CommandManager;
    ContextManager Graphics::s_ContextManager;

    // cached resources and states
    ShaderManager Graphics::s_ShaderManager;
    BufferManager Graphics::s_BufferManager;
    TextureManager Graphics::s_TextureManager;
    CommonStates Graphics::s_CommonStates;

    DescriptorAllocator Graphics::s_DescriptorAllocator[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] =
    {
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
    };

    Graphics::Graphics(DXGI_FORMAT backBufferFormat, D3D_FEATURE_LEVEL minFeatureLevel, 
        unsigned flags, Resolutions nativeRes)
        : m_SwapChainFormat{ backBufferFormat },
        m_BackBufferIndex{ 0 },
        m_D3DMinFeatureLevel{ minFeatureLevel },
        m_D3DFeatureLevel{ minFeatureLevel },
        m_Options{ flags },
        m_CurNativeRes{ nativeRes }
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

        // 静态缓存 width height
        GfxStates::s_DisplayWidth = m_DisplayWidth;
        GfxStates::s_DisplayHeight = m_DisplayHeight;

        CreateDeviceResources();
        CreateWindowSizeDependentResources();

        //
        s_ShaderManager.CreateFromByteCode();
        s_CommonStates.InitCommonStates(m_Device.Get());
        GfxStates::SetNativeResolution(m_Device.Get(), m_CurNativeRes);
        CustomInit();

        // 考虑放在 Graphics里 还是 IGameApp里 -20-1-27
        // 目前 感觉放在 Graphics里 更加合适
        Effect::Init(m_Device.Get());
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

        // 静态缓存 width height
        GfxStates::s_DisplayWidth = m_DisplayWidth;
        GfxStates::s_DisplayHeight = m_DisplayHeight;

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

        // 调整 显示尺寸相关buffer大小
        s_CommandManager.IdleGPU();
        s_BufferManager.ResizeDisplayDependentBuffers(m_Device.Get(), 
            GfxStates::s_NativeWidth, GfxStates::s_NativeHeight);
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

        // resources
        s_CommonStates.DestroyCommonStates();
        s_BufferManager.DestroyRenderingBuffers();
        s_TextureManager.Shutdown();

        // effects
        Effect::Shutdown();

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
        // 默认开启HDR
        // m_bEnableHDROutput = true;
        if (m_bEnableHDROutput)
            PreparePresentHDR();
        else
            PreparePresentLDR();

        m_BackBufferIndex = (m_BackBufferIndex + 1) % SWAP_CHAIN_BUFFER_COUNT;

        m_SwapChain->Present(1, 0);

        ++m_FrameIndex;

        // 这是MS MiniEinge做法，移到ModelViewer::Update -20-2-22
        // Effect::s_TemporalAA.Update(m_FrameIndex);

        // 可以动态改变 NativeResolution
        GfxStates::SetNativeResolution(m_Device.Get(), m_CurNativeRes);
    }

    void Graphics::Clear(Color clearColor)
    {
        auto& context = GraphicsContext::Begin(L"Clear Color");

        auto& colorBuffer = s_BufferManager.m_SceneColorBuffer;
        context.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] =
        {
            colorBuffer.GetRTV()
        };
        context.SetRenderTargets(_countof(rtvs), rtvs);
        context.SetViewportAndScissor(0, 0, m_DisplayWidth, m_DisplayHeight);

        context.ClearColor(colorBuffer);

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
            // suppress whole categories of messages
            //D3D12_MESSAGE_CATEGORY categories[] = {  };

            // suppress messages based on their severity level（严重等级）
            D3D12_MESSAGE_SEVERITY severities[] =
            {
                D3D12_MESSAGE_SEVERITY_INFO
            };

            // suppress individual messages by their ID
            D3D12_MESSAGE_ID denyIds[] =
            {
                D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
                D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
                D3D12_MESSAGE_ID_EXECUTECOMMANDLISTS_WRONGSWAPCHAINBUFFERREFERENCE,

                // 3dgep.com
                // this occurs when a render target is cleared using a clear color that is not the optimized color
                // specified during resource creation.
                D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,

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
            //filter.DenyList.NumCategories = _countof(categories);
            //filter.DenyList.pCategoryList = categories;
            filter.DenyList.NumSeverities = _countof(severities);
            filter.DenyList.pSeverityList = severities;
            filter.DenyList.NumIDs = _countof(denyIds);
            filter.DenyList.pIDList = denyIds;
            d3dInfoQueue->AddStorageFilterEntries(&filter);

            // d3dInfoQueue->PushStorageFilter(&filter);
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
            // set clear color, for debugging
            m_BackBuffer[i].SetClearColor(Color(.2f, .4f, .4f));
        }
    }

    void Graphics::HandleDeviceLost()
    {
        Shutdown();

        CreateDeviceResources();
        CreateWindowSizeDependentResources();
    }

    // init
    void Graphics::CustomInit()
    {
        InitRootSignatures();
        InitPSOs();
    }

    void Graphics::InitRootSignatures()
    {
        m_EmptyRS.Finalize(m_Device.Get(), L"EmptyRootSignature");

        // present RootSignature
        m_PresentRS.Reset(4, 2);
        m_PresentRS[0].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 2);    // SceanColorBuffer, OveralyBuffer
        m_PresentRS[1].InitAsConstants(0, 6, D3D12_SHADER_VISIBILITY_ALL);
        m_PresentRS[2].InitAsBufferSRV(2, D3D12_SHADER_VISIBILITY_PIXEL);
        m_PresentRS[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
        m_PresentRS.InitStaticSampler(0, s_CommonStates.SamplerLinearClampDesc);
        m_PresentRS.InitStaticSampler(1, s_CommonStates.SamplerPointClampDesc);
        m_PresentRS.Finalize(m_Device.Get(), L"Present");
    }

    void Graphics::InitPSOs()
    {
        // PresentSDRPSO
        m_PresentSDRPSO.SetRootSignature(m_PresentRS);
        m_PresentSDRPSO.SetRasterizerState(s_CommonStates.RasterizerDefault);
        m_PresentSDRPSO.SetBlendState(s_CommonStates.BlendDisable);
        m_PresentSDRPSO.SetDepthStencilState(s_CommonStates.DepthStateDisabled);
        m_PresentSDRPSO.SetSampleMask(0xFFFFFFFF);
        m_PresentSDRPSO.SetInputLayout(0, nullptr);
        m_PresentSDRPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        m_PresentSDRPSO.SetVertexShader(s_ShaderManager.m_ScreenQuadVS);
        m_PresentSDRPSO.SetPixelShader(s_ShaderManager.m_PresentSDRPS);
        m_PresentSDRPSO.SetRenderTargetFormat(m_SwapChainFormat, DXGI_FORMAT_UNKNOWN);
        m_PresentSDRPSO.Finalize(m_Device.Get());

        auto CreatePSO = [&](GraphicsPSO &pso, const CD3DX12_SHADER_BYTECODE &pixelShader)
        {
            pso = m_PresentSDRPSO;
            pso.SetPixelShader(pixelShader);
            pso.Finalize(m_Device.Get());
        };
        CreatePSO(m_MagnifyPixelsPSO, s_ShaderManager.m_MagnifyPixelsPS);
        CreatePSO(m_BilinearUpsamplePSO, s_ShaderManager.m_BilinearUpsamplePS);
        CreatePSO(m_BicubicVerticalUpsamplePSO, s_ShaderManager.m_BicubicVerticalUpsamplePS);
        CreatePSO(m_SharpeningUpsamplePSO, s_ShaderManager.m_SharpeningUpsamplePS);

        // BlendUIPSO
        m_BlendUIPSO = m_PresentSDRPSO;
        m_BlendUIPSO.SetRasterizerState(s_CommonStates.RasterizerTwoSided);
        m_BlendUIPSO.SetBlendState(s_CommonStates.BlendPreMultiplied);
        m_BlendUIPSO.SetPixelShader(s_ShaderManager.m_BufferCopyPS);
        m_BlendUIPSO.Finalize(m_Device.Get());

        // BicubicHorizontalUpsamplePSO
        m_BicubicHorizontalUpsamplePSO = m_PresentSDRPSO;
        m_BicubicHorizontalUpsamplePSO.SetPixelShader(s_ShaderManager.m_BicubicHorizontalUpsamplePS);
        m_BicubicHorizontalUpsamplePSO.SetRenderTargetFormat(GfxStates::s_DefaultHdrColorFormat, DXGI_FORMAT_UNKNOWN);
        m_BicubicHorizontalUpsamplePSO.Finalize(m_Device.Get());

        // PresentHDRPSO
        m_PresentHDRPSO = m_PresentSDRPSO;
        m_PresentHDRPSO.SetPixelShader(s_ShaderManager.m_PresentHDRPS);
        DXGI_FORMAT swapChainFormats[2] = { m_SwapChainFormat, m_SwapChainFormat };
        m_PresentHDRPSO.SetRenderTargetFormats(2, swapChainFormats, DXGI_FORMAT_UNKNOWN);
        m_PresentHDRPSO.Finalize(m_Device.Get());

    }

    void Graphics::PreparePresentHDR()
    {
        GraphicsContext& context = GraphicsContext::Begin(L"Present");

        // we're going to be reading these buffers to write to the swap chain buffer(s)
        auto& backBuffer = m_BackBuffer[m_BackBufferIndex];
        context.TransitionResource(s_BufferManager.m_SceneColorBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        context.TransitionResource(s_BufferManager.m_OverlayBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        context.TransitionResource(m_BackBuffer[m_BackBufferIndex], D3D12_RESOURCE_STATE_RENDER_TARGET);

        context.SetRootSignature(m_PresentRS);
        context.SetPipelineState(m_PresentHDRPSO);
        context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        context.SetDynamicDescriptor(0, 0, s_BufferManager.m_SceneColorBuffer.GetSRV());
        context.SetDynamicDescriptor(1, 0, s_BufferManager.m_OverlayBuffer.GetSRV());
        
        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] =
        {
            backBuffer.GetRTV()
        };
        context.SetRenderTargets(_countof(rtvs), rtvs);
        // debug
        {
            static bool b = true;
            if (b == false)
            {
                GfxStates::s_NativeWidth /= 2;
                GfxStates::s_NativeHeight /= 2;
                b = true;
            }
        }
        // end debug
        context.SetViewportAndScissor(0, 0, GfxStates::s_NativeWidth, GfxStates::s_NativeHeight);
        // Note: -20-1-21
        // MS MiniEngine采用NativeWidth和NativeHeight
        // 但是 如果 DisplayWidth和DisplayHeight与之不同，显示部分图像（截断）（默认Display W和H更小），
        // 这样 shader里面应该采用 采样 （Sample）而非 取值（Tex[xy]）
        // context.SetViewportAndScissor(0, 0, GfxStates::s_DisplayWidth, GfxStates::s_DisplayHeight);
        struct Constants
        {
            float RcpDstWidth;
            float RcpDstHeight;
            float PaperWhite;
            float MaxBrightness;
            int32_t DebugMode;
        };
        Constants consts = {
            1.f / GfxStates::s_NativeWidth, 1.f / GfxStates::s_NativeHeight,
            (float)GfxStates::s_HDRPaperWhite, (float)GfxStates::s_MaxDisplayLuminance, 0
        };
        context.SetConstantArray(1, sizeof(Constants) / 4, (float*)&consts);
        context.Draw(3);

        context.TransitionResource(backBuffer, D3D12_RESOURCE_STATE_PRESENT);

        // close the final context to be executed before frame present
        context.Finish();
    }

    void Graphics::CompositeOverlays(GraphicsContext& context)
    {
        // blend (or write) the UI overlay
        auto& overlayBuffer = s_BufferManager.m_OverlayBuffer;
        context.TransitionResource(overlayBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        context.SetDynamicDescriptor(0, 0, overlayBuffer.GetSRV());

        // debug
        // 显示 Default字体纹理 （调试使用） -20-1-28
        // auto& textRenderer = Effect::s_TextRenderer;
        // context.SetDynamicDescriptor(0, 0, textRenderer.GetDefaultFontTexture());
        
        // 显示 LinearDepth [n, f] / f
        // auto& linearDepth = s_BufferManager.m_LinearDepth[m_FrameIndex % 2];
        // context.SetDynamicDescriptor(0, 0, linearDepth.GetSRV());

        // debug end

        context.SetPipelineState(m_BlendUIPSO);
        context.SetConstants(1, 1.0f / GfxStates::s_NativeWidth, 1.0f / GfxStates::s_NativeHeight);
        context.Draw(3);
    }

    void Graphics::PreparePresentLDR()
    {
        GraphicsContext &context = GraphicsContext::Begin(L"Present");

        // we're going to be reading these buffers to write to the swap chain buffer(s)
        auto& backBuffer = m_BackBuffer[m_BackBufferIndex];
        auto& colorBuffer = s_BufferManager.m_SceneColorBuffer;
        context.TransitionResource(colorBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        context.SetRootSignature(m_PresentRS);
        context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // copy (and convert) the LDR buffer to the back buffer
        context.SetDynamicDescriptor(0, 0, colorBuffer.GetSRV());

        ColorBuffer& upsampleDest = (GfxStates::s_DebugZoom == DebugZoom::Off ?
            backBuffer : m_PreDisplayBuffer);

        if (GfxStates::s_NativeWidth == GfxStates::s_DisplayWidth &&
            GfxStates::s_NativeHeight == GfxStates::s_DisplayHeight)
        {
            context.SetPipelineState(m_PresentSDRPSO);
            context.TransitionResource(upsampleDest, D3D12_RESOURCE_STATE_RENDER_TARGET);
            context.SetRenderTarget(upsampleDest.GetRTV());
            context.SetViewportAndScissor(0, 0, GfxStates::s_NativeWidth, GfxStates::s_NativeHeight);
            context.Draw(3);
        }
        else if (GfxStates::s_UpsampleFilter == UpsampleFilter::kBicubic)
        {
            // horizontal pass
            context.TransitionResource(s_BufferManager.m_HorizontalBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
            context.SetRenderTarget(s_BufferManager.m_HorizontalBuffer.GetRTV());
            context.SetViewportAndScissor(0, 0, GfxStates::s_DisplayWidth, GfxStates::s_NativeHeight);
            context.SetPipelineState(m_BicubicHorizontalUpsamplePSO);
            context.SetConstants(1, GfxStates::s_NativeWidth, GfxStates::s_NativeHeight, (float)GfxStates::s_BicubicUpsampleWeight);
            context.Draw(3);

            // vertical pass
            context.TransitionResource(s_BufferManager.m_HorizontalBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            context.TransitionResource(upsampleDest, D3D12_RESOURCE_STATE_RENDER_TARGET);
            context.SetRenderTarget(upsampleDest.GetRTV());
            context.SetViewportAndScissor(0, 0, GfxStates::s_DisplayWidth, GfxStates::s_DisplayHeight);
            context.SetPipelineState(m_BicubicVerticalUpsamplePSO);
            context.SetDynamicDescriptor(0, 0, s_BufferManager.m_HorizontalBuffer.GetSRV());
            context.SetConstants(1, GfxStates::s_DisplayWidth, GfxStates::s_NativeHeight, (float)GfxStates::s_BicubicUpsampleWeight);
            context.Draw(3);
        }
        else if (GfxStates::s_UpsampleFilter == UpsampleFilter::kSharpening)
        {
            context.TransitionResource(upsampleDest, D3D12_RESOURCE_STATE_RENDER_TARGET);
            context.SetRenderTarget(upsampleDest.GetRTV());
            context.SetViewportAndScissor(0, 0, GfxStates::s_DisplayWidth, GfxStates::s_DisplayHeight);
            context.SetPipelineState(m_SharpeningUpsamplePSO);
            
            float texelWidth = 1.0f / GfxStates::s_NativeWidth;
            float texelHeight = 1.0f / GfxStates::s_NativeHeight;
            float X = Math::Cos((float)GfxStates::s_SharpeningRotation * Math::DegToRad) * (float)GfxStates::s_SharpeningSpread;
            float Y = Math::Sin((float)GfxStates::s_SharpeningRotation * Math::DegToRad) * (float)GfxStates::s_SharpeningSpread;
            const float WA = (float)GfxStates::s_SharpeningStrength;
            const float WB = 1.0f + 4.0f * WA;
            // offset0 - (cosTheta * x + sinTheta * y)
            // offset1 - (sinTheta * x - cosTheta * y)
            // 倾斜 theta 角度的2个互相垂直的向量
            float Constants[] = { X * texelWidth, Y * texelHeight, Y * texelWidth, -X * texelHeight, WA, WB };
            context.SetConstantArray(1, _countof(Constants), Constants);
            context.Draw(3);
        }
        else if (GfxStates::s_UpsampleFilter == UpsampleFilter::kBilinear)
        {
            context.TransitionResource(upsampleDest, D3D12_RESOURCE_STATE_RENDER_TARGET);
            context.SetRenderTarget(upsampleDest.GetRTV());
            context.SetViewportAndScissor(0, 0, GfxStates::s_DisplayWidth, GfxStates::s_DisplayHeight);
            context.SetPipelineState(m_BilinearUpsamplePSO);
            context.Draw(3);
        }

        if (GfxStates::s_DebugZoom != DebugZoom::Off)
        {
            context.TransitionResource(m_PreDisplayBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            context.TransitionResource(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
            context.SetRenderTarget(backBuffer.GetRTV());
            context.SetViewportAndScissor(0, 0, GfxStates::s_DisplayWidth, GfxStates::s_DisplayWidth);
            context.SetPipelineState(m_MagnifyPixelsPSO);
            context.SetDynamicDescriptor(0, 0, m_PreDisplayBuffer.GetSRV());
            context.SetConstants(1, 1.f / ((int)GfxStates::s_DebugZoom + 1.f));
            context.Draw(3);
        }

        CompositeOverlays(context);

        context.TransitionResource(backBuffer, D3D12_RESOURCE_STATE_PRESENT);

        // close the final context to be executed before frame present
        context.Finish();
    }
}