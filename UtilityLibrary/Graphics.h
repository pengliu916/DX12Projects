#pragma once


using namespace Microsoft::WRL;
using namespace DirectX;

class CmdListMngr;
class DescriptorHeap;
class LinearAllocator;

namespace Graphics
{
    // Framework level gfx resource
    extern ComPtr<ID3D12Device>         g_device;
    extern ComPtr<IDXGISwapChain3>      g_swapChain;
    extern ComPtr<IDXGIFactory4>        g_factory;
    extern CmdListMngr                  g_cmdListMngr;
    extern DescriptorHeap*              g_pRTVDescriptorHeap;
    extern DescriptorHeap*              g_pDSVDescriptorHeap;
    extern DescriptorHeap*              g_pSMPDescriptorHeap;
    extern DescriptorHeap*              g_pCSUDescriptorHeap;
    extern LinearAllocator              g_CpuLinearAllocator;
    extern LinearAllocator              g_GpuLinearAllocator;

    void Init();
    void Shutdown();
    HRESULT CreateResource();
    HRESULT ResizeBackBuffer();
    HRESULT CompileShaderFromFile( LPCWSTR pFileName, const D3D_SHADER_MACRO* pDefines, ID3DInclude* pInclude,
                                   LPCSTR pEntrypoint, LPCSTR pTarget, UINT Flags1, UINT Flags2, ID3DBlob** ppCode );
}