#include "stdafx.h"
#include "RotatingCube.h"

RotatingCube::RotatingCube( uint32_t width, uint32_t height, std::wstring name ) :
    m_frameIndex( 0 )
{
    m_width = width;
    m_height = height;

    m_camOrbitRadius = 10.f;
    m_camMaxOribtRadius = 100.f;
    m_camMinOribtRadius = 2.f;
}

void RotatingCube::ResetCameraView()
{
    auto center = XMVectorSet( 0.0f, 0.0f, 0.0f, 0.0f );
    auto radius = m_camOrbitRadius;
    auto minRadius = m_camMinOribtRadius;
    auto maxRadius = m_camMaxOribtRadius;
    auto longAngle = 4.50f;
    auto latAngle = 1.45f;
    m_camera.View( center, radius, minRadius, maxRadius, longAngle, latAngle );
}


void RotatingCube::OnConfiguration()
{
    Core::g_config.swapChainDesc.BufferCount = 5;
    Core::g_config.swapChainDesc.Width = m_width;
    Core::g_config.swapChainDesc.Height = m_height;
}

HRESULT RotatingCube::OnCreateResource()
{
    HRESULT hr;
    VRET( LoadPipeline() );
    VRET( LoadAssets() );
    VRET( LoadSizeDependentResource() );
    return S_OK;
}

// Load the rendering pipeline dependencies.
HRESULT RotatingCube::LoadPipeline()
{
    HRESULT hr;

    m_frameIndex = Graphics::g_swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a depth stencil view (DSV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        VRET( Graphics::g_device->CreateDescriptorHeap( &dsvHeapDesc, IID_PPV_ARGS( &m_dsvHeap ) ) );
        DXDebugName( m_dsvHeap );
    }

    VRET( Graphics::g_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &m_commandAllocator ) ) );
    DXDebugName( m_commandAllocator );

    return S_OK;
}

// Load the sample assets.
HRESULT RotatingCube::LoadAssets()
{
    HRESULT	hr;
    // Create an empty root signature.
    {
        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        D3D12_ROOT_PARAMETER rootParameters[1];
        rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParameters[0].Descriptor.ShaderRegister = 0;
        rootParameters[0].Descriptor.RegisterSpace = 0;
        rootSignatureDesc.Init( 1, rootParameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT );

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        VRET( D3D12SerializeRootSignature( &rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error ) );
        VRET( Graphics::g_device->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( &m_rootSignature ) ) );
        DXDebugName( m_rootSignature );
    }

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;

        uint32_t compileFlags = 0;

        VRET( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "RotatingCube_shader.hlsl" ) ).c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "vsmain", "vs_5_0", compileFlags, 0, &vertexShader ) );
        VRET( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "RotatingCube_shader.hlsl" ) ).c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "psmain", "ps_5_0", compileFlags, 0, &pixelShader ) );
        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        CD3DX12_DEPTH_STENCIL_DESC depthStencilDesc( D3D12_DEFAULT );
        depthStencilDesc.DepthEnable = true;
        depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        depthStencilDesc.StencilEnable = FALSE;

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, _countof( inputElementDescs ) };
        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = { reinterpret_cast< uint8_t* >( vertexShader->GetBufferPointer() ), vertexShader->GetBufferSize() };
        psoDesc.PS = { reinterpret_cast< uint8_t* >( pixelShader->GetBufferPointer() ), pixelShader->GetBufferSize() };
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC( D3D12_DEFAULT );
        psoDesc.BlendState = CD3DX12_BLEND_DESC( D3D12_DEFAULT );
        psoDesc.DepthStencilState = depthStencilDesc;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = Core::g_config.swapChainDesc.Format;
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc.Count = 1;
        VRET( Graphics::g_device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( &m_pipelineState ) ) );
        DXDebugName( m_pipelineState );
    }

    // Create the command list.
    VRET( Graphics::g_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS( &m_commandList ) ) );
    DXDebugName( m_commandList );

    // Create the vertex buffer.
    {
        // Define the geometry for a triangle.
        Vertex cubeVertices[] =
        {
            { XMFLOAT3( -1.0f, -1.0f, -1.0f ), XMFLOAT3( 0.0f, 0.0f, 0.0f ) },
            { XMFLOAT3( -1.0f, -1.0f,  1.0f ), XMFLOAT3( 0.0f, 0.0f, 1.0f ) },
            { XMFLOAT3( -1.0f,  1.0f, -1.0f ), XMFLOAT3( 0.0f, 1.0f, 0.0f ) },
            { XMFLOAT3( -1.0f,  1.0f,  1.0f ), XMFLOAT3( 0.0f, 1.0f, 1.0f ) },
            { XMFLOAT3( 1.0f, -1.0f, -1.0f ), XMFLOAT3( 1.0f, 0.0f, 0.0f ) },
            { XMFLOAT3( 1.0f, -1.0f,  1.0f ), XMFLOAT3( 1.0f, 0.0f, 1.0f ) },
            { XMFLOAT3( 1.0f,  1.0f, -1.0f ), XMFLOAT3( 1.0f, 1.0f, 0.0f ) },
            { XMFLOAT3( 1.0f,  1.0f,  1.0f ), XMFLOAT3( 1.0f, 1.0f, 1.0f ) },
        };

        const uint32_t vertexBufferSize = sizeof( cubeVertices );

        // Note: using upload heaps to transfer static data like vert buffers is not 
        // recommended. Every time the GPU needs it, the upload heap will be marshalled 
        // over. Please read up on Default Heap usage. An upload heap is used here for 
        // code simplicity and because there are very few verts to actually transfer.
        VRET( Graphics::g_device->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ), D3D12_HEAP_FLAG_NONE,
                                                           &CD3DX12_RESOURCE_DESC::Buffer( vertexBufferSize ), D3D12_RESOURCE_STATE_GENERIC_READ,
                                                           nullptr, IID_PPV_ARGS( &m_vertexBuffer ) ) );
        DXDebugName( m_vertexBuffer );

        // Copy the triangle data to the vertex buffer.
        uint8_t* pVertexDataBegin;
        VRET( m_vertexBuffer->Map( 0, nullptr, reinterpret_cast< void** >( &pVertexDataBegin ) ) );
        memcpy( pVertexDataBegin, cubeVertices, sizeof( cubeVertices ) );
        m_vertexBuffer->Unmap( 0, nullptr );

        // Initialize the vertex buffer view.
        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof( Vertex );
        m_vertexBufferView.SizeInBytes = vertexBufferSize;
    }

    // Create the index buffer
    {
        uint16_t cubeIndices[] =
        {
            0,2,1, 1,2,3,  4,5,6, 5,7,6,  0,1,5, 0,5,4,  2,6,7, 2,7,3,  0,4,6, 0,6,2,  1,3,7, 1,7,5,
        };

        const uint32_t indexBufferSize = sizeof( cubeIndices );
        VRET( Graphics::g_device->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ), D3D12_HEAP_FLAG_NONE,
                                                           &CD3DX12_RESOURCE_DESC::Buffer( indexBufferSize ), D3D12_RESOURCE_STATE_GENERIC_READ,
                                                           nullptr, IID_PPV_ARGS( &m_indexBuffer ) ) );
        DXDebugName( m_indexBuffer );

        uint8_t* pIndexDataBegin;
        VRET( m_indexBuffer->Map( 0, nullptr, reinterpret_cast< void** >( &pIndexDataBegin ) ) );
        memcpy( pIndexDataBegin, cubeIndices, sizeof( cubeIndices ) );
        m_indexBuffer->Unmap( 0, nullptr );

        m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
        m_indexBufferView.SizeInBytes = sizeof( cubeIndices );
        m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
    }

    // Create the constant buffer
    {
        VRET( Graphics::g_device->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ), D3D12_HEAP_FLAG_NONE,
                                                           &CD3DX12_RESOURCE_DESC::Buffer( sizeof( XMFLOAT4X4 ) ), D3D12_RESOURCE_STATE_GENERIC_READ,
                                                           nullptr, IID_PPV_ARGS( &m_constantBuffer ) ) );
        DXDebugName( m_constantBuffer );

        uint8_t* pConstantBufferBegin;
        VRET( m_constantBuffer->Map( 0, nullptr, reinterpret_cast< void** >( &pConstantBufferBegin ) ) );
        XMFLOAT4X4 matrix;
        XMStoreFloat4x4( &matrix, XMMatrixIdentity() );
        memcpy( pConstantBufferBegin, &matrix, sizeof( matrix ) );
        m_constantBuffer->Unmap( 0, nullptr );
    }


    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    VRET( m_commandList->Close() );

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        VRET( Graphics::g_device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &m_fence ) ) );
        DXDebugName( m_fence );
        m_fenceValue = 1;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEventEx( nullptr, FALSE, FALSE, EVENT_ALL_ACCESS );
        if ( m_fenceEvent == nullptr )
        {
            VRET( HRESULT_FROM_WIN32( GetLastError() ) );
        }

        // Wait for the command list to execute; we are reusing the same command 
        // list in our main loop but for now, we just want to wait for setup to 
        // complete before continuing.
        WaitForPreviousFrame();
    }

    ResetCameraView();
    return S_OK;
}

// Load size dependent resource
HRESULT RotatingCube::LoadSizeDependentResource()
{
    HRESULT hr;

    uint32_t width = Core::g_config.swapChainDesc.Width;
    uint32_t height = Core::g_config.swapChainDesc.Height;

    // Create the depth stencil.
    {
        CD3DX12_RESOURCE_DESC shadowTextureDesc( D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, static_cast< uint32_t >( width ), static_cast< uint32_t >( height ),
                                                 1, 1, DXGI_FORMAT_D32_FLOAT, 1, 0, D3D12_TEXTURE_LAYOUT_UNKNOWN,
                                                 D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE );

        D3D12_CLEAR_VALUE clearValue;	// Performance tip: Tell the runtime at resource creation the desired clear value.
        clearValue.Format = DXGI_FORMAT_D32_FLOAT;
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.DepthStencil.Stencil = 0;

        VRET( Graphics::g_device->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ), D3D12_HEAP_FLAG_NONE, &shadowTextureDesc,
                                                           D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS( &m_depthBuffer ) ) );
        DXDebugName( m_depthBuffer );

        // Create the depth stencil view.
        Graphics::g_device->CreateDepthStencilView( m_depthBuffer.Get(), nullptr, m_dsvHeap->GetCPUDescriptorHandleForHeapStart() );
    }

    float fAspectRatio = width / ( FLOAT ) height;
    m_camera.Projection( XM_PIDIV2 / 2, fAspectRatio );
    return S_OK;
}

// Update frame-based values.
void RotatingCube::OnUpdate()
{
    m_timer.Tick( NULL );
    m_camera.ProcessInertia();
    // Temporary procedural for display GPU timing, should be removed once
    // GPU_Profiler::Draw() have been implemented
#ifndef RELEASE
    char temp[128];
    GPU_Profiler::BeginReadBack();
    uint32_t n = GPU_Profiler::GetTimingStr( 0, temp );
    GPU_Profiler::EndReadBack();
    swprintf( Core::g_strCustom, L"%hs", temp );
#endif
}

// Render the scene.
void RotatingCube::OnRender()
{
    // Record all the commands we need to render the scene into the command list.
    PopulateCommandList();

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    Graphics::g_cmdListMngr.GetCommandQueue()->ExecuteCommandLists( _countof( ppCommandLists ), ppCommandLists );

    WaitForPreviousFrame();
}

HRESULT RotatingCube::OnSizeChanged()
{
    HRESULT hr;
    // Flush all current GPU commands.
    WaitForPreviousFrame();
    m_depthBuffer.Reset();
    VRET( LoadSizeDependentResource() );
    return S_OK;
}


void RotatingCube::OnDestroy()
{
    // Wait for the GPU to be done with all resources.
    WaitForPreviousFrame();

    CloseHandle( m_fenceEvent );
}

bool RotatingCube::OnEvent( MSG* msg )
{
    switch ( msg->message )
    {
    case WM_MOUSEWHEEL:
        {
            auto delta = GET_WHEEL_DELTA_WPARAM( msg->wParam );
            m_camera.ZoomRadius( -0.007f*delta );
        }
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP:
        {
            auto pointerId = GET_POINTERID_WPARAM( msg->wParam );
            POINTER_INFO pointerInfo;
            if ( GetPointerInfo( pointerId, &pointerInfo ) ) {
                if ( msg->message == WM_POINTERDOWN ) {
                    // Compute pointer position in render units
                    POINT p = pointerInfo.ptPixelLocation;
                    ScreenToClient( Core::g_hwnd, &p );
                    RECT clientRect;
                    GetClientRect( Core::g_hwnd, &clientRect );
                    p.x = p.x * Core::g_config.swapChainDesc.Width / ( clientRect.right - clientRect.left );
                    p.y = p.y * Core::g_config.swapChainDesc.Height / ( clientRect.bottom - clientRect.top );
                    // Camera manipulation
                    m_camera.AddPointer( pointerId );
                }
            }

            // Otherwise send it to the camera controls
            m_camera.ProcessPointerFrames( pointerId, &pointerInfo );
            if ( msg->message == WM_POINTERUP ) m_camera.RemovePointer( pointerId );
        }
    }
    return false;
}

void RotatingCube::PopulateCommandList()
{
    HRESULT hr;
    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    V( m_commandAllocator->Reset() );

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    V( m_commandList->Reset( m_commandAllocator.Get(), m_pipelineState.Get() ) );
    {
        GPU_PROFILE( m_commandList.Get(), "Rendering" );
        XMMATRIX view = m_camera.View();
        XMMATRIX proj = m_camera.Projection();

        XMMATRIX world = XMMatrixRotationY( static_cast< float >( m_timer.GetTotalSeconds() ) );
        XMMATRIX wvp = XMMatrixMultiply( XMMatrixMultiply( world, view ), proj );
        void* pCB;
        V( m_constantBuffer->Map( 0, nullptr, &pCB ) );
        if ( pCB ) {
            XMStoreFloat4x4( ( XMFLOAT4X4* ) pCB, wvp );
            m_constantBuffer->Unmap( 0, nullptr );
        }

        // Set necessary state.
        m_commandList->SetGraphicsRootSignature( m_rootSignature.Get() );
        m_commandList->SetGraphicsRootConstantBufferView( 0, m_constantBuffer->GetGPUVirtualAddress() );
        m_commandList->RSSetViewports( 1, &Graphics::g_DisplayPlaneViewPort );
        m_commandList->RSSetScissorRects( 1, &Graphics::g_DisplayPlaneScissorRect );

        // Indicate that the back buffer will be used as a render target.
        m_commandList->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( Graphics::g_pDisplayBuffers[Graphics::g_CurrentDPIdx].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET ) );

        m_commandList->OMSetRenderTargets( 1, &Graphics::g_pDisplayPlaneHandlers[Graphics::g_CurrentDPIdx], FALSE, &m_dsvHeap->GetCPUDescriptorHandleForHeapStart() );

        // Record commands.
        const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
        m_commandList->ClearRenderTargetView( Graphics::g_pDisplayPlaneHandlers[Graphics::g_CurrentDPIdx], clearColor, 0, nullptr );
        m_commandList->ClearDepthStencilView( m_dsvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr );
        m_commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        m_commandList->IASetVertexBuffers( 0, 1, &m_vertexBufferView );
        m_commandList->IASetIndexBuffer( &m_indexBufferView );
        m_commandList->DrawIndexedInstanced( 36, 1, 0, 0, 0 );

        // Indicate that the back buffer will now be used to present.
        m_commandList->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( Graphics::g_pDisplayBuffers[Graphics::g_CurrentDPIdx].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT ) );
    }
    V( m_commandList->Close() );
}

void RotatingCube::WaitForPreviousFrame()
{
    HRESULT hr;
    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This is code implemented as such for simplicity. More advanced samples 
    // illustrate how to use fences for efficient resource usage.

    // Signal and increment the fence value.
    const uint64_t fence = m_fenceValue;
    V( Graphics::g_cmdListMngr.GetCommandQueue()->Signal( m_fence.Get(), fence ) );
    m_fenceValue++;

    // Wait until the previous frame is finished.
    if ( m_fence->GetCompletedValue() < fence )
    {
        V( m_fence->SetEventOnCompletion( fence, m_fenceEvent ) );
        WaitForSingleObject( m_fenceEvent, INFINITE );
    }

    m_frameIndex = Graphics::g_swapChain->GetCurrentBackBufferIndex();
}
