
#include "stdafx.h"
#include "VolumetricAnimation.h"

#include "VolumetricAnimation_SharedHeader.inl"

VolumetricAnimation::VolumetricAnimation(uint32_t width, uint32_t height, std::wstring name) :
	m_DepthBuffer()
{
	m_OneContext = false;
	m_volumeWidth = VOLUME_SIZE_X;
	m_volumeHeight = VOLUME_SIZE_Y;
	m_volumeDepth = VOLUME_SIZE_Z;

	m_pConstantBufferData = new ConstantBuffer();
	m_pConstantBufferData->bgCol = XMINT4(32, 32, 32, 32);

	m_width = width;
	m_height = height;

	m_camOrbitRadius = 10.f;
	m_camMaxOribtRadius = 100.f;
	m_camMinOribtRadius = 2.f;

#if !STATIC_ARRAY
	for (uint32_t i = 0; i < ARRAY_COUNT(shiftingColVals); i++)
		m_pConstantBufferData->shiftingColVals[i] = shiftingColVals[i];
#endif
}

VolumetricAnimation::~VolumetricAnimation()
{
	delete m_pConstantBufferData;
}

void VolumetricAnimation::ResetCameraView()
{
	auto center = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
	auto radius = m_camOrbitRadius;
	auto maxRadius = m_camMaxOribtRadius;
	auto minRadius = m_camMinOribtRadius;
	auto longAngle = 4.50f;
	auto latAngle = 1.45f;
	m_camera.View(center, radius, minRadius, maxRadius, longAngle, latAngle);
}

void VolumetricAnimation::OnConfiguration()
{
	Core::g_config.swapChainDesc.BufferCount = 5;
	Core::g_config.swapChainDesc.Width = m_width;
	Core::g_config.swapChainDesc.Height = m_height;
}

HRESULT VolumetricAnimation::OnCreateResource()
{
	ASSERT(Graphics::g_device);
	HRESULT hr;
	VRET(LoadSizeDependentResource());
	VRET(LoadAssets());

	return S_OK;
}


// Load the assets.
HRESULT VolumetricAnimation::LoadAssets()
{
	HRESULT	hr;

	D3D12_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.MipLODBias = 0;
	sampler.MaxAnisotropy = 0;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.MinLOD = 0.0f;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;

	m_RootSignature.Reset(3, 1);
	m_RootSignature.InitStaticSampler(0, sampler);
	m_RootSignature[0].InitAsConstantBuffer(0);
#if USING_DESCRIPTOR_TABLE
	//m_RootSignature[0].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 0, 1);
	m_RootSignature[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
	m_RootSignature[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
#else
	m_RootSignature[1].InitAsBufferSRV(0);
	m_RootSignature[2].InitAsBufferUAV(0);
#endif
	m_RootSignature.Finalize(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS);

	m_GraphicsPSO.SetRootSignature(m_RootSignature);
	m_ComputePSO.SetRootSignature(m_RootSignature);

	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;
	ComPtr<ID3DBlob> computeShader;

	uint32_t compileFlags = 0;
	D3D_SHADER_MACRO macro[] =
	{
		{ "__hlsl",			"1" },
		{ nullptr,		nullptr }
	};
	VRET(Graphics::CompileShaderFromFile(Core::GetAssetFullPath(_T("VolumetricAnimation_shader.hlsl")).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "vsmain", "vs_5_0", compileFlags, 0, &vertexShader));
	VRET(Graphics::CompileShaderFromFile(Core::GetAssetFullPath(_T("VolumetricAnimation_shader.hlsl")).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "psmain", "ps_5_0", compileFlags, 0, &pixelShader));
	VRET(Graphics::CompileShaderFromFile(Core::GetAssetFullPath(_T("VolumetricAnimation_shader.hlsl")).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "csmain", "cs_5_0", compileFlags, 0, &computeShader));

	m_GraphicsPSO.SetVertexShader(vertexShader->GetBufferPointer(), vertexShader->GetBufferSize());
	m_GraphicsPSO.SetPixelShader(pixelShader->GetBufferPointer(), pixelShader->GetBufferSize());
	m_ComputePSO.SetComputeShader(computeShader->GetBufferPointer(), computeShader->GetBufferSize());


	// Define the vertex input layout.
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
	m_GraphicsPSO.SetInputLayout(_countof(inputElementDescs), inputElementDescs);
	m_GraphicsPSO.SetRasterizerState(Graphics::g_RasterizerDefault);
	m_GraphicsPSO.SetBlendState(Graphics::g_BlendDisable);
	m_GraphicsPSO.SetDepthStencilState(Graphics::g_DepthStateReadWrite);
	m_GraphicsPSO.SetSampleMask(UINT_MAX);
	m_GraphicsPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	DXGI_FORMAT ColorFormat = Graphics::g_pDisplayPlanes[0].GetFormat();
	DXGI_FORMAT DepthFormat = m_DepthBuffer.GetFormat();
	m_GraphicsPSO.SetRenderTargetFormats(1, &ColorFormat, DepthFormat);

	m_ComputePSO.Finalize();
	m_GraphicsPSO.Finalize();

	uint32_t volumeBufferElementCount = m_volumeDepth*m_volumeHeight*m_volumeWidth;
	uint8_t* volumeBuffer = (uint8_t*)malloc(volumeBufferElementCount * 4 * sizeof(uint8_t));

	float a = m_volumeWidth / 2.f;
	float b = m_volumeHeight / 2.f;
	float c = m_volumeDepth / 2.f;
#if SPHERE_VOLUME_ANIMATION
	float radius = sqrt(a*a + b*b + c*c);
#else
	float radius = (abs(a) + abs(b) + abs(c));
#endif
	XMINT4 bg = m_pConstantBufferData->bgCol;
	uint32_t bgMax = max(max(bg.x, bg.y), bg.z);
	m_pConstantBufferData->bgCol.w = bgMax;
	for (uint32_t z = 0; z < m_volumeDepth; z++)
		for (uint32_t y = 0; y < m_volumeHeight; y++)
			for (uint32_t x = 0; x < m_volumeWidth; x++)
			{
				float _x = x - m_volumeWidth / 2.f;
				float _y = y - m_volumeHeight / 2.f;
				float _z = z - m_volumeDepth / 2.f;
#if SPHERE_VOLUME_ANIMATION
				float currentRaidus = sqrt(_x*_x + _y*_y + _z*_z);
#else
				float currentRaidus = (abs(_x) + abs(_y) + abs(_z));
#endif
				float scale = currentRaidus / radius;
				uint32_t maxColCnt = 4;
				assert(maxColCnt < COLOR_COUNT);
				float currentScale = scale * maxColCnt + 0.1f;
				uint32_t idx = COLOR_COUNT - (uint32_t)(currentScale)-1;
				float intensity = currentScale - (uint32_t)currentScale;
				uint32_t col = (uint32_t)(intensity * (255 - bgMax)) + 1;
				volumeBuffer[(x + y*m_volumeWidth + z*m_volumeHeight*m_volumeWidth) * 4 + 0] = bg.x + col * shiftingColVals[idx].x;
				volumeBuffer[(x + y*m_volumeWidth + z*m_volumeHeight*m_volumeWidth) * 4 + 1] = bg.y + col * shiftingColVals[idx].y;
				volumeBuffer[(x + y*m_volumeWidth + z*m_volumeHeight*m_volumeWidth) * 4 + 2] = bg.z + col * shiftingColVals[idx].z;
				volumeBuffer[(x + y*m_volumeWidth + z*m_volumeHeight*m_volumeWidth) * 4 + 3] = shiftingColVals[idx].w;
			}

	m_VolumeBuffer.Create(L"Volume Buffer", volumeBufferElementCount, 4 * sizeof(uint8_t), volumeBuffer);

	// Define the geometry for a triangle.
	Vertex cubeVertices[] =
	{
		{ XMFLOAT3(-1 * VOLUME_SIZE_X * 0.5f * VOLUME_SIZE_SCALE, -1 * VOLUME_SIZE_Y * 0.5f * VOLUME_SIZE_SCALE, -1 * VOLUME_SIZE_Z * 0.5f * VOLUME_SIZE_SCALE) },
		{ XMFLOAT3(-1 * VOLUME_SIZE_X * 0.5f * VOLUME_SIZE_SCALE, -1 * VOLUME_SIZE_Y * 0.5f * VOLUME_SIZE_SCALE,  1 * VOLUME_SIZE_Z * 0.5f * VOLUME_SIZE_SCALE) },
		{ XMFLOAT3(-1 * VOLUME_SIZE_X * 0.5f * VOLUME_SIZE_SCALE,  1 * VOLUME_SIZE_Y * 0.5f * VOLUME_SIZE_SCALE, -1 * VOLUME_SIZE_Z * 0.5f * VOLUME_SIZE_SCALE) },
		{ XMFLOAT3(-1 * VOLUME_SIZE_X * 0.5f * VOLUME_SIZE_SCALE,  1 * VOLUME_SIZE_Y * 0.5f * VOLUME_SIZE_SCALE,  1 * VOLUME_SIZE_Z * 0.5f * VOLUME_SIZE_SCALE) },
		{ XMFLOAT3(1 * VOLUME_SIZE_X * 0.5f * VOLUME_SIZE_SCALE, -1 * VOLUME_SIZE_Y * 0.5f * VOLUME_SIZE_SCALE, -1 * VOLUME_SIZE_Z * 0.5f * VOLUME_SIZE_SCALE) },
		{ XMFLOAT3(1 * VOLUME_SIZE_X * 0.5f * VOLUME_SIZE_SCALE, -1 * VOLUME_SIZE_Y * 0.5f * VOLUME_SIZE_SCALE,  1 * VOLUME_SIZE_Z * 0.5f * VOLUME_SIZE_SCALE) },
		{ XMFLOAT3(1 * VOLUME_SIZE_X * 0.5f * VOLUME_SIZE_SCALE,  1 * VOLUME_SIZE_Y * 0.5f * VOLUME_SIZE_SCALE, -1 * VOLUME_SIZE_Z * 0.5f * VOLUME_SIZE_SCALE) },
		{ XMFLOAT3(1 * VOLUME_SIZE_X * 0.5f * VOLUME_SIZE_SCALE,  1 * VOLUME_SIZE_Y * 0.5f * VOLUME_SIZE_SCALE,  1 * VOLUME_SIZE_Z * 0.5f * VOLUME_SIZE_SCALE) },
	};

	const uint32_t vertexBufferSize = sizeof(cubeVertices);
	m_VertexBuffer.Create(L"Vertex Buffer", ARRAYSIZE(cubeVertices), sizeof(XMFLOAT3), (void*)cubeVertices);

	uint16_t cubeIndices[] =
	{
		0,2,1, 1,2,3,  4,5,6, 5,7,6,  0,1,5, 0,5,4,  2,6,7, 2,7,3,  0,4,6, 0,6,2,  1,3,7, 1,7,5,
	};

	m_IndexBuffer.Create(L"Index Buffer", ARRAYSIZE(cubeIndices), sizeof(uint16_t), (void*)cubeIndices);

	ResetCameraView();

	return S_OK;
}

// Load size dependent resource
HRESULT VolumetricAnimation::LoadSizeDependentResource()
{
	uint32_t width = Core::g_config.swapChainDesc.Width;
	uint32_t height = Core::g_config.swapChainDesc.Height;

	m_DepthBuffer.Create(L"Depth Buffer", width, height, DXGI_FORMAT_D32_FLOAT);

	float fAspectRatio = width / (FLOAT)height;
	m_camera.Projection(XM_PIDIV2 / 2, fAspectRatio);
	return S_OK;
}

// Update frame-based values.
void VolumetricAnimation::OnUpdate()
{
	m_camera.ProcessInertia();
}

// Render the scene.
void VolumetricAnimation::OnRender()
{
	XMMATRIX view = m_camera.View();
	XMMATRIX proj = m_camera.Projection();

	XMMATRIX world = XMMatrixIdentity();
	m_pConstantBufferData->invWorld = XMMatrixInverse(nullptr, world);
	m_pConstantBufferData->wvp = XMMatrixMultiply(XMMatrixMultiply(world, view), proj);
	XMStoreFloat4(&m_pConstantBufferData->viewPos, m_camera.Eye());

	if (!m_OneContext)
	{
		ComputeContext& cptContext = ComputeContext::Begin(L"Update Volume");
		{
			GPU_PROFILE(cptContext, L"Updating");
			cptContext.SetRootSignature(m_RootSignature);
			cptContext.SetPipelineState(m_ComputePSO);
			cptContext.TransitionResource(m_VolumeBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			cptContext.SetDynamicConstantBufferView(0, sizeof(ConstantBuffer), m_pConstantBufferData);
			cptContext.SetBufferUAV(2, m_VolumeBuffer);
			cptContext.Dispatch(m_volumeWidth / THREAD_X, m_volumeHeight / THREAD_Y, m_volumeDepth / THREAD_Z);
		}
		cptContext.Finish(true);

		GraphicsContext& gfxContext = GraphicsContext::Begin(L"Render Volume");
		{

			GPU_PROFILE(gfxContext, L"Rendering");

			gfxContext.ClearColor(Graphics::g_pDisplayPlanes[Graphics::g_CurrentDPIdx]);
			gfxContext.ClearDepth(m_DepthBuffer);
			gfxContext.SetRootSignature(m_RootSignature);
			gfxContext.SetPipelineState(m_GraphicsPSO);
			gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			gfxContext.TransitionResource(m_VolumeBuffer, D3D12_RESOURCE_STATE_GENERIC_READ);
			gfxContext.TransitionResource(Graphics::g_pDisplayPlanes[Graphics::g_CurrentDPIdx], D3D12_RESOURCE_STATE_RENDER_TARGET);
			gfxContext.SetDynamicConstantBufferView(0, sizeof(ConstantBuffer), m_pConstantBufferData);
			gfxContext.SetBufferSRV(1, m_VolumeBuffer);
			gfxContext.SetRenderTargets(1, &Graphics::g_pDisplayPlanes[Graphics::g_CurrentDPIdx], &m_DepthBuffer);
			gfxContext.SetViewport(Graphics::g_DisplayPlaneViewPort);
			gfxContext.SetScisor(Graphics::g_DisplayPlaneScissorRect);
			gfxContext.SetVertexBuffer(0, m_VertexBuffer.VertexBufferView());
			gfxContext.SetIndexBuffer(m_IndexBuffer.IndexBufferView());
			gfxContext.DrawIndexed(36);

			TextContext Text(gfxContext);
			Text.Begin();
			Text.SetViewSize((float)Core::g_config.swapChainDesc.Width, (float)Core::g_config.swapChainDesc.Height);
			Text.SetFont(L"xerox.fnt");
			Text.ResetCursor(10, 80);
			Text.SetTextSize(20.f);
			Text.DrawString("Use 's' to switch between using one cmdqueue or using two cmdqueue and sync\n");
			Text.NewLine();
			Text.DrawString(m_OneContext ? "Current State: Using one cmdqueue" : "Current State: Using two cmdqueue and sync");
			Text.End();

			GPU_Profiler::DrawStats(gfxContext);

			gfxContext.TransitionResource(Graphics::g_pDisplayPlanes[Graphics::g_CurrentDPIdx], D3D12_RESOURCE_STATE_PRESENT);
		}
		gfxContext.Finish();
	}
	else
	{
		CommandContext& cmdContext = CommandContext::Begin(L"Update&Render");
		ComputeContext& cptContext = cmdContext.GetComputeContext();
		{
			GPU_PROFILE(cptContext, L"Updating");
			cptContext.SetRootSignature(m_RootSignature);
			cptContext.SetPipelineState(m_ComputePSO);
			cptContext.TransitionResource(m_VolumeBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			cptContext.SetDynamicConstantBufferView(0, sizeof(ConstantBuffer), m_pConstantBufferData);
			cptContext.SetBufferUAV(2, m_VolumeBuffer);
			cptContext.Dispatch(m_volumeWidth / THREAD_X, m_volumeHeight / THREAD_Y, m_volumeDepth / THREAD_Z);
		}

		GraphicsContext& gfxContext = cmdContext.GetGraphicsContext();
		{
			GPU_PROFILE(gfxContext, L"Rendering");

			gfxContext.ClearColor(Graphics::g_pDisplayPlanes[Graphics::g_CurrentDPIdx]);
			gfxContext.ClearDepth(m_DepthBuffer);
			gfxContext.SetRootSignature(m_RootSignature);
			gfxContext.SetPipelineState(m_GraphicsPSO);
			gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			gfxContext.TransitionResource(m_VolumeBuffer, D3D12_RESOURCE_STATE_GENERIC_READ);
			gfxContext.TransitionResource(Graphics::g_pDisplayPlanes[Graphics::g_CurrentDPIdx], D3D12_RESOURCE_STATE_RENDER_TARGET);
			gfxContext.SetDynamicConstantBufferView(0, sizeof(ConstantBuffer), m_pConstantBufferData);
			gfxContext.SetBufferSRV(1, m_VolumeBuffer);
			gfxContext.SetRenderTargets(1, &Graphics::g_pDisplayPlanes[Graphics::g_CurrentDPIdx], &m_DepthBuffer);
			gfxContext.SetViewport(Graphics::g_DisplayPlaneViewPort);
			gfxContext.SetScisor(Graphics::g_DisplayPlaneScissorRect);
			gfxContext.SetVertexBuffer(0, m_VertexBuffer.VertexBufferView());
			gfxContext.SetIndexBuffer(m_IndexBuffer.IndexBufferView());
			gfxContext.DrawIndexed(36);

			TextContext Text(gfxContext);
			Text.Begin();
			Text.SetViewSize((float)Core::g_config.swapChainDesc.Width, (float)Core::g_config.swapChainDesc.Height);
			Text.SetFont(L"xerox.fnt");
			Text.ResetCursor(10, 80);
			Text.SetTextSize(20.f);
			Text.DrawString("Use 's' to switch between using one cmdqueue or using two cmdqueue and sync\n");
			Text.NewLine();
			Text.DrawString(m_OneContext ? "Current State: Using one cmdqueue" : "Current State: Using two cmdqueue and sync");
			Text.End();

			GPU_Profiler::DrawStats(gfxContext);

			gfxContext.TransitionResource(Graphics::g_pDisplayPlanes[Graphics::g_CurrentDPIdx], D3D12_RESOURCE_STATE_PRESENT);
		}
		gfxContext.Finish();
	}
}

HRESULT VolumetricAnimation::OnSizeChanged()
{
	HRESULT hr;
	m_DepthBuffer.Destroy();
	VRET(LoadSizeDependentResource());
	return S_OK;
}

void VolumetricAnimation::OnDestroy()
{
}

bool VolumetricAnimation::OnEvent(MSG* msg)
{
	switch (msg->message)
	{
	case WM_MOUSEWHEEL:
	{
		auto delta = GET_WHEEL_DELTA_WPARAM(msg->wParam);
		m_camera.ZoomRadius(-0.007f*delta);
		return true;
	}
	case WM_POINTERDOWN:
	case WM_POINTERUPDATE:
	case WM_POINTERUP:
	{
		auto pointerId = GET_POINTERID_WPARAM(msg->wParam);
		POINTER_INFO pointerInfo;
		if (GetPointerInfo(pointerId, &pointerInfo)) {
			if (msg->message == WM_POINTERDOWN) {
				// Compute pointer position in render units
				POINT p = pointerInfo.ptPixelLocation;
				ScreenToClient(Core::g_hwnd, &p);
				RECT clientRect;
				GetClientRect(Core::g_hwnd, &clientRect);
				p.x = p.x * Core::g_config.swapChainDesc.Width / (clientRect.right - clientRect.left);
				p.y = p.y * Core::g_config.swapChainDesc.Height / (clientRect.bottom - clientRect.top);
				// Camera manipulation
				m_camera.AddPointer(pointerId);
			}
		}

		// Otherwise send it to the camera controls
		m_camera.ProcessPointerFrames(pointerId, &pointerInfo);
		if (msg->message == WM_POINTERUP) m_camera.RemovePointer(pointerId);
		return true;
	}
	case WM_KEYDOWN:
		switch (msg->wParam) {
		case 'S':
			m_OneContext = !m_OneContext;
			PRINTINFO("OneContext is %s", m_OneContext ? "on" : "off");
			return 0;
		} 
	return 0;
	}
	return false;
}