
#include "stdafx.h"
#include "RotatingCube.h"
#include "GPU_Profiler.h"

RotatingCube::RotatingCube( UINT width, UINT height, std::wstring name ) :
	DX12Framework( width, height, name ),m_frameIndex( 0 ),m_viewport(),m_scissorRect(),m_rtvDescriptorSize( 0 )
{}

void RotatingCube::ResetCameraView()
{
	auto center = XMVectorSet( 0.0f, 0.0f, 0.0f, 0.0f );
	auto radius = DEFAULT_ORBIT_RADIUS;
	auto minRadius = MIN_ORBIT_RADIUS;
	auto maxRadius = MAX_ORBIT_RADIUS;
	auto longAngle = 4.50f;
	auto latAngle = 1.45f;
	m_camera.View( center, radius, minRadius, maxRadius, longAngle, latAngle );
}


void RotatingCube::OnConfiguration( Settings* config )
{
	config->swapChainDesc.BufferCount = m_FrameCount;
}

HRESULT RotatingCube::OnInit( ID3D12Device* m_device )
{
	HRESULT hr;
	VRET(LoadPipeline());
	VRET(LoadAssets());
	VRET(LoadSizeDependentResource());
	return S_OK;
}

// Load the rendering pipeline dependencies.
HRESULT RotatingCube::LoadPipeline()
{
	HRESULT hr;
	
	m_frameIndex = framework_gfxSwapChain->GetCurrentBackBufferIndex();

	// Create descriptor heaps.
	{
		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = m_FrameCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		VRET( framework_gfxDevice->CreateDescriptorHeap( &rtvHeapDesc, IID_PPV_ARGS( &m_rtvHeap ) ) );
		DXDebugName( m_rtvHeap );

		m_rtvDescriptorSize = framework_gfxDevice->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );

		// Describe and create a depth stencil view (DSV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
		dsvHeapDesc.NumDescriptors = 1;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		VRET( framework_gfxDevice->CreateDescriptorHeap( &dsvHeapDesc, IID_PPV_ARGS( &m_dsvHeap ) ) );
		DXDebugName( m_dsvHeap );
	}

	VRET( framework_gfxDevice->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &m_commandAllocator ) ) );
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
		VRET( framework_gfxDevice->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( &m_rootSignature ) ) );
		DXDebugName( m_rootSignature );
	}

	// Create the pipeline state, which includes compiling and loading shaders.
	{
		ComPtr<ID3DBlob> vertexShader;
		ComPtr<ID3DBlob> pixelShader;

		UINT compileFlags = 0;

		VRET( CompileShaderFromFile( GetAssetFullPath( _T("RotatingCube_shader.hlsl" )).c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "vsmain", "vs_5_0", compileFlags, 0, &vertexShader ) );
		VRET( CompileShaderFromFile( GetAssetFullPath( _T("RotatingCube_shader.hlsl") ).c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "psmain", "ps_5_0", compileFlags, 0, &pixelShader ) );
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
		psoDesc.VS = { reinterpret_cast< UINT8* >( vertexShader->GetBufferPointer() ), vertexShader->GetBufferSize() };
		psoDesc.PS = { reinterpret_cast< UINT8* >( pixelShader->GetBufferPointer() ), pixelShader->GetBufferSize() };
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC( D3D12_DEFAULT );
		psoDesc.BlendState = CD3DX12_BLEND_DESC( D3D12_DEFAULT );
		psoDesc.DepthStencilState = depthStencilDesc;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleDesc.Count = 1;
		VRET( framework_gfxDevice->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( &m_pipelineState ) ) );
		DXDebugName( m_pipelineState );
	}

	// Create the command list.
	VRET( framework_gfxDevice->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS( &m_commandList ) ) );
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

		const UINT vertexBufferSize = sizeof( cubeVertices );

		// Note: using upload heaps to transfer static data like vert buffers is not 
		// recommended. Every time the GPU needs it, the upload heap will be marshalled 
		// over. Please read up on Default Heap usage. An upload heap is used here for 
		// code simplicity and because there are very few verts to actually transfer.
		VRET( framework_gfxDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),D3D12_HEAP_FLAG_NONE,
														  &CD3DX12_RESOURCE_DESC::Buffer( vertexBufferSize ),D3D12_RESOURCE_STATE_GENERIC_READ,
														  nullptr,IID_PPV_ARGS( &m_vertexBuffer ) ) );
		DXDebugName( m_vertexBuffer );

		// Copy the triangle data to the vertex buffer.
		UINT8* pVertexDataBegin;
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

		const UINT indexBufferSize = sizeof( cubeIndices );
		VRET( framework_gfxDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),D3D12_HEAP_FLAG_NONE,
														  &CD3DX12_RESOURCE_DESC::Buffer( indexBufferSize ),D3D12_RESOURCE_STATE_GENERIC_READ,
														  nullptr,IID_PPV_ARGS( &m_indexBuffer ) ) );
		DXDebugName( m_indexBuffer );

		UINT8* pIndexDataBegin;
		VRET( m_indexBuffer->Map( 0, nullptr, reinterpret_cast< void** >( &pIndexDataBegin ) ) );
		memcpy( pIndexDataBegin, cubeIndices, sizeof( cubeIndices ) );
		m_indexBuffer->Unmap( 0, nullptr );

		m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
		m_indexBufferView.SizeInBytes = sizeof( cubeIndices );
		m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
	}

	// Create the constant buffer
	{
		VRET( framework_gfxDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),D3D12_HEAP_FLAG_NONE,
														  &CD3DX12_RESOURCE_DESC::Buffer( sizeof( XMFLOAT4X4 ) ),D3D12_RESOURCE_STATE_GENERIC_READ,
														  nullptr,IID_PPV_ARGS( &m_constantBuffer ) ) );
		DXDebugName( m_constantBuffer );

		UINT8* pConstantBufferBegin;
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
		VRET( framework_gfxDevice->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &m_fence ) ) );
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
		CD3DX12_RESOURCE_DESC shadowTextureDesc(D3D12_RESOURCE_DIMENSION_TEXTURE2D,0,static_cast< UINT >( width ),static_cast< UINT >( height ),
												 1,1,DXGI_FORMAT_D32_FLOAT,1,0,D3D12_TEXTURE_LAYOUT_UNKNOWN,
												 D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE );

		D3D12_CLEAR_VALUE clearValue;	// Performance tip: Tell the runtime at resource creation the desired clear value.
		clearValue.Format = DXGI_FORMAT_D32_FLOAT;
		clearValue.DepthStencil.Depth = 1.0f;
		clearValue.DepthStencil.Stencil = 0;

		VRET( framework_gfxDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),D3D12_HEAP_FLAG_NONE,&shadowTextureDesc,
														  D3D12_RESOURCE_STATE_DEPTH_WRITE,&clearValue,IID_PPV_ARGS( &m_depthBuffer ) ) );
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
void RotatingCube::OnUpdate()
{
	m_timer.Tick( NULL );
	m_camera.ProcessInertia();
	// Temporary procedural for display GPU timing, should be removed once
	// GPU_Profiler::Draw() have been implemented
#ifndef RELEASE
	char temp[128];
	uint32_t n = GPU_Profiler::GetTimingStr( 0, temp );
	swprintf( strCustom, L"%hs", temp );
#endif
}

// Render the scene.
void RotatingCube::OnRender()
{
	HRESULT hr;
	// Record all the commands we need to render the scene into the command list.
	PopulateCommandList();

	// Execute the command list.
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	framework_gfxBackbufferGfxCmdQueue->ExecuteCommandLists( _countof( ppCommandLists ), ppCommandLists );

	// Present the frame.
	V( framework_gfxSwapChain->Present( framework_config.vsync ? 1 : 0, 0 ) );
	m_frameIndex = ( m_frameIndex + 1 ) % m_FrameCount;

	WaitForPreviousFrame();
}

HRESULT RotatingCube::OnSizeChanged()
{
	HRESULT hr;
	// Flush all current GPU commands.
	WaitForPreviousFrame();

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

	VRET(LoadSizeDependentResource());

	// Reset the frame index to the current back buffer index.
	m_frameIndex = framework_gfxSwapChain->GetCurrentBackBufferIndex();
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
		m_commandList->RSSetViewports( 1, &m_viewport );
		m_commandList->RSSetScissorRects( 1, &m_scissorRect );

		// Indicate that the back buffer will be used as a render target.
		m_commandList->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET ) );

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle( m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize );
		m_commandList->OMSetRenderTargets( 1, &rtvHandle, FALSE, &m_dsvHeap->GetCPUDescriptorHandleForHeapStart() );

		// Record commands.
		const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
		m_commandList->ClearRenderTargetView( rtvHandle, clearColor, 0, nullptr );
		m_commandList->ClearDepthStencilView( m_dsvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr );
		m_commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		m_commandList->IASetVertexBuffers( 0, 1, &m_vertexBufferView );
		m_commandList->IASetIndexBuffer( &m_indexBufferView );
		m_commandList->DrawIndexedInstanced( 36, 1, 0, 0, 0 );

		// Indicate that the back buffer will now be used to present.
		m_commandList->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT ) );
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
	const UINT64 fence = m_fenceValue;
	V( framework_gfxBackbufferGfxCmdQueue->Signal( m_fence.Get(), fence ) );
	m_fenceValue++;

	// Wait until the previous frame is finished.
	if ( m_fence->GetCompletedValue() < fence )
	{
		V( m_fence->SetEventOnCompletion( fence, m_fenceEvent ) );
		WaitForSingleObject( m_fenceEvent, INFINITE );
	}

	m_frameIndex = framework_gfxSwapChain->GetCurrentBackBufferIndex();
}
