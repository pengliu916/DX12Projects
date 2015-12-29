
#include "stdafx.h"
#include "VolumetricAnimation.h"

#include "VolumetricAnimation_SharedHeader.inl"

VolumetricAnimation::VolumetricAnimation( uint32_t width, uint32_t height, std::wstring name ) :
    m_viewport(), m_scissorRect()
{
    m_volumeWidth = VOLUME_SIZE_X;
    m_volumeHeight = VOLUME_SIZE_Y;
    m_volumeDepth = VOLUME_SIZE_Z;

    m_pConstantBufferData = new ConstantBuffer();
    m_pConstantBufferData->bgCol = XMINT4( 32, 32, 32, 32 );

    m_width = width;
    m_height = height;

    m_camOrbitRadius = 10.f;
    m_camMaxOribtRadius = 100.f;
    m_camMinOribtRadius = 2.f;

#if !STATIC_ARRAY
    for ( uint32_t i = 0; i < ARRAY_COUNT( shiftingColVals ); i++ )
        m_pConstantBufferData->shiftingColVals[i] = shiftingColVals[i];
#endif
}

VolumetricAnimation::~VolumetricAnimation()
{
    delete m_pConstantBufferData;
}

void VolumetricAnimation::ResetCameraView()
{
    auto center = XMVectorSet( 0.0f, 0.0f, 0.0f, 0.0f );
    auto radius = m_camOrbitRadius;
    auto maxRadius = m_camMaxOribtRadius;
    auto minRadius = m_camMinOribtRadius;
    auto longAngle = 4.50f;
    auto latAngle = 1.45f;
    m_camera.View( center, radius, minRadius, maxRadius, longAngle, latAngle );
}

void VolumetricAnimation::OnConfiguration()
{
    Core::g_config.swapChainDesc.BufferCount = 5;
    Core::g_config.swapChainDesc.Width = m_width;
    Core::g_config.swapChainDesc.Height = m_height;
}

HRESULT VolumetricAnimation::OnCreateResource()
{
    ASSERT( Graphics::g_device );
    HRESULT hr;
    VRET( LoadPipeline( Graphics::g_device.Get() ) );
    VRET( LoadAssets() );
    VRET( LoadSizeDependentResource() );

    return S_OK;
}

// Load the rendering pipeline dependencies.
HRESULT VolumetricAnimation::LoadPipeline( ID3D12Device* m_device )
{
    m_dsvHandle = Graphics::g_pDSVDescriptorHeap->Append().GetCPUHandle();

    return S_OK;
}

// Load the assets.
HRESULT VolumetricAnimation::LoadAssets()
{
    HRESULT	hr;

    // Create a root signature consisting of a descriptor table with a CBV SRV and a sampler.
    {
        CD3DX12_DESCRIPTOR_RANGE ranges[3];
        CD3DX12_ROOT_PARAMETER rootParameters[3];

        ranges[0].Init( D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0 );
        ranges[1].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0 );
        ranges[2].Init( D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0 );

#if USING_DESCRIPTOR_TABLE
        rootParameters[RootParameterCBV].InitAsDescriptorTable( 1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL );
        rootParameters[RootParameterSRV].InitAsDescriptorTable( 1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL );
        rootParameters[RootParameterUAV].InitAsDescriptorTable( 1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL );
#else
        rootParameters[RootParameterCBV].InitAsConstantBufferView( 0 );
        rootParameters[RootParameterSRV].InitAsShaderResourceView( 0 );
        rootParameters[RootParameterUAV].InitAsUnorderedAccessView( 0 );
#endif
        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.MipLODBias = 0;
        sampler.MaxAnisotropy = 0;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // Allow input layout and deny unnecessary access to certain pipeline stages.
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init( _countof( rootParameters ), rootParameters, 1, &sampler, rootSignatureFlags );

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        V( D3D12SerializeRootSignature( &rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error ) );
        if ( error ) PRINTERROR( reinterpret_cast< const char* >( error->GetBufferPointer() ) );

        VRET( Graphics::g_device->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( &m_graphicsRootSignature ) ) );
        DXDebugName( m_graphicsRootSignature );

        // Create compute signature. Must change visibility for the SRV.
        rootParameters[RootParameterSRV].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        CD3DX12_ROOT_SIGNATURE_DESC computeRootSignatureDesc( _countof( rootParameters ), rootParameters, 0, nullptr );
        VRET( D3D12SerializeRootSignature( &computeRootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error ) );

        VRET( Graphics::g_device->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( &m_computeRootSignature ) ) );
    }

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;
        ComPtr<ID3DBlob> computeShader;

        uint32_t compileFlags = 0;
        D3D_SHADER_MACRO macro[] =
        {
            {"__hlsl",			"1"},
            {nullptr,		nullptr}
        };
        VRET( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "VolumetricAnimation_shader.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "vsmain", "vs_5_0", compileFlags, 0, &vertexShader ) );
        VRET( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "VolumetricAnimation_shader.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "psmain", "ps_5_0", compileFlags, 0, &pixelShader ) );
        VRET( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "VolumetricAnimation_shader.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "csmain", "cs_5_0", compileFlags, 0, &computeShader ) );
        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        CD3DX12_DEPTH_STENCIL_DESC depthStencilDesc( D3D12_DEFAULT );
        depthStencilDesc.DepthEnable = true;
        depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        depthStencilDesc.StencilEnable = FALSE;

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, _countof( inputElementDescs ) };
        psoDesc.pRootSignature = m_graphicsRootSignature.Get();
        psoDesc.VS = { reinterpret_cast< uint8_t* >( vertexShader->GetBufferPointer() ), vertexShader->GetBufferSize() };
        psoDesc.PS = { reinterpret_cast< uint8_t* >( pixelShader->GetBufferPointer() ), pixelShader->GetBufferSize() };
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC( D3D12_DEFAULT );
        psoDesc.BlendState = CD3DX12_BLEND_DESC( D3D12_DEFAULT );
        psoDesc.DepthStencilState = depthStencilDesc;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc.Count = 1;
        VRET( Graphics::g_device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( &m_pipelineState ) ) );
        DXDebugName( m_pipelineState );

        // Describe and create the compute pipeline state object (PSO).
        D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
        computePsoDesc.pRootSignature = m_computeRootSignature.Get();
        computePsoDesc.CS = { reinterpret_cast< uint8_t* >( computeShader->GetBufferPointer() ), computeShader->GetBufferSize() };

        VRET( Graphics::g_device->CreateComputePipelineState( &computePsoDesc, IID_PPV_ARGS( &m_computeState ) ) );
        DXDebugName( m_computeState );
    }

    Graphics::g_cmdListMngr.CreateNewCommandList( &m_computeCmdList, &m_cptcmdAllocator );
    m_computeCmdList->Close();
    Graphics::g_cmdListMngr.DiscardAllocator( 0, m_cptcmdAllocator );

    Graphics::g_cmdListMngr.CreateNewCommandList( &m_graphicCmdList, &m_gfxcmdAllocator );

    // Note: ComPtr's are CPU objects but this resource needs to stay in scope until
    // the command list that references it has finished executing on the GPU.
    // We will flush the GPU at the end of this method to ensure the resource is not
    // prematurely destroyed.
    ComPtr<ID3D12Resource> volumeBufferUploadHeap;

    // Create the volumeBuffer.
    {
        uint64_t volumeBufferSize = m_volumeDepth*m_volumeHeight*m_volumeWidth * 4 * sizeof( uint8_t );

        D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer( volumeBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );
        D3D12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer( volumeBufferSize );

        VRET( Graphics::g_device->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ), D3D12_HEAP_FLAG_NONE,
                                                           &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &m_volumeBuffer ) ) );

        const UINT64 uploadBufferSize = GetRequiredIntermediateSize( m_volumeBuffer.Get(), 0, 1 );

        // Create the GPU upload buffer.
        VRET( Graphics::g_device->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ), D3D12_HEAP_FLAG_NONE,
                                                           &uploadBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                           nullptr, IID_PPV_ARGS( &volumeBufferUploadHeap ) ) );

        // Copy data to the intermediate upload heap and then schedule a copy 
        // from the upload heap to the Texture2D.
        uint8_t* volumeBuffer = ( uint8_t* ) malloc( volumeBufferSize );

        float a = m_volumeWidth / 2.f;
        float b = m_volumeHeight / 2.f;
        float c = m_volumeDepth / 2.f;
#if SPHERE_VOLUME_ANIMATION
        float radius = sqrt( a*a + b*b + c*c );
#else
        float radius = ( abs( a ) + abs( b ) + abs( c ) );
#endif
        XMINT4 bg = m_pConstantBufferData->bgCol;
        uint32_t bgMax = max( max( bg.x, bg.y ), bg.z );
        m_pConstantBufferData->bgCol.w = bgMax;
        for ( uint32_t z = 0; z < m_volumeDepth; z++ )
            for ( uint32_t y = 0; y < m_volumeHeight; y++ )
                for ( uint32_t x = 0; x < m_volumeWidth; x++ )
                {
                    float _x = x - m_volumeWidth / 2.f;
                    float _y = y - m_volumeHeight / 2.f;
                    float _z = z - m_volumeDepth / 2.f;
#if SPHERE_VOLUME_ANIMATION
                    float currentRaidus = sqrt( _x*_x + _y*_y + _z*_z );
#else
                    float currentRaidus = ( abs( _x ) + abs( _y ) + abs( _z ) );
#endif
                    float scale = currentRaidus / radius;
                    uint32_t maxColCnt = 4;
                    assert( maxColCnt < COLOR_COUNT );
                    float currentScale = scale * maxColCnt + 0.1f;
                    uint32_t idx = COLOR_COUNT - ( uint32_t ) ( currentScale ) -1;
                    float intensity = currentScale - ( uint32_t ) currentScale;
                    uint32_t col = ( uint32_t ) ( intensity * ( 255 - bgMax ) ) + 1;
                    volumeBuffer[( x + y*m_volumeWidth + z*m_volumeHeight*m_volumeWidth ) * 4 + 0] = bg.x + col * shiftingColVals[idx].x;
                    volumeBuffer[( x + y*m_volumeWidth + z*m_volumeHeight*m_volumeWidth ) * 4 + 1] = bg.y + col * shiftingColVals[idx].y;
                    volumeBuffer[( x + y*m_volumeWidth + z*m_volumeHeight*m_volumeWidth ) * 4 + 2] = bg.z + col * shiftingColVals[idx].z;
                    volumeBuffer[( x + y*m_volumeWidth + z*m_volumeHeight*m_volumeWidth ) * 4 + 3] = shiftingColVals[idx].w;
                }
        D3D12_SUBRESOURCE_DATA volumeBufferData = {};
        volumeBufferData.pData = &volumeBuffer[0];
        volumeBufferData.RowPitch = volumeBufferSize;
        volumeBufferData.SlicePitch = volumeBufferData.RowPitch;

        UpdateSubresources<1>( m_graphicCmdList.Get(), m_volumeBuffer.Get(), volumeBufferUploadHeap.Get(), 0, 0, 1, &volumeBufferData );
        free( volumeBuffer );
        m_graphicCmdList->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( m_volumeBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ) );

        // Describe and create a SRV for the volumeBuffer.
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = m_volumeDepth*m_volumeHeight*m_volumeWidth;
        srvDesc.Buffer.StructureByteStride = 4 * sizeof( uint8_t );
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;


        m_srvHandle = Graphics::g_pCSUDescriptorHeap->Append();
        Graphics::g_device->CreateShaderResourceView( m_volumeBuffer.Get(), &srvDesc, m_srvHandle.GetCPUHandle() );

        // Describe and create a UAV for the volumeBuffer.
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = m_volumeWidth*m_volumeHeight*m_volumeDepth;
        uavDesc.Buffer.StructureByteStride = 4 * sizeof( uint8_t );
        uavDesc.Buffer.CounterOffsetInBytes = 0;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        m_uavHandle = Graphics::g_pCSUDescriptorHeap->Append();
        Graphics::g_device->CreateUnorderedAccessView( m_volumeBuffer.Get(), nullptr, &uavDesc, m_uavHandle.GetCPUHandle() );
    }

    // Create the vertex buffer.

    // Note: ComPtr's are CPU objects but this resource needs to stay in scope until
    // the command list that references it has finished executing on the GPU.
    // We will flush the GPU at the end of this method to ensure the resource is not
    // prematurely destroyed.
    ComPtr<ID3D12Resource> vertexBufferUpload;
    {
        // Define the geometry for a triangle.
        Vertex cubeVertices[] =
        {
            { XMFLOAT3( -1 * VOLUME_SIZE_X * 0.5f * VOLUME_SIZE_SCALE, -1 * VOLUME_SIZE_Y * 0.5f * VOLUME_SIZE_SCALE, -1 * VOLUME_SIZE_Z * 0.5f * VOLUME_SIZE_SCALE )},
            { XMFLOAT3( -1 * VOLUME_SIZE_X * 0.5f * VOLUME_SIZE_SCALE, -1 * VOLUME_SIZE_Y * 0.5f * VOLUME_SIZE_SCALE,  1 * VOLUME_SIZE_Z * 0.5f * VOLUME_SIZE_SCALE )},
            { XMFLOAT3( -1 * VOLUME_SIZE_X * 0.5f * VOLUME_SIZE_SCALE,  1 * VOLUME_SIZE_Y * 0.5f * VOLUME_SIZE_SCALE, -1 * VOLUME_SIZE_Z * 0.5f * VOLUME_SIZE_SCALE )},
            { XMFLOAT3( -1 * VOLUME_SIZE_X * 0.5f * VOLUME_SIZE_SCALE,  1 * VOLUME_SIZE_Y * 0.5f * VOLUME_SIZE_SCALE,  1 * VOLUME_SIZE_Z * 0.5f * VOLUME_SIZE_SCALE )},
            { XMFLOAT3( 1 * VOLUME_SIZE_X * 0.5f * VOLUME_SIZE_SCALE, -1 * VOLUME_SIZE_Y * 0.5f * VOLUME_SIZE_SCALE, -1 * VOLUME_SIZE_Z * 0.5f * VOLUME_SIZE_SCALE )},
            { XMFLOAT3( 1 * VOLUME_SIZE_X * 0.5f * VOLUME_SIZE_SCALE, -1 * VOLUME_SIZE_Y * 0.5f * VOLUME_SIZE_SCALE,  1 * VOLUME_SIZE_Z * 0.5f * VOLUME_SIZE_SCALE )},
            { XMFLOAT3( 1 * VOLUME_SIZE_X * 0.5f * VOLUME_SIZE_SCALE,  1 * VOLUME_SIZE_Y * 0.5f * VOLUME_SIZE_SCALE, -1 * VOLUME_SIZE_Z * 0.5f * VOLUME_SIZE_SCALE )},
            { XMFLOAT3( 1 * VOLUME_SIZE_X * 0.5f * VOLUME_SIZE_SCALE,  1 * VOLUME_SIZE_Y * 0.5f * VOLUME_SIZE_SCALE,  1 * VOLUME_SIZE_Z * 0.5f * VOLUME_SIZE_SCALE )},
        };

        const uint32_t vertexBufferSize = sizeof( cubeVertices );

        VRET( Graphics::g_device->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ), D3D12_HEAP_FLAG_NONE,
                                                           &CD3DX12_RESOURCE_DESC::Buffer( vertexBufferSize ), D3D12_RESOURCE_STATE_GENERIC_READ,
                                                           nullptr, IID_PPV_ARGS( &vertexBufferUpload ) ) );
        VRET( Graphics::g_device->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ), D3D12_HEAP_FLAG_NONE,
                                                           &CD3DX12_RESOURCE_DESC::Buffer( vertexBufferSize ), D3D12_RESOURCE_STATE_COPY_DEST,
                                                           nullptr, IID_PPV_ARGS( &m_vertexBuffer ) ) );
        DXDebugName( m_vertexBuffer );

        D3D12_SUBRESOURCE_DATA vertexData = {};
        vertexData.pData = reinterpret_cast< uint8_t* >( cubeVertices );
        vertexData.RowPitch = vertexBufferSize;
        vertexData.SlicePitch = vertexBufferSize;

        UpdateSubresources<1>( m_graphicCmdList.Get(), m_vertexBuffer.Get(), vertexBufferUpload.Get(), 0, 0, 1, &vertexData );
        m_graphicCmdList->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( m_vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                                                     D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ) );

        // Initialize the vertex buffer view.
        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof( Vertex );
        m_vertexBufferView.SizeInBytes = vertexBufferSize;
    }

    // Create the index buffer

    // Note: ComPtr's are CPU objects but this resource needs to stay in scope until
    // the command list that references it has finished executing on the GPU.
    // We will flush the GPU at the end of this method to ensure the resource is not
    // prematurely destroyed.
    ComPtr<ID3D12Resource> indexBufferUpload;
    {
        uint16_t cubeIndices[] =
        {
            0,2,1, 1,2,3,  4,5,6, 5,7,6,  0,1,5, 0,5,4,  2,6,7, 2,7,3,  0,4,6, 0,6,2,  1,3,7, 1,7,5,
        };

        const uint32_t indexBufferSize = sizeof( cubeIndices );

        VRET( Graphics::g_device->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ), D3D12_HEAP_FLAG_NONE,
                                                           &CD3DX12_RESOURCE_DESC::Buffer( indexBufferSize ), D3D12_RESOURCE_STATE_GENERIC_READ,
                                                           nullptr, IID_PPV_ARGS( &indexBufferUpload ) ) );
        VRET( Graphics::g_device->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ), D3D12_HEAP_FLAG_NONE,
                                                           &CD3DX12_RESOURCE_DESC::Buffer( indexBufferSize ), D3D12_RESOURCE_STATE_COPY_DEST,
                                                           nullptr, IID_PPV_ARGS( &m_indexBuffer ) ) );
        DXDebugName( m_indexBuffer );

        D3D12_SUBRESOURCE_DATA indexData = {};
        indexData.pData = reinterpret_cast< uint8_t* >( cubeIndices );
        indexData.RowPitch = indexBufferSize;
        indexData.SlicePitch = indexBufferSize;

        UpdateSubresources<1>( m_graphicCmdList.Get(), m_indexBuffer.Get(), indexBufferUpload.Get(), 0, 0, 1, &indexData );
        m_graphicCmdList->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( m_indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                                                     D3D12_RESOURCE_STATE_INDEX_BUFFER ) );

        m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
        m_indexBufferView.SizeInBytes = sizeof( cubeIndices );
        m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
    }

    // Create the constant buffer
    {
        VRET( Graphics::g_device->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ), D3D12_HEAP_FLAG_NONE,
                                                           &CD3DX12_RESOURCE_DESC::Buffer( 1024 * 64 ), D3D12_RESOURCE_STATE_GENERIC_READ,
                                                           nullptr, IID_PPV_ARGS( &m_constantBuffer ) ) );
        DXDebugName( m_constantBuffer );

        // Describe and create a constant buffer view.
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = ( sizeof( ConstantBuffer ) + 255 ) & ~255;	// CB size is required to be 256-byte aligned.
        m_cbvHandle = Graphics::g_pCSUDescriptorHeap->Append();
        Graphics::g_device->CreateConstantBufferView( &cbvDesc, m_cbvHandle.GetCPUHandle() );

        // Initialize and map the constant buffers. We don't unmap this until the
        // app closes. Keeping things mapped for the lifetime of the resource is okay.
        CD3DX12_RANGE readRange( 0, 0 );		// We do not intend to read from this resource on the CPU.
        VRET( m_constantBuffer->Map( 0, &readRange, reinterpret_cast< void** >( &m_pCbvDataBegin ) ) );
        memcpy( m_pCbvDataBegin, &m_pConstantBufferData, sizeof( m_pConstantBufferData ) );
    }

    // Close the command list and execute it to begin the initial GPU setup.
    VRET( m_graphicCmdList->Close() );


    uint64_t FenceValue = Graphics::g_cmdListMngr.ExecuteCommandList( m_graphicCmdList.Get() );
    Graphics::g_cmdListMngr.DiscardAllocator( FenceValue, m_gfxcmdAllocator );
    Graphics::g_cmdListMngr.IdleGPU();

    ResetCameraView();

    return S_OK;
}

// Load size dependent resource
HRESULT VolumetricAnimation::LoadSizeDependentResource()
{
    HRESULT hr;

    uint32_t width = Core::g_config.swapChainDesc.Width;
    uint32_t height = Core::g_config.swapChainDesc.Height;

    // Create the depth stencil.
    {
        CD3DX12_RESOURCE_DESC shadowTextureDesc( D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, static_cast< uint32_t >( width ),
                                                 static_cast< uint32_t >( height ), 1, 1, DXGI_FORMAT_D32_FLOAT,
                                                 1, 0, D3D12_TEXTURE_LAYOUT_UNKNOWN,
                                                 D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE );

        D3D12_CLEAR_VALUE clearValue;	// Performance tip: Tell the runtime at resource creation the desired clear value.
        clearValue.Format = DXGI_FORMAT_D32_FLOAT;
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.DepthStencil.Stencil = 0;

        VRET( Graphics::g_device->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ), D3D12_HEAP_FLAG_NONE, &shadowTextureDesc,
                                                           D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS( &m_depthBuffer ) ) );
        DXDebugName( m_depthBuffer );

        // Create the depth stencil view.
        Graphics::g_device->CreateDepthStencilView( m_depthBuffer.Get(), nullptr, m_dsvHandle );
    }

    float fAspectRatio = width / ( FLOAT ) height;
    m_camera.Projection( XM_PIDIV2 / 2, fAspectRatio );
    return S_OK;
}

// Update frame-based values.
void VolumetricAnimation::OnUpdate()
{
    m_camera.ProcessInertia();
    // Temporary procedural for display GPU timing, should be removed once
    // GPU_Profiler::Draw() have been implemented
#ifndef RELEASE
    char temp[128];
    GPU_Profiler::BeginReadBack();
    uint32_t n = GPU_Profiler::GetTimingStr( 0, temp );
    GPU_Profiler::GetTimingStr( 1, temp + n );
    GPU_Profiler::EndReadBack();
    swprintf( Core::g_strCustom, L"%hs", temp );
#endif
}

// Render the scene.
void VolumetricAnimation::OnRender()
{
    XMMATRIX view = m_camera.View();
    XMMATRIX proj = m_camera.Projection();

    XMMATRIX world = XMMatrixIdentity();
    m_pConstantBufferData->invWorld = XMMatrixInverse( nullptr, world );
    m_pConstantBufferData->wvp = XMMatrixMultiply( XMMatrixMultiply( world, view ), proj );
    XMStoreFloat4( &m_pConstantBufferData->viewPos, m_camera.Eye() );

    memcpy( m_pCbvDataBegin, m_pConstantBufferData, sizeof( ConstantBuffer ) );

    PopulateComputeCommandList( 0 );
    uint64_t FenceValue = Graphics::g_cmdListMngr.ExecuteCommandList( m_computeCmdList.Get() );
    Graphics::g_cmdListMngr.DiscardAllocator( FenceValue, m_cptcmdAllocator );
    Graphics::g_cmdListMngr.WaitForFence( FenceValue );

    PopulateGraphicsCommandList( Graphics::g_CurrentDPIdx );
    FenceValue = Graphics::g_cmdListMngr.ExecuteCommandList( m_graphicCmdList.Get() );
    Graphics::g_cmdListMngr.DiscardAllocator( FenceValue, m_gfxcmdAllocator );
    Graphics::g_CpuLinearAllocator.CleanupUsedPages( FenceValue );
}

HRESULT VolumetricAnimation::OnSizeChanged()
{
    HRESULT hr;
    m_depthBuffer.Reset();
    VRET( LoadSizeDependentResource() );
    return S_OK;
}

void VolumetricAnimation::OnDestroy()
{
    // Wait for the GPU to be done with all resources.
    Graphics::g_cmdListMngr.IdleGPU();
}

bool VolumetricAnimation::OnEvent( MSG* msg )
{
    switch ( msg->message )
    {
    case WM_MOUSEWHEEL:
        {
            auto delta = GET_WHEEL_DELTA_WPARAM( msg->wParam );
            m_camera.ZoomRadius( -0.007f*delta );
            return true;
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
            return true;
        }

        return 0;
    }
    return false;
}

void VolumetricAnimation::PopulateGraphicsCommandList( uint32_t i )
{
    HRESULT hr;
    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    m_gfxcmdAllocator = Graphics::g_cmdListMngr.RequestAllocator();

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    V( m_graphicCmdList->Reset( m_gfxcmdAllocator, m_pipelineState.Get() ) );

    {
        GPU_PROFILE( m_graphicCmdList.Get(), "Rendering" );

        // Set necessary state.
        m_graphicCmdList->SetGraphicsRootSignature( m_graphicsRootSignature.Get() );

        ID3D12DescriptorHeap* ppHeaps[] = { Graphics::g_pCSUDescriptorHeap->mHeap.Get() };
        m_graphicCmdList->SetDescriptorHeaps( _countof( ppHeaps ), ppHeaps );

#if USING_DESCRIPTOR_TABLE
        m_graphicCmdList->SetGraphicsRootDescriptorTable( RootParameterCBV, m_cbvHandle.GetGPUHandle() );
        m_graphicCmdList->SetGraphicsRootDescriptorTable( RootParameterSRV, m_srvHandle.GetGPUHandle() );
#else
        m_graphicCmdList->SetGraphicsRootConstantBufferView( RootParameterCBV, m_constantBuffer->GetGPUVirtualAddress() );
        m_graphicCmdList->SetGraphicsRootShaderResourceView( RootParameterSRV, m_volumeBuffer->GetGPUVirtualAddress() );
#endif
        m_graphicCmdList->RSSetViewports( 1, &Graphics::g_DisplayPlaneViewPort );
        m_graphicCmdList->RSSetScissorRects( 1, &Graphics::g_DisplayPlaneScissorRect );

        // Indicate that the back buffer will be used as a render target.
        D3D12_RESOURCE_BARRIER resourceBarriersBefore[] = {
            CD3DX12_RESOURCE_BARRIER::Transition( Graphics::g_pDisplayBuffers[Graphics::g_CurrentDPIdx].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET ),
            CD3DX12_RESOURCE_BARRIER::Transition( m_volumeBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE )
        };
        m_graphicCmdList->ResourceBarrier( 2, resourceBarriersBefore );
        m_graphicCmdList->OMSetRenderTargets( 1, &Graphics::g_pDisplayPlaneHandlers[Graphics::g_CurrentDPIdx], FALSE, &m_dsvHandle );

        // Record commands.
        const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_graphicCmdList->ClearRenderTargetView( Graphics::g_pDisplayPlaneHandlers[Graphics::g_CurrentDPIdx], clearColor, 0, nullptr );
        m_graphicCmdList->ClearDepthStencilView( m_dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr );
        m_graphicCmdList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        m_graphicCmdList->IASetVertexBuffers( 0, 1, &m_vertexBufferView );
        m_graphicCmdList->IASetIndexBuffer( &m_indexBufferView );
        m_graphicCmdList->DrawIndexedInstanced( 36, 1, 0, 0, 0 );


        // Draw Text
        TextContext m_TextRenderer;
        m_TextRenderer.Begin( m_graphicCmdList.Get() );
        m_TextRenderer.SetViewSize( ( float ) Core::g_config.swapChainDesc.Width, ( float ) Core::g_config.swapChainDesc.Height );
        m_TextRenderer.SetFont( L"xerox.fnt" );
        m_TextRenderer.ResetCursor( 10, 20 );
        m_TextRenderer.SetTextSize( 20.f );
        m_TextRenderer.DrawString( "ABCDEFGHIJKLMNOPQRSTUVWXYZ\nabcdefghijklmnopqrstuvwxyz\n1234567890\n" );
        m_TextRenderer.End();

        // Indicate that the back buffer will now be used to present.
        D3D12_RESOURCE_BARRIER resourceBarriersAfter[] = {
            CD3DX12_RESOURCE_BARRIER::Transition( Graphics::g_pDisplayBuffers[Graphics::g_CurrentDPIdx].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT ),
            CD3DX12_RESOURCE_BARRIER::Transition( m_volumeBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS )
        };
        m_graphicCmdList->ResourceBarrier( 2, resourceBarriersAfter );

    }

    V( m_graphicCmdList->Close() );
}

void VolumetricAnimation::PopulateComputeCommandList( uint32_t i )
{
    HRESULT hr;
    m_cptcmdAllocator = Graphics::g_cmdListMngr.RequestAllocator();

    V( m_computeCmdList->Reset( m_cptcmdAllocator, m_computeState.Get() ) );
    {
        GPU_PROFILE( m_computeCmdList.Get(), "Processing" );
        m_computeCmdList->SetComputeRootSignature( m_computeRootSignature.Get() );
        ID3D12DescriptorHeap* ppHeaps[] = { Graphics::g_pCSUDescriptorHeap->mHeap.Get() };
        m_computeCmdList->SetDescriptorHeaps( _countof( ppHeaps ), ppHeaps );

#if USING_DESCRIPTOR_TABLE
        m_computeCmdList->SetComputeRootDescriptorTable( RootParameterCBV, m_cbvHandle.GetGPUHandle() );
        m_computeCmdList->SetComputeRootDescriptorTable( RootParameterUAV, m_uavHandle.GetGPUHandle() );
#else
        m_computeCmdList->SetComputeRootConstantBufferView( RootParameterCBV, m_constantBuffer->GetGPUVirtualAddress() );
        m_computeCmdList->SetComputeRootUnorderedAccessView( RootParameterUAV, m_volumeBuffer->GetGPUVirtualAddress() );
#endif

        m_computeCmdList->Dispatch( m_volumeWidth / THREAD_X, m_volumeHeight / THREAD_Y, m_volumeDepth / THREAD_Z );
    }
    m_computeCmdList->Close();
}
