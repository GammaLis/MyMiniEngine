#include "CommonCompute.h"
#include "CommandListManager.h"
#include "CommandContext.h"
#include "TextureManager.h"

namespace MyDirectX
{
    using Microsoft::WRL::ComPtr;

    // managers
    ID3D12Device* CommonCompute::s_Device = nullptr;
    CommandListManager CommonCompute::s_CommandManager;
    ContextManager CommonCompute::s_ContextManager;

    // cached resources and states
    TextureManager CommonCompute::s_TextureManager;

    DescriptorAllocator CommonCompute::s_DescriptorAllocator[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] =
    {
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
    };

	CommonCompute::CommonCompute(D3D_FEATURE_LEVEL minFeatureLevel, unsigned flags)
        : m_D3DMinFeatureLevel{ minFeatureLevel }, m_D3DFeatureLevel{minFeatureLevel}, m_Options{flags}
	{
        if (minFeatureLevel < D3D_FEATURE_LEVEL_11_0)
        {
            throw std::out_of_range("minFeatureLevel too low");
        }
	}

    void CommonCompute::Init()
    {
        CreateDeviceResources();

        //
        CustomInit();
    }

    void CommonCompute::Terminate()
    {
        s_CommandManager.IdleGPU();
    }

    void CommonCompute::Shutdown()
    {
        CommandContext::DestroyAllContexts();
        s_CommandManager.Shutdown();

        PSO::DestroyAll();
        RootSignature::DestroyAll();
        DescriptorAllocator::DestroyAll();

        // resources
        s_TextureManager.Shutdown();

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
        s_Device = nullptr;
    }

    void CommonCompute::CreateDeviceResources()
    {
        EnableDebugLayer();

        m_Factory = CreateFactory();

        m_Adapter = GetAdapter();

        m_Device = CreateDevice();

        CheckFeatures();

        //
        s_CommandManager.Create(m_Device.Get());
    }

    void CommonCompute::EnableDebugLayer()
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

    ComPtr<IDXGIFactory4> CommonCompute::CreateFactory()
    {
        ComPtr<IDXGIFactory4> dxgiFactory4;
        ASSERT_SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(dxgiFactory4.GetAddressOf())));
        return dxgiFactory4;
    }

    ComPtr<IDXGIAdapter1> CommonCompute::GetAdapter()
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

    ComPtr<ID3D12Device> CommonCompute::CreateDevice()
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
                // 忽略 ClearRenderTargetView clearValue 与设置值不一的warning
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

    void CommonCompute::CheckFeatures()
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
                    GfxStates::s_bTypedUAVLoadSupport_R11G11B10_FLOAT = true;
                }
                support.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                if (SUCCEEDED(m_Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) &&
                    (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0)
                {
                    GfxStates::s_bTypedUAVLoadSupport_R16G16B16A16_FLOAT = true;
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

    void CommonCompute::HandleDeviceLost()
    {
        Shutdown();

        CreateDeviceResources();
    }

    void CommonCompute::CustomInit()
    {
        InitRootSignatures();
        InitPSOs();
    }

    void CommonCompute::InitRootSignatures()
    {
        m_EmptyRS.Finalize(m_Device.Get(), L"EmptyRootSignature");
    }

    void CommonCompute::InitPSOs()
    {

    }
}
