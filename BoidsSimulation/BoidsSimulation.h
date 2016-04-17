#pragma once

#include "DX12Framework.h"
#include "DescriptorHeap.h"
#include "GpuResource.h"
#include "Graphics.h"
#include "SamplerMngr.h"
#include "LinearAllocator.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "CommandContext.h"
#include "Camera.h"


using namespace DirectX;
using namespace Microsoft::WRL;

#include "BoidsSimulation_SharedHeader.inl"


class BoidsSimulation : public Core::IDX12Framework
{
public:
	BoidsSimulation( uint32_t width, uint32_t height );
	~BoidsSimulation();

	virtual void OnConfiguration();
	virtual void OnInit();
	virtual HRESULT OnCreateResource();
	virtual HRESULT OnSizeChanged();
	virtual void OnUpdate();
	virtual void OnRender( CommandContext& EngineContext );
	virtual void OnDestroy();
	virtual bool OnEvent( MSG* msg );

private:
	HRESULT LoadAssets();
	HRESULT LoadSizeDependentResource();
	void ResetCameraView();

	FishData* CreateInitialFishData();

	uint32_t			m_width;
	uint32_t			m_height;

	float				m_camOrbitRadius = 80.f;
	float				m_camMaxOribtRadius = 10000.f;
	float				m_camMinOribtRadius = 1.f;

	SimulationCB		m_SimulationCB;
	RenderCB			m_RenderCB;

	// Buffers
	Texture				m_ColorMapTex;
	DepthBuffer			m_DepthBuffer;
	StructuredBuffer	m_BoidsPosVelBuffer[2];
	DynAlloc*			m_pConstantBuffer;

	StructuredBuffer	m_VertexBuffer;

	GraphicsPSO			m_GraphicsPSO;
	ComputePSO			m_ComputePSO;
	RootSignature		m_RootSignature;

	// App resources.
	OrbitCamera			m_camera;

	uint8_t				m_OnStageBufIdx = 0;
	bool				m_NeedUpdate = true;
	bool				m_ForcePerFrameSimulation = true;
	bool				m_PauseSimulation = false;
	float				m_SimulationDelta = 0.015f;
	float				m_SimulationTimer = 0.0f;
	float				m_SimulationMaxDelta = 0.08f;

	LinearAllocator		m_Allocator;
};
