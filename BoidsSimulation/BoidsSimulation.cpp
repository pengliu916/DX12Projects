#include "stdafx.h"
#include "BoidsSimulation.h"
#include <ppl.h>

struct FishVertex { float Pos3Tex2[5]; };
FishVertex FishMesh[] =
{
	{-0.3f, 0.0f, -0.3f, 0.0f, 0.0f},
	{1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
	{-0.3f, 0.0f, -0.3f, 0.25f, 0.25f},
	{-1.0f, 1.0f, 0.0f, 0.5f, 0.0f},
	{-0.3f, 0.0f, 0.3f, 0.75f, 0.25},
	{1.0f, 0.0f, 0.0f, 1.0f, 0.0f},
	{-0.3f, 0.0f, 0.3f, 0.75f, 0.25f},
	{-1.0f, -1.0f, 0.0f, 1.0f, 0.5f},
	{-0.3f, 0.0f, -0.3f, 0.75f, 0.75f},
	{1.0f, 0.0f, 0.0f, 1.0f, 1.0f},
};


FishData* BoidsSimulation::CreateInitialFishData()
{
	FishData* pFishData = new FishData[m_SimulationCB.uNumInstance];
	if (!pFishData) return nullptr;

	float clusterScale = 0.2f;			// Cluster radius in meters
	float velFactor = 1.f;

	for (uint32_t i = 0; i < m_SimulationCB.uNumInstance; ++i)
	{
		pFishData[i].pos.x = (rand() / (float)RAND_MAX * 2 - 1) * m_SimulationCB.f3xyzExpand.x * clusterScale + m_SimulationCB.f3CenterPos.x;
		pFishData[i].pos.y = (rand() / (float)RAND_MAX * 2 - 1) * m_SimulationCB.f3xyzExpand.y * clusterScale + m_SimulationCB.f3CenterPos.y;
		pFishData[i].pos.z = (rand() / (float)RAND_MAX * 2 - 1) * m_SimulationCB.f3xyzExpand.z * clusterScale + m_SimulationCB.f3CenterPos.z;
		pFishData[i].vel.x = (rand() / (float)RAND_MAX * 2 - 1) * velFactor;				  
		pFishData[i].vel.y = (rand() / (float)RAND_MAX * 2 - 1) * velFactor;
		pFishData[i].vel.z = (rand() / (float)RAND_MAX * 2 - 1) * velFactor;
	};
	return pFishData;
}

BoidsSimulation::BoidsSimulation( uint32_t width, uint32_t height )
	:m_Allocator( kCpuWritable )
{
	m_width = width;
	m_height = height;

	m_SimulationCB.fAvoidanceFactor = 8.0f;					// Strength of avoidance force
	m_SimulationCB.fSeperationFactor = 0.4f;				// Strength of separation force
	m_SimulationCB.fCohesionFactor = 15.f;					// Strength of cohesion force
	m_SimulationCB.fAlignmentFactor = 12.f;					// Strength of alignment force
	m_SimulationCB.fSeekingFactor = 0.2f;					// Strength of seeking force
	m_SimulationCB.f3SeekSourcePos = float3( 0.f, 0.f, 0.f );
	m_SimulationCB.fFleeFactor = 0.f;						// Strength of flee force
	m_SimulationCB.f3FleeSourcePos = float3( 0.f, 0.f, 0.f );
	m_SimulationCB.fMaxForce = 200.0f;						// Maximum power a fish could offer
	m_SimulationCB.f3CenterPos = float3( 0.f, 0.f, 0.f );
	m_SimulationCB.fMaxSpeed = 20.0f;						// m/s maximum speed a fish could run
	m_SimulationCB.f3xyzExpand = float3( 60.f, 30.f, 60.f );
	m_SimulationCB.fMinSpeed = 2.5f;						// m/s minimum speed a fish have to maintain
	m_SimulationCB.fVisionDist = 3.5f;						// Neighbor dist threshold
	m_SimulationCB.fVisionAngleCos = -0.6f;					// Neighbor angle(cos) threshold
	m_SimulationCB.fDeltaT = 0.01f;							// seconds of simulation interval
	m_SimulationCB.uNumInstance = 10000;
	m_SimulationCB.fFishSize = 0.3f;
}

BoidsSimulation::~BoidsSimulation()
{
}

void BoidsSimulation::ResetCameraView()
{
	auto center = XMVectorSet( 0.0f, 0.0f, 0.0f, 0.0f );
	auto radius = m_camOrbitRadius;
	auto minRadius = m_camMinOribtRadius;
	auto maxRadius = m_camMaxOribtRadius;
	auto longAngle = 0.f;
	auto latAngle = DirectX::XM_PIDIV2;
	m_camera.View( center, radius, minRadius, maxRadius, longAngle, latAngle );
}


void BoidsSimulation::OnConfiguration()
{
	Core::g_config.swapChainDesc.BufferCount = 5;
	Core::g_config.swapChainDesc.Width = m_width;
	Core::g_config.swapChainDesc.Height = m_height;
}

void BoidsSimulation::OnInit()
{
}

HRESULT BoidsSimulation::OnCreateResource()
{
	HRESULT hr;
	VRET( LoadSizeDependentResource() );
	VRET( LoadAssets() );
	return S_OK;
}

// Load the sample assets.
HRESULT BoidsSimulation::LoadAssets()
{
	HRESULT	hr;
	// Create the root signature for both rendering and simulation.
	m_RootSignature.Reset( 5, 1 );
	m_RootSignature[0].InitAsConstantBuffer( 0 );
	m_RootSignature[1].InitAsConstantBuffer( 1 );
	m_RootSignature[2].InitAsBufferSRV( 0 );
	m_RootSignature[3].InitAsBufferUAV( 0 );
	m_RootSignature[4].InitAsDescriptorRange( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2 );
	m_RootSignature.InitStaticSampler( 0, Graphics::g_SamplerLinearWrapDesc );
	m_RootSignature.Finalize( D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS );

	// Create the pipeline state, which includes compiling and loading shaders.
	m_GraphicsPSO.SetRootSignature( m_RootSignature );
	m_ComputePSO.SetRootSignature( m_RootSignature );

	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> geometryShader;
	ComPtr<ID3DBlob> pixelShader;
	ComPtr<ID3DBlob> computeShader;

	uint32_t compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
	D3D_SHADER_MACRO macro[] =
	{
		{	"__hlsl"			,	"1"		},
		{	"COMPUTE_SHADER"	,	"0"		},
		{	"GRAPHICS_SHADER"	,	"1"		},
		{	nullptr				,	nullptr	}
	};
	VRET( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "BoidsSimulation_shader.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "vsmain", "vs_5_1", compileFlags, 0, &vertexShader ) );
	VRET( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "BoidsSimulation_shader.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "gsmain", "gs_5_1", compileFlags, 0, &geometryShader ) );
	VRET( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "BoidsSimulation_shader.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "psmain", "ps_5_1", compileFlags, 0, &pixelShader ) );

	macro[1].Definition = "1";
	macro[2].Definition = "0";
	VRET( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "BoidsSimulation_shader.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "csmain", "cs_5_1", compileFlags, 0, &computeShader ) );

	m_GraphicsPSO.SetVertexShader( vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() );
	m_GraphicsPSO.SetGeometryShader( geometryShader->GetBufferPointer(), geometryShader->GetBufferSize() );
	m_GraphicsPSO.SetPixelShader( pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() );
	m_ComputePSO.SetComputeShader( computeShader->GetBufferPointer(), computeShader->GetBufferSize() );

	// Define the vertex input layout.
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	m_GraphicsPSO.SetInputLayout( _countof( inputElementDescs ), inputElementDescs );
	m_GraphicsPSO.SetRasterizerState( Graphics::g_RasterizerDefaultCW );
	m_GraphicsPSO.SetBlendState( Graphics::g_BlendDisable );
	m_GraphicsPSO.SetDepthStencilState( Graphics::g_DepthStateReadWrite );
	m_GraphicsPSO.SetSampleMask( UINT_MAX );
	m_GraphicsPSO.SetPrimitiveTopologyType( D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE );
	DXGI_FORMAT ColorFormat = Graphics::g_SceneColorBuffer.GetFormat();
	DXGI_FORMAT DepthFormat = Graphics::g_SceneDepthBuffer.GetFormat();
	m_GraphicsPSO.SetRenderTargetFormats( 1, &ColorFormat, DepthFormat );

	m_GraphicsPSO.Finalize();
	m_ComputePSO.Finalize();

	// Create the buffer resource used in rendering and simulation
	FishData* pFishData = CreateInitialFishData();
	m_BoidsPosVelBuffer[0].Create( L"BoidsPosVolBuffer[0]", m_SimulationCB.uNumInstance, sizeof( FishData ), (void*)pFishData );
	m_BoidsPosVelBuffer[1].Create( L"BoidsPosVolBuffer[1]", m_SimulationCB.uNumInstance, sizeof( FishData ), (void*)pFishData );
	delete pFishData;

	// Define and create vertex buffer for fish
	m_VertexBuffer.Create( L"FishVertexBuffer", _countof( FishMesh ), sizeof( FishVertex ), (void*)FishMesh );

	// Create constant buffer
	m_pConstantBuffer = new DynAlloc( std::move( m_Allocator.Allocate( sizeof( SimulationCB ) ) ) );
	memcpy( m_pConstantBuffer->DataPtr, &m_SimulationCB, sizeof( SimulationCB ) );

	// Load color map texture from file
	ASSERT(m_ColorMapTex.CreateFromFIle( L"colorMap.dds", true ));

	ResetCameraView();
	return S_OK;
}

// Load size dependent resource
HRESULT BoidsSimulation::LoadSizeDependentResource()
{
	uint32_t width = Core::g_config.swapChainDesc.Width;
	uint32_t height = Core::g_config.swapChainDesc.Height;

	float fAspectRatio = width / (FLOAT)height;
	m_camera.Projection( XM_PIDIV2 / 2, fAspectRatio );
	return S_OK;
}

// Update frame-based values.
void BoidsSimulation::OnUpdate()
{
	m_camera.ProcessInertia();
	static bool showPanel = true;
	bool valueChanged = false;
	if (ImGui::Begin( "BoidsSimulation", &showPanel ))
	{
		ImGui::Checkbox( "Separate Context", &m_SeperateContext);
		ImGui::Checkbox( "PerFrame Simulation", &m_ForcePerFrameSimulation ); ImGui::SameLine();
		static char pauseSim[] = "Pause Simulation";
		static char continueSim[] = "Continue Simulation";
		static bool clicked;
		char* buttonTex = m_PauseSimulation ? continueSim : pauseSim;
		if (clicked = ImGui::Button( buttonTex )) m_PauseSimulation = !m_PauseSimulation;
		valueChanged = ImGui::SliderFloat( "SimulationDelta", &m_SimulationDelta, 0.01f, 0.05f ); m_NeedUpdate = (m_NeedUpdate || valueChanged);
		ImGui::Separator();
		valueChanged = ImGui::SliderFloat( "FishSize", &m_SimulationCB.fFishSize, 0.1f, 10.f, "%.2f" ); m_NeedUpdate = (m_NeedUpdate || valueChanged);
		valueChanged = ImGui::SliderFloat( "VisionDist", &m_SimulationCB.fVisionDist, 1.f, 20.f, "%.2f" ); m_NeedUpdate = (m_NeedUpdate || valueChanged);
		valueChanged = ImGui::SliderFloat( "VisionAngleCos", &m_SimulationCB.fVisionAngleCos, -1.f, 0.5f, "%.2f" ); m_NeedUpdate = (m_NeedUpdate || valueChanged);
		valueChanged = ImGui::SliderFloat( "AvoidanceForce", &m_SimulationCB.fAvoidanceFactor, 0.f, 10.f, "%.2f" ); m_NeedUpdate = (m_NeedUpdate || valueChanged);
		valueChanged = ImGui::SliderFloat( "SeparationForce", &m_SimulationCB.fSeperationFactor, 0.f, 10.f, "%.2f" ); m_NeedUpdate = (m_NeedUpdate || valueChanged);
		valueChanged = ImGui::SliderFloat( "CohesionForce", &m_SimulationCB.fCohesionFactor, 0.f, 20.f, "%.2f" ); m_NeedUpdate = (m_NeedUpdate || valueChanged);
		valueChanged = ImGui::SliderFloat( "AlignmentForce", &m_SimulationCB.fAlignmentFactor, 0.f, 20.f, "%.2f" ); m_NeedUpdate = (m_NeedUpdate || valueChanged);
		valueChanged = ImGui::SliderFloat( "SeekingForce", &m_SimulationCB.fSeekingFactor, 0.f, 5.f, "%.2f" ); m_NeedUpdate = (m_NeedUpdate || valueChanged);
		valueChanged = ImGui::SliderFloat( "FleeForce", &m_SimulationCB.fFleeFactor, 0.f, 100.f, "%.2f" ); m_NeedUpdate = (m_NeedUpdate || valueChanged);
		valueChanged = ImGui::SliderFloat( "MaxForce", &m_SimulationCB.fMaxForce, 1.f, 500.f, "%.2f" ); m_NeedUpdate = (m_NeedUpdate || valueChanged);
		valueChanged = ImGui::DragFloatRange2( "SpeedRange", &m_SimulationCB.fMinSpeed, &m_SimulationCB.fMaxSpeed, 0.002f, 0.f, 50.f, "Min:%.2f", "Max:%.2f" ); m_NeedUpdate = (m_NeedUpdate || valueChanged);
	}
	ImGui::End();
}

// Render the scene.
void BoidsSimulation::OnRender( CommandContext& EngineContext )
{
	float deltaTime = Core::g_deltaTime > m_SimulationMaxDelta ? m_SimulationMaxDelta : (float)Core::g_deltaTime;
	m_SimulationTimer += deltaTime;
	uint16_t SimulationCnt = (uint16_t)(m_SimulationTimer/m_SimulationDelta);
	m_SimulationTimer -= SimulationCnt * m_SimulationDelta;

	if (m_ForcePerFrameSimulation) 
	{
		SimulationCnt = 1;
		m_SimulationCB.fDeltaT = deltaTime<=0? m_SimulationDelta : deltaTime;
		m_NeedUpdate = true;
	}
	else if( m_SimulationCB.fDeltaT != m_SimulationDelta)
	{
		m_SimulationCB.fDeltaT = m_SimulationDelta;
		m_NeedUpdate = true;
	}

	wchar_t timerName[32];
	if (m_PauseSimulation) SimulationCnt = 0;
	for (int i = 0; i < SimulationCnt; ++i)
	{
		swprintf( timerName, L"Simulation %d", i );
		ComputeContext& cptContext = m_SeperateContext? ComputeContext::Begin(L"Simulating"): EngineContext.GetComputeContext();
		{
			GPU_PROFILE( cptContext, timerName );
			cptContext.SetRootSignature( m_RootSignature );
			cptContext.SetPipelineState( m_ComputePSO );
			cptContext.TransitionResource( m_BoidsPosVelBuffer[m_OnStageBufIdx], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
			cptContext.TransitionResource( m_BoidsPosVelBuffer[1 - m_OnStageBufIdx], D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
			if (m_NeedUpdate)
			{
				m_NeedUpdate = false;
				memcpy( m_pConstantBuffer->DataPtr, &m_SimulationCB, sizeof( SimulationCB ) );
			}
			cptContext.SetConstantBuffer( 1, m_pConstantBuffer->GpuAddress );
			cptContext.SetBufferSRV( 2, m_BoidsPosVelBuffer[m_OnStageBufIdx] );
			cptContext.SetBufferUAV( 3, m_BoidsPosVelBuffer[1 - m_OnStageBufIdx] );
			cptContext.Dispatch1D( m_SimulationCB.uNumInstance, BLOCK_SIZE );
			m_OnStageBufIdx = 1 - m_OnStageBufIdx;
			cptContext.BeginResourceTransition( m_BoidsPosVelBuffer[1-m_OnStageBufIdx], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
			cptContext.BeginResourceTransition( m_BoidsPosVelBuffer[m_OnStageBufIdx], D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
		}
		if (m_SeperateContext) cptContext.Finish();
	}

	// Record all the commands we need to render the scene into the command list.
	XMMATRIX view = m_camera.View();
	XMMATRIX proj = m_camera.Projection();

	// [NOTE]: vectors matrix in shader is column major in default, but on CPU side they are in row major as default. 
	// So for saving cpu cycles to do transpose, we change the order of matrix multiplication here. Otherwise, 
	// We should do m_renderCB.mWorldViewProj = XMMatrixTranspose(XMMatricMultiply(proj,view))
	m_RenderCB.mWorldViewProj = XMMatrixMultiply( view, proj );

	GraphicsContext& gfxContext = m_SeperateContext? GraphicsContext::Begin(L"Rendering") : EngineContext.GetGraphicsContext();
	{
		GPU_PROFILE( gfxContext, L"Render" );
		gfxContext.TransitionResource( m_BoidsPosVelBuffer[m_OnStageBufIdx], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
		gfxContext.ClearColor( Graphics::g_SceneColorBuffer );
		gfxContext.ClearDepth( Graphics::g_SceneDepthBuffer );
		gfxContext.SetRootSignature( m_RootSignature );
		gfxContext.SetPipelineState( m_GraphicsPSO );
		gfxContext.SetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
		if (m_NeedUpdate)
		{
			m_NeedUpdate = false;
			memcpy( m_pConstantBuffer->DataPtr, &m_SimulationCB, sizeof( SimulationCB ) );
		}
		gfxContext.SetConstantBuffer( 1, m_pConstantBuffer->GpuAddress );
		gfxContext.SetDynamicConstantBufferView( 0, sizeof( RenderCB ), (void*)(&m_RenderCB) );
		gfxContext.SetBufferSRV( 2, m_BoidsPosVelBuffer[m_OnStageBufIdx] );
		gfxContext.SetDynamicDescriptors( 4, 0, 1, &m_ColorMapTex.GetSRV() );
		gfxContext.SetRenderTargets( 1, &Graphics::g_SceneColorBuffer, &Graphics::g_SceneDepthBuffer );
		gfxContext.SetViewport( Graphics::g_DisplayPlaneViewPort );
		gfxContext.SetScisor( Graphics::g_DisplayPlaneScissorRect );
		gfxContext.SetVertexBuffer( 0, m_VertexBuffer.VertexBufferView() );
		gfxContext.DrawInstanced( _countof( FishMesh ), m_SimulationCB.uNumInstance );
		gfxContext.BeginResourceTransition( m_BoidsPosVelBuffer[m_OnStageBufIdx], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
		gfxContext.BeginResourceTransition( m_BoidsPosVelBuffer[1 - m_OnStageBufIdx], D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
	}
	if (m_SeperateContext) gfxContext.Finish();
}

HRESULT BoidsSimulation::OnSizeChanged()
{
	HRESULT hr;
	VRET( LoadSizeDependentResource() );
	return S_OK;
}

void BoidsSimulation::OnDestroy()
{
}

bool BoidsSimulation::OnEvent( MSG* msg )
{
	switch (msg->message)
	{
	case WM_MOUSEWHEEL:
	{
		auto delta = GET_WHEEL_DELTA_WPARAM( msg->wParam );
		m_camera.ZoomRadius( -0.1f*delta );
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