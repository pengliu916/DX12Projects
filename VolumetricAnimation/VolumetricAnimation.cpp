
#include "stdafx.h"
#include "VolumetricAnimation.h"
#include "GPU_Profiler.h"

#include "VolumetricAnimation_SharedHeader.inl"

VolumetricAnimation::VolumetricAnimation( UINT width, UINT height, std::wstring name ) :
	DX12Framework( width, height, name ), m_frameIndex( 0 ), m_viewport(), m_scissorRect(), m_rtvDescriptorSize( 0 )
{
	m_volumeWidth = VOLUME_SIZE_X;
	m_volumeHeight = VOLUME_SIZE_Y;
	m_volumeDepth = VOLUME_SIZE_Z;

	m_pConstantBufferData = new ConstantBuffer();
	m_pConstantBufferData->bgCol = XMINT4( 32, 32, 32, 32 );

#if !STATIC_ARRAY
	for ( UINT i = 0; i < ARRAY_COUNT( shiftingColVals ); i++ )
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
	auto radius = DEFAULT_ORBIT_RADIUS;
	auto minRadius = MIN_ORBIT_RADIUS;
	auto maxRadius = MAX_ORBIT_RADIUS;
	auto longAngle = 4.50f;
	auto latAngle = 1.45f;
	m_camera.View( center, radius, minRadius, maxRadius, longAngle, latAngle );
}

void VolumetricAnimation::OnConfiguration( Settings* config )
{
	config->swapChainDesc.BufferCount = m_FrameCount;
}

HRESULT VolumetricAnimation::OnInit( ID3D12Device* m_device )
{
	HRESULT hr;
	VRET( LoadPipeline( m_device ) );
	VRET( LoadAssets() );
	VRET( LoadSizeDependentResource() );
	return S_OK;
}

// Load the rendering pipeline dependencies.
HRESULT VolumetricAnimation::LoadPipeline( ID3D12Device* m_device )
{
	HRESULT hr;

	// Describe and create the compute command queue;
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
	VRET( m_device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &m_computeCmdQueue ) ) );
	DXDebugName( m_computeCmdQueue );

	m_frameIndex = framework_gfxSwapChain->GetCurrentBackBufferIndex();

	// Create descriptor heaps.
	{
		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = m_FrameCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		VRET( m_device->CreateDescriptorHeap( &rtvHeapDesc, IID_PPV_ARGS( &m_rtvHeap ) ) );
		DXDebugName( m_rtvHeap );

		m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );

		// Describe and create a shader resource view (SRV) and constant buffer view (CBV) descriptor heap.
		// Flags indicate that this descriptor heap can be bound to the pipeline
		// and that descriptors contained in it can be reference by a root table
		D3D12_DESCRIPTOR_HEAP_DESC cbvsrvuavHeapDesc = {};
		cbvsrvuavHeapDesc.NumDescriptors = 3; // One for SRV two for CBV (gfx and compute)
		cbvsrvuavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		cbvsrvuavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		VRET( m_device->CreateDescriptorHeap( &cbvsrvuavHeapDesc, IID_PPV_ARGS( &m_cbvsrvuavHeap ) ) );
		DXDebugName( m_cbvsrvuavHeap );

		m_cbvsrvuavDescriptorSize = m_device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

		// Describe and create a depth stencil view (DSV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
		dsvHeapDesc.NumDescriptors = 1;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		VRET( m_device->CreateDescriptorHeap( &dsvHeapDesc, IID_PPV_ARGS( &m_dsvHeap ) ) );
		DXDebugName( m_dsvHeap );
	}

	VRET( m_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &m_graphicCmdAllocator ) ) );
	DXDebugName( m_graphicCmdAllocator );

	VRET( m_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS( &m_computeCmdAllocator ) ) );
	DXDebugName( m_computeCmdAllocator );

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
		rootParameters[RootParameterCBV].InitAsDescriptorTable( 1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL );
		rootParameters[RootParameterSRV].InitAsDescriptorTable( 1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL );
		rootParameters[RootParameterUAV].InitAsDescriptorTable( 1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL );

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

		VRET( framework_gfxDevice->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( &m_graphicsRootSignature ) ) );
		DXDebugName( m_graphicsRootSignature );

		// Create compute signature. Must change visibility for the SRV.
		rootParameters[RootParameterSRV].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		CD3DX12_ROOT_SIGNATURE_DESC computeRootSignatureDesc( _countof( rootParameters ), rootParameters, 0, nullptr );
		VRET( D3D12SerializeRootSignature( &computeRootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error ) );

		VRET( framework_gfxDevice->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( &m_computeRootSignature ) ) );
	}

	// Create the pipeline state, which includes compiling and loading shaders.
	{
		ComPtr<ID3DBlob> vertexShader;
		ComPtr<ID3DBlob> pixelShader;
		ComPtr<ID3DBlob> computeShader;

		UINT compileFlags = 0;
		D3D_SHADER_MACRO macro[] =
		{
			{"__hlsl",			"1"},
			{nullptr,		nullptr}
		};
		VRET( CompileShaderFromFile( GetAssetFullPath( _T( "VolumetricAnimation_shader.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "vsmain", "vs_5_0", compileFlags, 0, &vertexShader ) );
		VRET( CompileShaderFromFile( GetAssetFullPath( _T( "VolumetricAnimation_shader.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "psmain", "ps_5_0", compileFlags, 0, &pixelShader ) );
		VRET( CompileShaderFromFile( GetAssetFullPath( _T( "VolumetricAnimation_shader.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "csmain", "cs_5_0", compileFlags, 0, &computeShader ) );
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
		psoDesc.VS = { reinterpret_cast< UINT8* >( vertexShader->GetBufferPointer() ), vertexShader->GetBufferSize() };
		psoDesc.PS = { reinterpret_cast< UINT8* >( pixelShader->GetBufferPointer() ), pixelShader->GetBufferSize() };
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC( D3D12_DEFAULT );
		psoDesc.BlendState = CD3DX12_BLEND_DESC( D3D12_DEFAULT );
		psoDesc.DepthStencilState = depthStencilDesc;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleDesc.Count = 1;
		VRET( framework_gfxDevice->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( &m_pipelineState ) ) );
		DXDebugName( m_pipelineState );

		// Describe and create the compute pipeline state object (PSO).
		D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
		computePsoDesc.pRootSignature = m_computeRootSignature.Get();
		computePsoDesc.CS = { reinterpret_cast< UINT8* >( computeShader->GetBufferPointer() ), computeShader->GetBufferSize() };

		VRET( framework_gfxDevice->CreateComputePipelineState( &computePsoDesc, IID_PPV_ARGS( &m_computeState ) ) );
		DXDebugName( m_computeState );
	}

	// Create the compute command list.
	VRET( framework_gfxDevice->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_COMPUTE, m_computeCmdAllocator.Get(), m_computeState.Get(), IID_PPV_ARGS( &m_computeCmdList ) ) );
	DXDebugName( m_computeCmdList );

	VRET( m_computeCmdList->Close() );

	// Create the graphics command list.
	VRET( framework_gfxDevice->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_graphicCmdAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS( &m_graphicCmdList ) ) );
	DXDebugName( m_graphicCmdList );

	// Note: ComPtr's are CPU objects but this resource needs to stay in scope until
	// the command list that references it has finished executing on the GPU.
	// We will flush the GPU at the end of this method to ensure the resource is not
	// prematurely destroyed.
	ComPtr<ID3D12Resource> volumeBufferUploadHeap;

	// Create the volumeBuffer.
	{
		UINT volumeBufferSize = m_volumeDepth*m_volumeHeight*m_volumeWidth * 4 * sizeof( UINT8 );

		D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer( volumeBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );
		D3D12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer( volumeBufferSize );

		VRET( framework_gfxDevice->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ), D3D12_HEAP_FLAG_NONE,
												 &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &m_volumeBuffer ) ) );

		const UINT64 uploadBufferSize = GetRequiredIntermediateSize( m_volumeBuffer.Get(), 0, 1 );

		// Create the GPU upload buffer.
		VRET( framework_gfxDevice->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ), D3D12_HEAP_FLAG_NONE,
												 &uploadBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
												 nullptr, IID_PPV_ARGS( &volumeBufferUploadHeap ) ) );

		// Copy data to the intermediate upload heap and then schedule a copy 
		// from the upload heap to the Texture2D.
		UINT8* volumeBuffer = ( UINT8* ) malloc( volumeBufferSize );

		float a = m_volumeWidth / 2.f;
		float b = m_volumeHeight / 2.f;
		float c = m_volumeDepth / 2.f;
#if SPHERE_VOLUME_ANIMATION
		float radius = sqrt( a*a + b*b + c*c );
#else
		float radius = ( abs( a ) + abs( b ) + abs( c ) );
#endif
		XMINT4 bg = m_pConstantBufferData->bgCol;
		UINT bgMax = max( max( bg.x, bg.y ), bg.z );
		m_pConstantBufferData->bgCol.w = bgMax;
		for ( UINT z = 0; z < m_volumeDepth; z++ )
			for ( UINT y = 0; y < m_volumeHeight; y++ )
				for ( UINT x = 0; x < m_volumeWidth; x++ )
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
					UINT maxColCnt = 4;
					assert( maxColCnt < COLOR_COUNT );
					float currentScale = scale * maxColCnt + 0.1f;
					UINT idx = COLOR_COUNT - ( UINT ) ( currentScale ) -1;
					float intensity = currentScale - ( UINT ) currentScale;
					UINT col = ( UINT ) ( intensity * ( 255 - bgMax ) ) + 1;
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
		srvDesc.Buffer.StructureByteStride = 4 * sizeof( UINT8 );
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle( m_cbvsrvuavHeap->GetCPUDescriptorHandleForHeapStart(), RootParameterSRV, m_cbvsrvuavDescriptorSize );
		framework_gfxDevice->CreateShaderResourceView( m_volumeBuffer.Get(), &srvDesc, srvHandle );

		// Describe and create a UAV for the volumeBuffer.
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = m_volumeWidth*m_volumeHeight*m_volumeDepth;
		uavDesc.Buffer.StructureByteStride = 4 * sizeof( UINT8 );
		uavDesc.Buffer.CounterOffsetInBytes = 0;
		uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

		CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle( m_cbvsrvuavHeap->GetCPUDescriptorHandleForHeapStart(), RootParameterUAV, m_cbvsrvuavDescriptorSize );
		framework_gfxDevice->CreateUnorderedAccessView( m_volumeBuffer.Get(), nullptr, &uavDesc, uavHandle );
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

		const UINT vertexBufferSize = sizeof( cubeVertices );

		VRET( framework_gfxDevice->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ), D3D12_HEAP_FLAG_NONE,
												 &CD3DX12_RESOURCE_DESC::Buffer( vertexBufferSize ), D3D12_RESOURCE_STATE_GENERIC_READ,
												 nullptr, IID_PPV_ARGS( &vertexBufferUpload ) ) );
		VRET( framework_gfxDevice->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ), D3D12_HEAP_FLAG_NONE,
												 &CD3DX12_RESOURCE_DESC::Buffer( vertexBufferSize ), D3D12_RESOURCE_STATE_COPY_DEST,
												 nullptr, IID_PPV_ARGS( &m_vertexBuffer ) ) );
		DXDebugName( m_vertexBuffer );

		D3D12_SUBRESOURCE_DATA vertexData = {};
		vertexData.pData = reinterpret_cast< UINT8* >( cubeVertices );
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

		const UINT indexBufferSize = sizeof( cubeIndices );

		VRET( framework_gfxDevice->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ), D3D12_HEAP_FLAG_NONE,
												 &CD3DX12_RESOURCE_DESC::Buffer( indexBufferSize ), D3D12_RESOURCE_STATE_GENERIC_READ,
												 nullptr, IID_PPV_ARGS( &indexBufferUpload ) ) );
		VRET( framework_gfxDevice->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ), D3D12_HEAP_FLAG_NONE,
												 &CD3DX12_RESOURCE_DESC::Buffer( indexBufferSize ), D3D12_RESOURCE_STATE_COPY_DEST,
												 nullptr, IID_PPV_ARGS( &m_indexBuffer ) ) );
		DXDebugName( m_indexBuffer );

		D3D12_SUBRESOURCE_DATA indexData = {};
		indexData.pData = reinterpret_cast< UINT8* >( cubeIndices );
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
		VRET( framework_gfxDevice->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ), D3D12_HEAP_FLAG_NONE,
												 &CD3DX12_RESOURCE_DESC::Buffer( 1024 * 64 ), D3D12_RESOURCE_STATE_GENERIC_READ,
												 nullptr, IID_PPV_ARGS( &m_constantBuffer ) ) );
		DXDebugName( m_constantBuffer );

		// Describe and create a constant buffer view.
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = ( sizeof( ConstantBuffer ) + 255 ) & ~255;	// CB size is required to be 256-byte aligned.
		framework_gfxDevice->CreateConstantBufferView( &cbvDesc, m_cbvsrvuavHeap->GetCPUDescriptorHandleForHeapStart() );

		// Initialize and map the constant buffers. We don't unmap this until the
		// app closes. Keeping things mapped for the lifetime of the resource is okay.
		CD3DX12_RANGE readRange( 0, 0 );		// We do not intend to read from this resource on the CPU.
		VRET( m_constantBuffer->Map( 0, &readRange, reinterpret_cast< void** >( &m_pCbvDataBegin ) ) );
		memcpy( m_pCbvDataBegin, &m_pConstantBufferData, sizeof( m_pConstantBufferData ) );
	}

	// Close the command list and execute it to begin the initial GPU setup.
	VRET( m_graphicCmdList->Close() );
	ID3D12CommandList* ppCommandLists[] = { m_graphicCmdList.Get() };
	framework_gfxBackbufferGfxCmdQueue->ExecuteCommandLists( _countof( ppCommandLists ), ppCommandLists );

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		VRET( framework_gfxDevice->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &m_fence ) ) );
		DXDebugName( m_fence );
		m_fenceValue = 1;

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );
		if ( m_fenceEvent == nullptr )
		{
			VRET( HRESULT_FROM_WIN32( GetLastError() ) );
		}

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		WaitForGraphicsCmd();
	}

	ResetCameraView();

	return S_OK;
}

// Load size dependent resource
HRESULT VolumetricAnimation::LoadSizeDependentResource()
{
	HRESULT hr;

	uint32_t width = framework_config.swapChainDesc.BufferDesc.Width;
	uint32_t height = framework_config.swapChainDesc.BufferDesc.Height;

	// Create render target views (RTVs).
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle( m_rtvHeap->GetCPUDescriptorHandleForHeapStart() );
	for ( UINT i = 0; i < m_FrameCount; i++ )
	{
		VRET( framework_gfxSwapChain->GetBuffer( i, IID_PPV_ARGS( &m_renderTargets[i] ) ) );
		DXDebugName( m_renderTargets[i] );
		framework_gfxDevice->CreateRenderTargetView( m_renderTargets[i].Get(), nullptr, rtvHandle );
		rtvHandle.Offset( 1, m_rtvDescriptorSize );
	}

	// Create the depth stencil.
	{
		CD3DX12_RESOURCE_DESC shadowTextureDesc( D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, static_cast< UINT >( width ),
												 static_cast< UINT >( height ), 1, 1, DXGI_FORMAT_D32_FLOAT,
												 1, 0, D3D12_TEXTURE_LAYOUT_UNKNOWN,
												 D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE );

		D3D12_CLEAR_VALUE clearValue;	// Performance tip: Tell the runtime at resource creation the desired clear value.
		clearValue.Format = DXGI_FORMAT_D32_FLOAT;
		clearValue.DepthStencil.Depth = 1.0f;
		clearValue.DepthStencil.Stencil = 0;

		VRET( framework_gfxDevice->CreateCommittedResource( &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ), D3D12_HEAP_FLAG_NONE, &shadowTextureDesc,
												 D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS( &m_depthBuffer ) ) );
		DXDebugName( m_depthBuffer );

		// Create the depth stencil view.
		framework_gfxDevice->CreateDepthStencilView( m_depthBuffer.Get(), nullptr, m_dsvHeap->GetCPUDescriptorHandleForHeapStart() );
	}
	m_viewport.Width = static_cast< float >( width );
	m_viewport.Height = static_cast< float >( height );
	m_viewport.MaxDepth = 1.0f;

	m_scissorRect.right = static_cast< LONG >( width );
	m_scissorRect.bottom = static_cast< LONG >( height );

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
	uint32_t n = GPU_Profiler::GetTimingStr( 0, temp );
	GPU_Profiler::GetTimingStr( 1, temp + n );
	swprintf( strCustom, L"%hs", temp );
#endif
}

// Render the scene.
void VolumetricAnimation::OnRender()
{
	HRESULT hr;
	PopulateComputeCommandList();
	ID3D12CommandList* ppComputeCommandLists[] = { m_computeCmdList.Get() };
	m_computeCmdQueue->ExecuteCommandLists( _countof( ppComputeCommandLists ), ppComputeCommandLists );

	WaitForComputeCmd();

	// Record all the commands we need to render the scene into the command list.
	PopulateGraphicsCommandList();

	// Execute the command list.
	ID3D12CommandList* ppGraphicsCommandLists[] = { m_graphicCmdList.Get() };
	framework_gfxBackbufferGfxCmdQueue->ExecuteCommandLists( _countof( ppGraphicsCommandLists ), ppGraphicsCommandLists );

	// Present the frame.
	V( framework_gfxSwapChain->Present( framework_config.vsync ? 1 : 0, 0 ) );
	m_frameIndex = ( m_frameIndex + 1 ) % m_FrameCount;

	WaitForGraphicsCmd();

}

HRESULT VolumetricAnimation::OnSizeChanged()
{
	HRESULT hr;
	// Flush all current GPU commands.
	WaitForGraphicsCmd();
	// Release the resources holding references to the swap chain (requirement of
	// IDXGISwapChain::ResizeBuffers) and reset the frame fence values to the
	// current fence value.
	for ( UINT n = 0; n < m_FrameCount; n++ )
	{
		m_renderTargets[n].Reset();
	}

	// Resize the swap chain to the desired dimensions.
	VRET( ResizeBackBuffer() );

	m_depthBuffer.Reset();

	VRET( LoadSizeDependentResource() );

	// Reset the frame index to the current back buffer index.
	m_frameIndex = framework_gfxSwapChain->GetCurrentBackBufferIndex();
	return S_OK;
}


void VolumetricAnimation::OnDestroy()
{
	// Wait for the GPU to be done with all resources.
	WaitForGraphicsCmd();

	CloseHandle( m_fenceEvent );
}

bool VolumetricAnimation::OnEvent( MSG* msg )
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
					ScreenToClient( framework_hwnd, &p );
					RECT clientRect;
					GetClientRect( framework_hwnd, &clientRect );
					p.x = p.x * framework_config.swapChainDesc.BufferDesc.Width / ( clientRect.right - clientRect.left );
					p.y = p.y * framework_config.swapChainDesc.BufferDesc.Height / ( clientRect.bottom - clientRect.top );
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

void VolumetricAnimation::PopulateGraphicsCommandList()
{
	HRESULT hr;
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	V( m_graphicCmdAllocator->Reset() );

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	V( m_graphicCmdList->Reset( m_graphicCmdAllocator.Get(), m_pipelineState.Get() ) );

	{
		GPU_PROFILE( m_graphicCmdList.Get(), "Rendering" );
		XMMATRIX view = m_camera.View();
		XMMATRIX proj = m_camera.Projection();

		XMMATRIX world = XMMatrixIdentity();
		m_pConstantBufferData->invWorld = XMMatrixInverse( nullptr, world );
		m_pConstantBufferData->wvp = XMMatrixMultiply( XMMatrixMultiply( world, view ), proj );
		XMStoreFloat4( &m_pConstantBufferData->viewPos, m_camera.Eye() );

		memcpy( m_pCbvDataBegin, m_pConstantBufferData, sizeof( ConstantBuffer ) );

		// Set necessary state.
		m_graphicCmdList->SetGraphicsRootSignature( m_graphicsRootSignature.Get() );

		ID3D12DescriptorHeap* ppHeaps[] = { m_cbvsrvuavHeap.Get() };
		m_graphicCmdList->SetDescriptorHeaps( _countof( ppHeaps ), ppHeaps );

		CD3DX12_GPU_DESCRIPTOR_HANDLE cbvHandle( m_cbvsrvuavHeap->GetGPUDescriptorHandleForHeapStart(), RootParameterCBV, m_cbvsrvuavDescriptorSize );
		CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle( m_cbvsrvuavHeap->GetGPUDescriptorHandleForHeapStart(), RootParameterSRV, m_cbvsrvuavDescriptorSize );

		m_graphicCmdList->SetGraphicsRootDescriptorTable( RootParameterCBV, cbvHandle );
		m_graphicCmdList->SetGraphicsRootDescriptorTable( RootParameterSRV, srvHandle );

		m_graphicCmdList->RSSetViewports( 1, &m_viewport );
		m_graphicCmdList->RSSetScissorRects( 1, &m_scissorRect );

		// Indicate that the back buffer will be used as a render target.
		D3D12_RESOURCE_BARRIER resourceBarriersBefore[] = {
			CD3DX12_RESOURCE_BARRIER::Transition( m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET ),
			CD3DX12_RESOURCE_BARRIER::Transition( m_volumeBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE )
		};
		m_graphicCmdList->ResourceBarrier( 2, resourceBarriersBefore );

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle( m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize );
		m_graphicCmdList->OMSetRenderTargets( 1, &rtvHandle, FALSE, &m_dsvHeap->GetCPUDescriptorHandleForHeapStart() );

		// Record commands.
		const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
		m_graphicCmdList->ClearRenderTargetView( rtvHandle, clearColor, 0, nullptr );
		m_graphicCmdList->ClearDepthStencilView( m_dsvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr );
		m_graphicCmdList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		m_graphicCmdList->IASetVertexBuffers( 0, 1, &m_vertexBufferView );
		m_graphicCmdList->IASetIndexBuffer( &m_indexBufferView );
		m_graphicCmdList->DrawIndexedInstanced( 36, 1, 0, 0, 0 );

		// Indicate that the back buffer will now be used to present.
		D3D12_RESOURCE_BARRIER resourceBarriersAfter[] = {
			CD3DX12_RESOURCE_BARRIER::Transition( m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT ),
			CD3DX12_RESOURCE_BARRIER::Transition( m_volumeBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS )
		};
		m_graphicCmdList->ResourceBarrier( 2, resourceBarriersAfter );

	}

	V( m_graphicCmdList->Close() );
}

void VolumetricAnimation::PopulateComputeCommandList()
{
	HRESULT hr;
	V( m_computeCmdAllocator->Reset() );
	V( m_computeCmdList->Reset( m_computeCmdAllocator.Get(), m_computeState.Get() ) );
	{
		GPU_PROFILE( m_computeCmdList.Get(), "Processing" );
		m_computeCmdList->SetPipelineState( m_computeState.Get() );
		m_computeCmdList->SetComputeRootSignature( m_computeRootSignature.Get() );
		ID3D12DescriptorHeap* ppHeaps[] = { m_cbvsrvuavHeap.Get() };
		m_computeCmdList->SetDescriptorHeaps( _countof( ppHeaps ), ppHeaps );
		CD3DX12_GPU_DESCRIPTOR_HANDLE cbvHandle( m_cbvsrvuavHeap->GetGPUDescriptorHandleForHeapStart(), RootParameterCBV, m_cbvsrvuavDescriptorSize );
		CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandle( m_cbvsrvuavHeap->GetGPUDescriptorHandleForHeapStart(), RootParameterUAV, m_cbvsrvuavDescriptorSize );

		m_computeCmdList->SetComputeRootDescriptorTable( RootParameterCBV, cbvHandle );
		m_computeCmdList->SetComputeRootDescriptorTable( RootParameterUAV, uavHandle );
		m_computeCmdList->Dispatch( m_volumeWidth / THREAD_X, m_volumeHeight / THREAD_Y, m_volumeDepth / THREAD_Z );
	}
	m_computeCmdList->Close();
}

void VolumetricAnimation::WaitForGraphicsCmd()
{
	HRESULT hr;
	// WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
	// This is code implemented as such for simplicity. More advanced samples 
	// illustrate how to use fences for efficient resource usage.

	// Signal and increment the fence value.
	const UINT64 fence = m_fenceValue;
	V( framework_gfxBackbufferGfxCmdQueue->Signal( m_fence.Get(), fence ) );
	m_fenceValue++;

	// Wait until the previous frame is finished.
	if ( m_fence->GetCompletedValue() < fence )
	{
		V( m_fence->SetEventOnCompletion( fence, m_fenceEvent ) );
		WaitForSingleObject( m_fenceEvent, INFINITE );
	}
}

void VolumetricAnimation::WaitForComputeCmd()
{
	HRESULT hr;

	// Signal and increment the fence value.
	const UINT64 fence = m_fenceValue;
	V( m_computeCmdQueue->Signal( m_fence.Get(), fence ) );
	m_fenceValue++;

	// Wait until the previous frame is finished.
	if ( m_fence->GetCompletedValue() < fence )
	{
		V( m_fence->SetEventOnCompletion( fence, m_fenceEvent ) );
		WaitForSingleObject( m_fenceEvent, INFINITE );
	}
}