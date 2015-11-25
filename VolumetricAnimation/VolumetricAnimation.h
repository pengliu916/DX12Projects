#pragma once

#include "DX12Framework.h"
#include "Camera.h"

using namespace DirectX;
using namespace Microsoft::WRL;

class VolumetricAnimation : public Core::IDX12Framework
{
public:
    VolumetricAnimation( uint32_t width, uint32_t height, std::wstring name );
    ~VolumetricAnimation();

    virtual void OnConfiguration();
    virtual HRESULT OnCreateResource();
    virtual HRESULT OnSizeChanged();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();
    virtual bool OnEvent( MSG* msg );

private:
    struct Vertex
    {
        XMFLOAT3 position;
    };

    // Indices in the root parameter table.
    enum RootParameters : uint32_t
    {
        RootParameterCBV = 0,
        RootParameterSRV,
        RootParameterUAV,
        RootParametersCount
    };

    // now in C++ 11 the following will be equivalent to add it to initialization list 
    bool                                reuseCmdListToggled = false;
    bool                                reuseCmdList = false;

    uint32_t                            m_width;
    uint32_t                            m_height;

    float                               m_camOrbitRadius;
    float                               m_camMaxOribtRadius;
    float                               m_camMinOribtRadius;

    static const int                    m_FrameCount = 5;

    // Pipeline objects.
    D3D12_VIEWPORT                      m_viewport;
    D3D12_RECT                          m_scissorRect;
    ComPtr<ID3D12Resource>              m_renderTargets[m_FrameCount];
    ComPtr<ID3D12CommandAllocator>      m_graphicCmdAllocator;
    ComPtr<ID3D12GraphicsCommandList>   m_graphicCmdList[m_FrameCount];
    ComPtr<ID3D12RootSignature>         m_graphicsRootSignature;
    ComPtr<ID3D12DescriptorHeap>        m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap>        m_dsvHeap;
    ComPtr<ID3D12DescriptorHeap>        m_cbvsrvuavHeap;
    ComPtr<ID3D12PipelineState>         m_pipelineState;
    uint32_t                            m_rtvDescriptorSize;
    uint32_t                            m_cbvsrvuavDescriptorSize;

    // Compute objects.
    ComPtr<ID3D12RootSignature>         m_computeRootSignature;
    ComPtr<ID3D12CommandAllocator>      m_computeCmdAllocator;
    ComPtr<ID3D12CommandQueue>          m_computeCmdQueue;
    ComPtr<ID3D12GraphicsCommandList>   m_computeCmdList[m_FrameCount];
    ComPtr<ID3D12PipelineState>         m_computeState;

    // App resources.
    ComPtr<ID3D12Resource>              m_depthBuffer;
    ComPtr<ID3D12Resource>              m_constantBuffer;
    ComPtr<ID3D12Resource>              m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW            m_vertexBufferView;
    ComPtr<ID3D12Resource>              m_indexBuffer;
    D3D12_INDEX_BUFFER_VIEW             m_indexBufferView;
    ComPtr<ID3D12Resource>              m_volumeBuffer;

    OrbitCamera                         m_camera;
    struct ConstantBuffer*              m_pConstantBufferData;
    uint8_t*                            m_pCbvDataBegin;

    // Synchronization objects.
    uint32_t                            m_frameIndex;
    HANDLE                              m_fenceEvent;
    ComPtr<ID3D12Fence>                 m_fence;
    UINT64                              m_fenceValue[m_FrameCount];

    uint32_t                            m_volumeWidth;
    uint32_t                            m_volumeHeight;
    uint32_t                            m_volumeDepth;

    HRESULT LoadPipeline( ID3D12Device* m_device );
    HRESULT LoadAssets();
    HRESULT LoadSizeDependentResource();

    void ResetCameraView();
    void PopulateGraphicsCommandList( uint32_t i );
    void PopulateComputeCommandList( uint32_t i );
    void WaitForGraphicsCmd();
    void WaitForComputeCmd();

    void CreateReuseCmdLists();
};
