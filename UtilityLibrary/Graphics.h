#pragma once

class CmdListMngr;
class ContextManager;
class DescriptorHeap;
class LinearAllocator;
class CommandContext;
class ColorBuffer;
class SamplerDesc;
class SamplerDescriptor;

namespace Graphics
{
    // Framework level gfx resource
    extern Microsoft::WRL::ComPtr<ID3D12Device>		g_device;
    extern Microsoft::WRL::ComPtr<IDXGISwapChain3>	g_swapChain;
    extern Microsoft::WRL::ComPtr<IDXGIFactory4>	g_factory;
	extern CmdListMngr								g_cmdListMngr;
	extern ContextManager							g_ContextMngr;
    extern DescriptorHeap*							g_pRTVDescriptorHeap;
    extern DescriptorHeap*							g_pDSVDescriptorHeap;
    extern DescriptorHeap*							g_pSMPDescriptorHeap;
    extern DescriptorHeap*							g_pCSUDescriptorHeap;
	extern ColorBuffer*								g_pDisplayPlanes;
    extern uint32_t									g_CurrentDPIdx;

    extern D3D12_VIEWPORT							g_DisplayPlaneViewPort;
    extern D3D12_RECT								g_DisplayPlaneScissorRect;

	extern SamplerDesc								g_SamplerLinearClampDesc;
	extern SamplerDescriptor						g_SamplerLinearClamp;
	extern SamplerDesc								g_SamplerLinearWrapDesc;
	extern SamplerDescriptor						g_SamplerLinearWrap;

	extern D3D12_RASTERIZER_DESC					g_RasterizerDefault;
	extern D3D12_RASTERIZER_DESC					g_RasterizerDefaultCW;
	extern D3D12_RASTERIZER_DESC					g_RasterizerTwoSided;

	extern D3D12_BLEND_DESC							g_BlendNoColorWrite;
	extern D3D12_BLEND_DESC							g_BlendDisable;
	extern D3D12_BLEND_DESC							g_BlendPreMultiplied;
	extern D3D12_BLEND_DESC							g_BlendTraditional;
	extern D3D12_BLEND_DESC							g_BlendAdditive;
	extern D3D12_BLEND_DESC							g_BlendTraditionalAdditive;

	extern D3D12_DEPTH_STENCIL_DESC					g_DepthStateDisabled;
	extern D3D12_DEPTH_STENCIL_DESC					g_DepthStateReadWrite;
	extern D3D12_DEPTH_STENCIL_DESC					g_DepthStateReadOnly;
	extern D3D12_DEPTH_STENCIL_DESC					g_DepthStateReadOnlyReversed;
	extern D3D12_DEPTH_STENCIL_DESC					g_DepthStateTestEqual;

    void Init();
    void Shutdown();
    void Resize();
    void Present(CommandContext& EngineContext);
    HRESULT CreateResource();
    HRESULT CompileShaderFromFile( LPCWSTR pFileName, const D3D_SHADER_MACRO* pDefines, ID3DInclude* pInclude,
                                   LPCSTR pEntrypoint, LPCSTR pTarget, UINT Flags1, UINT Flags2, ID3DBlob** ppCode );
}