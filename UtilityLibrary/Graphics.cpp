#include "LibraryHeader.h"

#include "CmdListMngr.h"
#include "DescriptorHeap.h"
#include "LinearAllocator.h"
#include "Graphics.h"
#include "TextRenderer.h"
#include "DX12Framework.h"

namespace
{
    // Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
    // If no such adapter can be found, *ppAdapter will be set to nullptr.
    _Use_decl_annotations_
        void GetHardwareAdapter( IDXGIFactory4* pFactory, IDXGIAdapter1** ppAdapter )
    {
        ComPtr<IDXGIAdapter1> adapter;
        *ppAdapter = nullptr;
        bool found = false;

        for ( UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1( adapterIndex, &adapter ); ++adapterIndex )
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1( &desc );

            if ( desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE )
            {
                // Don't select the Basic Render Driver adapter.
                // If you want a software adapter, pass in "/warp" on the command line.
                PRINTINFO( L"D3D12-capable hardware found:  %s (%u MB)", desc.Description, desc.DedicatedVideoMemory >> 20 );
                continue;
            }

            // Check to see if the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if ( SUCCEEDED( D3D12CreateDevice( adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof( ID3D12Device ), nullptr ) ) )
            {
                adapter->GetDesc1( &desc );
                PRINTINFO( L"D3D12-capable hardware found (selected):  %s (%u MB)", desc.Description, desc.DedicatedVideoMemory >> 20 );
                if ( !found ) *ppAdapter = adapter.Detach();
                found = true;
            }
        }
        *ppAdapter = adapter.Detach();
    }
}

namespace Graphics
{
    // Framework level gfx resource
    ComPtr<ID3D12Device>        g_device;
    ComPtr<IDXGISwapChain3>     g_swapChain;
    ComPtr<IDXGIFactory4>       g_factory;
    CmdListMngr                 g_cmdListMngr;
    DescriptorHeap*             g_pRTVDescriptorHeap;
    DescriptorHeap*             g_pDSVDescriptorHeap;
    DescriptorHeap*             g_pSMPDescriptorHeap;
    DescriptorHeap*             g_pCSUDescriptorHeap;
    LinearAllocator             g_CpuLinearAllocator( kCpuWritable );
    LinearAllocator             g_GpuLinearAllocator( kGpuExclusive );

    void Init()
    {
        // Initial system setting with default
        Core::g_config.enableFullScreen = false;
        Core::g_config.warpDevice = false;
        Core::g_config.vsync = false;
        Core::g_config.swapChainDesc = {};
        Core::g_config.swapChainDesc.Width = 1280;
        Core::g_config.swapChainDesc.Height = 800;
        Core::g_config.swapChainDesc.BufferCount = 5;
        Core::g_config.swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        Core::g_config.swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        Core::g_config.swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        Core::g_config.swapChainDesc.Stereo = FALSE;
        Core::g_config.swapChainDesc.SampleDesc.Count = 1;
        Core::g_config.swapChainDesc.SampleDesc.Quality = 0;
        Core::g_config.swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
        Core::g_config.swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED; // Not used
        Core::g_config.swapChainDesc.Flags = 0;
    }

    void Shutdown()
    {
        g_cmdListMngr.Shutdown();

        TextRenderer::ShutDown();

        LinearAllocator::DestroyAll();

        delete g_pRTVDescriptorHeap;
        delete g_pDSVDescriptorHeap;
        delete g_pSMPDescriptorHeap;
        delete g_pCSUDescriptorHeap;

#ifdef _DEBUG
        ID3D12DebugDevice* debugInterface;
        if ( SUCCEEDED( g_device.Get()->QueryInterface( &debugInterface ) ) )
        {
            debugInterface->ReportLiveDeviceObjects( D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL );
            debugInterface->Release();
        }
#endif
    }

    HRESULT ResizeBackBuffer()
    {
        HRESULT hr;
        // Resize the swap chain to the desired dimensions.
        DXGI_SWAP_CHAIN_DESC desc = {};
        Graphics::g_swapChain->GetDesc( &desc );
        VRET( Graphics::g_swapChain->ResizeBuffers( Core::g_config.swapChainDesc.BufferCount,
                                                    Core::g_config.swapChainDesc.Width,
                                                    Core::g_config.swapChainDesc.Height,
                                                    Core::g_config.swapChainDesc.Format,
                                                    Core::g_config.swapChainDesc.Flags ) );
        return hr;
    }

    HRESULT CreateResource()
    {
        HRESULT hr;
#ifdef _DEBUG
        // Enable the D3D12 debug layer.
        ComPtr<ID3D12Debug> debugController;
        if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &debugController ) ) ) )
        {
            debugController->EnableDebugLayer();
        }
        else
        {
            PRINTWARN( L"Unable to enable D3D12 debug validation layer." )
        }
#endif

        // Create directx device
        VRET( CreateDXGIFactory1( IID_PPV_ARGS( &Graphics::g_factory ) ) );
        if ( Core::g_config.warpDevice )
        {
            ComPtr<IDXGIAdapter> warpAdapter;
            VRET( Graphics::g_factory->EnumWarpAdapter( IID_PPV_ARGS( &warpAdapter ) ) );
            VRET( D3D12CreateDevice( warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &Graphics::g_device ) ) );
            PRINTWARN( L"Warp Device created." )
        }
        else
        {
            ComPtr<IDXGIAdapter1> hardwareAdapter;
            GetHardwareAdapter( Graphics::g_factory.Get(), &hardwareAdapter );
            VRET( D3D12CreateDevice( hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &Graphics::g_device ) ) );
        }

        // Check Direct3D 12 feature hardware support (more usage refer Direct3D 12 sdk Capability Querying)
        D3D12_FEATURE_DATA_D3D12_OPTIONS options;
        Graphics::g_device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof( options ) );
        switch ( options.ResourceBindingTier )
        {
        case D3D12_RESOURCE_BINDING_TIER_1:
            PRINTWARN( L"Tier 1 is supported." );
            break;
        case D3D12_RESOURCE_BINDING_TIER_2:
            PRINTWARN( L"Tier 1 and 2 are supported." );
            break;
        case D3D12_RESOURCE_BINDING_TIER_3:
            PRINTWARN( L"Tier 1, 2 and 3 are supported." );
            break;
        default:
            break;
        }

        // Suppress specific validation layer warnings or errors
#if _DEBUG
        ID3D12InfoQueue* pInfoQueue = nullptr;
        if ( SUCCEEDED( g_device.Get()->QueryInterface( IID_PPV_ARGS( &pInfoQueue ) ) ) )
        {
            // Suppress whole categories of messages
            //D3D12_MESSAGE_CATEGORY Categories[] = {};

            // Suppress messages based on their severity level
            D3D12_MESSAGE_SEVERITY Severities[] =
            {
                D3D12_MESSAGE_SEVERITY_INFO
            };

            // Suppress individual messages by their ID
            D3D12_MESSAGE_ID DenyIds[] = { D3D12_MESSAGE_ID_EXECUTECOMMANDLISTS_GPU_WRITTEN_READBACK_RESOURCE_MAPPED };

            D3D12_INFO_QUEUE_FILTER NewFilter = {};
            //NewFilter.DenyList.NumCategories = _countof(Categories);
            //NewFilter.DenyList.pCategoryList = Categories;
            NewFilter.DenyList.NumSeverities = _countof( Severities );
            NewFilter.DenyList.pSeverityList = Severities;
            NewFilter.DenyList.NumIDs = _countof( DenyIds );
            NewFilter.DenyList.pIDList = DenyIds;

            pInfoQueue->PushStorageFilter( &NewFilter );
            pInfoQueue->Release();
        }
#endif

#ifndef RELEASE
        // Prevent the GPU from overclocking or underclocking to get consistent timings
        g_device->SetStablePowerState( TRUE );
#endif
        g_cmdListMngr.CreateResource( g_device.Get() );
        g_pRTVDescriptorHeap = new DescriptorHeap( g_device.Get(), Core::NUM_RTV, D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
        g_pDSVDescriptorHeap = new DescriptorHeap( g_device.Get(), Core::NUM_DSV, D3D12_DESCRIPTOR_HEAP_TYPE_DSV );
        g_pSMPDescriptorHeap = new DescriptorHeap( g_device.Get(), Core::NUM_SMP, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER );
        g_pCSUDescriptorHeap = new DescriptorHeap( g_device.Get(), Core::NUM_CSU, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true );

        ASSERT( Core::g_config.swapChainDesc.BufferCount <= DXGI_MAX_SWAP_CHAIN_BUFFERS );
        // Create the swap chain
        ComPtr<IDXGISwapChain1> swapChain;
        // Swap chain needs the queue so that it can force a flush on it.
        VRET( Graphics::g_factory->CreateSwapChainForHwnd( Graphics::g_cmdListMngr.GetCommandQueue(), Core::g_hwnd, &Core::g_config.swapChainDesc, NULL, NULL, &swapChain ) );
        VRET( swapChain.As( &Graphics::g_swapChain ) );
        DXDebugName( Graphics::g_swapChain );

        // Enable or disable full screen
        if ( !Core::g_config.enableFullScreen ) VRET( Graphics::g_factory->MakeWindowAssociation( Core::g_hwnd, DXGI_MWA_NO_ALT_ENTER ) );

        // Create graphics resources for text renderer
        VRET( TextRenderer::CreateResource() );

        return S_OK;
    }

    HRESULT CompileShaderFromFile( LPCWSTR pFileName, const D3D_SHADER_MACRO* pDefines, ID3DInclude* pInclude,
                                   LPCSTR pEntrypoint, LPCSTR pTarget, UINT Flags1, UINT Flags2, ID3DBlob** ppCode )
    {
        HRESULT hr;
#if defined( DEBUG ) || defined( _DEBUG )
        // Set the D3DCOMPILE_DEBUG flag to embed debug information in the shaders.
        // Setting this flag improves the shader debugging experience, but still allows 
        // the shaders to be optimized and to run exactly the way they will run in 
        // the release configuration of this program.
        Flags1 |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        ID3DBlob* pErrorBlob = nullptr;
        hr = D3DCompileFromFile( pFileName, pDefines, pInclude, pEntrypoint, pTarget, Flags1, Flags2, ppCode, &pErrorBlob );
        if ( pErrorBlob )
        {
            PRINTERROR( reinterpret_cast< const char* >( pErrorBlob->GetBufferPointer() ) );
            pErrorBlob->Release();
        }

        return hr;
    }
}