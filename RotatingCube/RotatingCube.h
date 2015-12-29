#pragma once

#include "DX12Framework.h"
#include "Camera.h"
#include "StepTimer.h"

using namespace DirectX;
using namespace Microsoft::WRL;

class RotatingCube : public Core::IDX12Framework
{
public:
    RotatingCube( uint32_t width, uint32_t height, std::wstring name );

    virtual void OnConfiguration();
    virtual HRESULT OnCreateResource();
    virtual HRESULT OnSizeChanged();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();
    virtual bool OnEvent( MSG* msg );

private:

    HRESULT LoadPipeline();                 
    HRESULT LoadAssets();
    HRESULT LoadSizeDependentResource();
    void ResetCameraView();
    void PopulateCommandList();
    void WaitForPreviousFrame();    
  
    struct Vertex
    {
        XMFLOAT3 position;
        XMFLOAT3 color;
    };

    uint32_t                            m_width;
    uint32_t                            m_height;

    float                               m_camOrbitRadius;
    float                               m_camMaxOribtRadius;
    float                               m_camMinOribtRadius;

    // Pipeline objects.
    ComPtr<ID3D12CommandAllocator>      m_commandAllocator;
    ComPtr<ID3D12RootSignature>         m_rootSignature;
    ComPtr<ID3D12DescriptorHeap>        m_dsvHeap;
    ComPtr<ID3D12PipelineState>         m_pipelineState;
    ComPtr<ID3D12GraphicsCommandList>   m_commandList;

    // App resources.
    ComPtr<ID3D12Resource>              m_depthBuffer;
    ComPtr<ID3D12Resource>              m_constantBuffer;
    ComPtr<ID3D12Resource>              m_vertexBuffer;
    ComPtr<ID3D12Resource>              m_indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW            m_vertexBufferView;
    D3D12_INDEX_BUFFER_VIEW             m_indexBufferView;
    OrbitCamera                         m_camera;
    StepTimer                           m_timer;

    // Synchronization objects.
    uint32_t                            m_frameIndex;
    HANDLE                              m_fenceEvent;
    ComPtr<ID3D12Fence>                 m_fence;
    uint64_t                            m_fenceValue;
};
