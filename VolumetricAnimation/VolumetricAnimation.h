#pragma once

#include "DX12Framework.h"
#include "DescriptorHeap.h"
#include "TextRenderer.h"
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


    uint32_t                            m_width;
    uint32_t                            m_height;

    float                               m_camOrbitRadius;
    float                               m_camMaxOribtRadius;
    float                               m_camMinOribtRadius;

    static const int                    m_FrameCount = 5;

    ID3D12CommandAllocator*             m_gfxcmdAllocator;
    ID3D12CommandAllocator*             m_cptcmdAllocator;

    // Pipeline objects.
    D3D12_VIEWPORT                      m_viewport;
    D3D12_RECT                          m_scissorRect;
    ComPtr<ID3D12Resource>              m_renderTargets[m_FrameCount];
    ComPtr<ID3D12GraphicsCommandList>   m_graphicCmdList;
    ComPtr<ID3D12RootSignature>         m_graphicsRootSignature;
    ComPtr<ID3D12PipelineState>         m_pipelineState;
    D3D12_CPU_DESCRIPTOR_HANDLE         m_rtvHandle[m_FrameCount];
    D3D12_CPU_DESCRIPTOR_HANDLE         m_dsvHandle;

    DescriptorHandle                    m_srvHandle;
    DescriptorHandle                    m_cbvHandle;
    DescriptorHandle                    m_uavHandle;

    // Compute objects.
    ComPtr<ID3D12RootSignature>         m_computeRootSignature;
    ComPtr<ID3D12GraphicsCommandList>   m_computeCmdList;
    ComPtr<ID3D12PipelineState>         m_computeState;

    // App resources.
    ComPtr<ID3D12Resource>              m_depthBuffer;
    ComPtr<ID3D12Resource>              m_constantBuffer;
    ComPtr<ID3D12Resource>              m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW            m_vertexBufferView;
    ComPtr<ID3D12Resource>              m_indexBuffer;
    D3D12_INDEX_BUFFER_VIEW             m_indexBufferView;
    ComPtr<ID3D12Resource>              m_volumeBuffer;

    TextContext                         m_TextRenderer;

    OrbitCamera                         m_camera;
    struct ConstantBuffer*              m_pConstantBufferData;
    uint8_t*                            m_pCbvDataBegin;

    // Synchronization objects.
    uint32_t                            m_frameIndex;

    uint32_t                            m_volumeWidth;
    uint32_t                            m_volumeHeight;
    uint32_t                            m_volumeDepth;

    HRESULT LoadPipeline( ID3D12Device* m_device );
    HRESULT LoadAssets();
    HRESULT LoadSizeDependentResource();

    void ResetCameraView();
    void PopulateGraphicsCommandList( uint32_t i );
    void PopulateComputeCommandList( uint32_t i );
};
