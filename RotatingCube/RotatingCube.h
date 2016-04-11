#pragma once

#include "DX12Framework.h"
#include "DescriptorHeap.h"
//#include "TextRenderer.h"
#include "GpuResource.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "CommandContext.h"
#include "Camera.h"
#include "StepTimer.h"

using namespace DirectX;
using namespace Microsoft::WRL;

class RotatingCube : public Core::IDX12Framework
{
public:
    RotatingCube( uint32_t width, uint32_t height, std::wstring name );
	~RotatingCube();

    virtual void OnConfiguration();
	virtual void OnInit();
    virtual HRESULT OnCreateResource();
    virtual HRESULT OnSizeChanged();
    virtual void OnUpdate();
    virtual void OnRender(CommandContext& EngineContext);
    virtual void OnDestroy();
    virtual bool OnEvent( MSG* msg );

private:        
    HRESULT LoadAssets();
    HRESULT LoadSizeDependentResource();
    void ResetCameraView();
  
    struct Vertex
    {
        XMFLOAT3 position;
        XMFLOAT3 color;
    };

	__declspec(align(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)) struct CBuffer
	{
		XMMATRIX wvp;
		XMMATRIX invWorld;
		XMFLOAT4 viewPos;
		XMINT4 bgCol;
		XMINT4 shiftingColVals[7];
		void * operator new(size_t i)
		{
			return _aligned_malloc(i, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
		};
		void operator delete(void* p)
		{
			_aligned_free(p);
		};
	};

	struct CBuffer* cbufferData;

    uint32_t                            m_width;
    uint32_t                            m_height;

    float                               m_camOrbitRadius;
    float                               m_camMaxOribtRadius;
    float                               m_camMinOribtRadius;

	DepthBuffer							m_DepthBuffer;
	StructuredBuffer					m_VertexBuffer;
	ByteAddressBuffer					m_IndexBuffer;
	GraphicsPSO							m_GraphicsPSO;
	RootSignature						m_RootSignature;

    // App resources.
    OrbitCamera                         m_camera;
    StepTimer                           m_timer;
};
