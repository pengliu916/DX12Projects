#pragma once

#include "DX12Framework.h"
#include "Camera.h"

using namespace DirectX;
using namespace Microsoft::WRL;

class VolumetricAnimation : public DX12Framework
{
public:
	VolumetricAnimation( UINT width, UINT height, std::wstring name );
	~VolumetricAnimation();

	// now in C++ 11 the following will be equivalent to add it to initialization list 
	bool reuseCmdListToggled = false;
	bool reuseCmdList = false;

protected:
	virtual void OnConfiguration( Settings* config );
	virtual HRESULT OnInit(ID3D12Device* m_device);
	virtual HRESULT OnSizeChanged();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();
	virtual bool OnEvent( MSG* msg );

private:

	static const int m_FrameCount = 3;
	struct Vertex
	{
		XMFLOAT3 position;
	};

	// Pipeline objects.
	D3D12_VIEWPORT m_viewport;
	D3D12_RECT m_scissorRect;
	ComPtr<ID3D12Resource> m_renderTargets[m_FrameCount];
	ComPtr<ID3D12CommandAllocator> m_graphicCmdAllocator;
	ComPtr<ID3D12GraphicsCommandList> m_graphicCmdList[m_FrameCount];
	ComPtr<ID3D12RootSignature> m_graphicsRootSignature;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	ComPtr<ID3D12DescriptorHeap> m_cbvsrvuavHeap;
	ComPtr<ID3D12PipelineState> m_pipelineState;
	UINT m_rtvDescriptorSize;
	UINT m_cbvsrvuavDescriptorSize;

	// Compute objects.
	ComPtr<ID3D12RootSignature> m_computeRootSignature;
	ComPtr<ID3D12CommandAllocator> m_computeCmdAllocator;
	ComPtr<ID3D12CommandQueue> m_computeCmdQueue;
	ComPtr<ID3D12GraphicsCommandList> m_computeCmdList[m_FrameCount];
	ComPtr<ID3D12PipelineState> m_computeState;

	// App resources.
	ComPtr<ID3D12Resource> m_depthBuffer;
	ComPtr<ID3D12Resource> m_constantBuffer;
	ComPtr<ID3D12Resource> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
	ComPtr<ID3D12Resource> m_indexBuffer;
	D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
	ComPtr<ID3D12Resource> m_volumeBuffer;

	OrbitCamera m_camera;
	struct ConstantBuffer* m_pConstantBufferData;
	UINT8* m_pCbvDataBegin;

	// Synchronization objects.
	UINT m_frameIndex;
	HANDLE m_fenceEvent;
	ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue[m_FrameCount];

	UINT m_volumeWidth;
	UINT m_volumeHeight;
	UINT m_volumeDepth;

	// Indices in the root parameter table.
	enum RootParameters : UINT32
	{
		RootParameterCBV = 0,
		RootParameterSRV,
		RootParameterUAV,
		RootParametersCount
	};

	HRESULT LoadPipeline( ID3D12Device* m_device );
	HRESULT LoadAssets();
	HRESULT LoadSizeDependentResource();
	
	void ResetCameraView();
	void PopulateGraphicsCommandList(UINT i);
	void PopulateComputeCommandList(UINT i);
	void WaitForGraphicsCmd();
	void WaitForComputeCmd();

	void CreateReuseCmdLists();
};
