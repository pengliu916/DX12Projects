#include "stdafx.h"
#include "RotatingCube.h"

RotatingCube::RotatingCube( uint32_t width, uint32_t height, std::wstring name ) :
	m_DepthBuffer()
{
	m_width = width;
	m_height = height;

	m_camOrbitRadius = 10.f;
	m_camMaxOribtRadius = 100.f;
	m_camMinOribtRadius = 2.f;
	cbufferData = new CBuffer();
}

RotatingCube::~RotatingCube()
{
	delete cbufferData;
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

void RotatingCube::OnInit()
{
}

HRESULT RotatingCube::OnCreateResource()
{
	HRESULT hr;
	VRET( LoadSizeDependentResource() );
	VRET( LoadAssets() );
	return S_OK;
}

// Load the sample assets.
HRESULT RotatingCube::LoadAssets()
{
	HRESULT	hr;
	// Create an empty root signature.
	m_RootSignature.Reset( 1 );
	m_RootSignature[0].InitAsConstantBuffer( 0 );
	m_RootSignature.Finalize( D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT );

	// Create the pipeline state, which includes compiling and loading shaders.
	m_GraphicsPSO.SetRootSignature( m_RootSignature );

	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;

	uint32_t compileFlags = 0;

	VRET( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "RotatingCube_shader.hlsl" ) ).c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "vsmain", "vs_5_0", compileFlags, 0, &vertexShader ) );
	VRET( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "RotatingCube_shader.hlsl" ) ).c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "psmain", "ps_5_0", compileFlags, 0, &pixelShader ) );

	m_GraphicsPSO.SetVertexShader( vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() );
	m_GraphicsPSO.SetPixelShader( pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() );
	// Define the vertex input layout.
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	m_GraphicsPSO.SetInputLayout( _countof( inputElementDescs ), inputElementDescs );
	m_GraphicsPSO.SetRasterizerState( Graphics::g_RasterizerDefaultCW );
	m_GraphicsPSO.SetBlendState( Graphics::g_BlendDisable );
	m_GraphicsPSO.SetDepthStencilState( Graphics::g_DepthStateReadWrite );
	m_GraphicsPSO.SetSampleMask( UINT_MAX );
	m_GraphicsPSO.SetPrimitiveTopologyType( D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE );
	DXGI_FORMAT ColorFormat = Graphics::g_pDisplayPlanes[0].GetFormat();
	DXGI_FORMAT DepthFormat = m_DepthBuffer.GetFormat();
	m_GraphicsPSO.SetRenderTargetFormats( 1, &ColorFormat, DepthFormat );

	m_GraphicsPSO.Finalize();

	// Create the vertex buffer.
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

	m_VertexBuffer.Create( L"Vertex Buffer", ARRAYSIZE( cubeVertices ), 2 * sizeof( XMFLOAT3 ), (void*)cubeVertices );

	uint16_t cubeIndices[] =
	{
		0,2,1, 1,2,3,  4,5,6, 5,7,6,  0,1,5, 0,5,4,  2,6,7, 2,7,3,  0,4,6, 0,6,2,  1,3,7, 1,7,5,
	};

	m_IndexBuffer.Create( L"Index Buffer", ARRAYSIZE( cubeIndices ), sizeof( uint16_t ), (void*)cubeIndices );

	ResetCameraView();
	return S_OK;
}

// Load size dependent resource
HRESULT RotatingCube::LoadSizeDependentResource()
{
	uint32_t width = Core::g_config.swapChainDesc.Width;
	uint32_t height = Core::g_config.swapChainDesc.Height;

	m_DepthBuffer.Create( L"Depth Buffer", width, height, DXGI_FORMAT_D32_FLOAT );

	float fAspectRatio = width / (FLOAT)height;
	m_camera.Projection( XM_PIDIV2 / 2, fAspectRatio );
	return S_OK;
}

// Update frame-based values.
void RotatingCube::OnUpdate()
{
	m_timer.Tick( NULL );
	m_camera.ProcessInertia();
}

// Render the scene.
void RotatingCube::OnRender( CommandContext& EngineContext )
{
	// Record all the commands we need to render the scene into the command list.
	XMMATRIX view = m_camera.View();
	XMMATRIX proj = m_camera.Projection();

	XMMATRIX world = XMMatrixRotationY( static_cast<float>(m_timer.GetTotalSeconds()) );
	//XMMATRIX wvp = XMMatrixMultiply(XMMatrixMultiply(world, view), proj);

	cbufferData->invWorld = XMMatrixInverse( nullptr, world );
	cbufferData->wvp = XMMatrixMultiply( XMMatrixMultiply( world, view ), proj );
	XMStoreFloat4( &cbufferData->viewPos, m_camera.Eye() );


	GraphicsContext& gfxContext = GraphicsContext::Begin( L"Render" );
	{
		GPU_PROFILE( gfxContext, L"Render" );
		gfxContext.ClearColor( Graphics::g_pDisplayPlanes[Graphics::g_CurrentDPIdx] );
		gfxContext.ClearDepth( m_DepthBuffer );
		gfxContext.SetRootSignature( m_RootSignature );
		gfxContext.SetPipelineState( m_GraphicsPSO );
		gfxContext.SetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		gfxContext.SetDynamicConstantBufferView( 0, sizeof( CBuffer ), (void*)(cbufferData) );
		gfxContext.SetRenderTargets( 1, &Graphics::g_pDisplayPlanes[Graphics::g_CurrentDPIdx], &m_DepthBuffer );
		gfxContext.SetViewport( Graphics::g_DisplayPlaneViewPort );
		gfxContext.SetScisor( Graphics::g_DisplayPlaneScissorRect );
		gfxContext.SetVertexBuffer( 0, m_VertexBuffer.VertexBufferView() );
		gfxContext.SetIndexBuffer( m_IndexBuffer.IndexBufferView() );
		gfxContext.DrawIndexed( 36 );
	}

	GPU_Profiler::DrawStats( gfxContext );

	gfxContext.Finish();
}

HRESULT RotatingCube::OnSizeChanged()
{
	HRESULT hr;
	m_DepthBuffer.Destroy();
	VRET( LoadSizeDependentResource() );
	return S_OK;
}


void RotatingCube::OnDestroy()
{
	// Wait for the GPU to be done with all resources.
}

bool RotatingCube::OnEvent( MSG* msg )
{
	/*if (ImGui_ImplDX12_WndProcHandler(Core::g_hwnd, msg->message, msg->wParam, msg->lParam))
		return false;*/

	switch (msg->message)
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
		if (GetPointerInfo( pointerId, &pointerInfo )) {
			if (msg->message == WM_POINTERDOWN) {
				// Compute pointer position in render units
				POINT p = pointerInfo.ptPixelLocation;
				ScreenToClient( Core::g_hwnd, &p );
				RECT clientRect;
				GetClientRect( Core::g_hwnd, &clientRect );
				p.x = p.x * Core::g_config.swapChainDesc.Width / (clientRect.right - clientRect.left);
				p.y = p.y * Core::g_config.swapChainDesc.Height / (clientRect.bottom - clientRect.top);
				// Camera manipulation
				m_camera.AddPointer( pointerId );
			}
		}

		// Otherwise send it to the camera controls
		m_camera.ProcessPointerFrames( pointerId, &pointerInfo );
		if (msg->message == WM_POINTERUP) m_camera.RemovePointer( pointerId );
	}
	}
	return false;
}