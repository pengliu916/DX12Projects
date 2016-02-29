#pragma once

#include "DX12Framework.h"
#include "DescriptorHeap.h"
//#include "TextRenderer.h"
#include "GpuResource.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "CommandContext.h"
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


    uint32_t                            m_width;
    uint32_t                            m_height;

    float                               m_camOrbitRadius;
    float                               m_camMaxOribtRadius;
    float                               m_camMinOribtRadius;

	DepthBuffer							m_DepthBuffer;
	StructuredBuffer					m_VertexBuffer;
	ByteAddressBuffer					m_IndexBuffer;
	GraphicsPSO							m_GraphicsPSO;
	ComputePSO							m_ComputePSO;
	RootSignature						m_RootSignature;
	StructuredBuffer					m_VolumeBuffer;

    OrbitCamera                         m_camera;
    struct ConstantBuffer*              m_pConstantBufferData;

    uint32_t                            m_volumeWidth;
    uint32_t                            m_volumeHeight;
    uint32_t                            m_volumeDepth;

    HRESULT LoadAssets();
    HRESULT LoadSizeDependentResource();

    void ResetCameraView();
};
