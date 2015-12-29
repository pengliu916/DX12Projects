#include "LibraryHeader.h"
#include "DX12Framework.h"
#include "Utility.h"
#include "DXHelper.h"
#include "GPU_Profiler.h"
#include "CmdListMngr.h"
#include "TextRenderer.h"
#include "Graphics.h"

#include <unordered_map>
#include <string>

using namespace std;

namespace{

    double                              gpuProfiler_GPUTickDelta;
    uint32_t                            gpuProfiler_timerCount;
    unordered_map<string, uint32_t>     gpuProfiler_timerNameIdxMap;
    string*                             gpuProfiler_timerNameArray;
    uint64_t*                           gpuProfiler_timeStampBufferCopy;

    ID3D12CommandAllocator*             gpuProfiler_cmdAllocator;

    ComPtr<ID3D12GraphicsCommandList>   gpuProfiler_cmdList;
    ComPtr<ID3D12Resource>              gpuProfiler_readbackBuffer;
    ComPtr<ID3D12QueryHeap>             gpuProfiler_queryHeap;
    uint64_t*                           gpuProfiler_timeStampBuffer;

    D3D12_VIEWPORT                      m_viewport;
    D3D12_RECT                          m_scissorRect;

    CRITICAL_SECTION                    gpuProfiler_critialSection;
}

HRESULT GPU_Profiler::CreateResource()
{
    HRESULT hr;

    // Initialize the array to store all timer name
    gpuProfiler_timerNameArray = new string[MAX_TIMER_COUNT];

    // Initialize the array to copy from timer buffer
    gpuProfiler_timeStampBufferCopy = new uint64_t[MAX_TIMER_COUNT];

    // Initialize output critical section
    InitializeCriticalSection( &gpuProfiler_critialSection );

    uint64_t freq;
    Graphics::g_cmdListMngr.GetCommandQueue()->GetTimestampFrequency( &freq );
    gpuProfiler_GPUTickDelta = 1000.0 / static_cast< double >( freq );

    D3D12_HEAP_PROPERTIES HeapProps;
    HeapProps.Type = D3D12_HEAP_TYPE_READBACK;
    HeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    HeapProps.CreationNodeMask = 1;
    HeapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC BufferDesc;
    BufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    BufferDesc.Alignment = 0;
    BufferDesc.Width = sizeof( uint64_t ) * MAX_TIMER_COUNT * 2;
    BufferDesc.Height = 1;
    BufferDesc.DepthOrArraySize = 1;
    BufferDesc.MipLevels = 1;
    BufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    BufferDesc.SampleDesc.Count = 1;
    BufferDesc.SampleDesc.Quality = 0;
    BufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    BufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    VRET( Graphics::g_device->CreateCommittedResource( &HeapProps, D3D12_HEAP_FLAG_NONE, &BufferDesc,
                                                       D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &gpuProfiler_readbackBuffer ) ) );
    DXDebugName( gpuProfiler_readbackBuffer );

    D3D12_QUERY_HEAP_DESC QueryHeapDesc;
    QueryHeapDesc.Count = MAX_TIMER_COUNT * 2;
    QueryHeapDesc.NodeMask = 1;
    QueryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    VRET( Graphics::g_device->CreateQueryHeap( &QueryHeapDesc, IID_PPV_ARGS( &gpuProfiler_queryHeap ) ) );
    PRINTINFO( "QueryHeap created" );
    DXDebugName( gpuProfiler_queryHeap );
    
    Graphics::g_cmdListMngr.CreateNewCommandList( &gpuProfiler_cmdList, &gpuProfiler_cmdAllocator );
    gpuProfiler_cmdList->Close();

    return S_OK;
}

void GPU_Profiler::ShutDown()
{
    delete[] gpuProfiler_timerNameArray;
    delete[] gpuProfiler_timeStampBufferCopy;
    DeleteCriticalSection( &gpuProfiler_critialSection );
}

void GPU_Profiler::ProcessAndReadback()
{
    gpuProfiler_cmdAllocator = Graphics::g_cmdListMngr.RequestAllocator();
    gpuProfiler_cmdList->Reset( gpuProfiler_cmdAllocator, nullptr );

    ID3D12DescriptorHeap* ppHeaps[] = { Graphics::g_pCSUDescriptorHeap->mHeap.Get() };
    gpuProfiler_cmdList->SetDescriptorHeaps( _countof( ppHeaps ), ppHeaps );

    gpuProfiler_cmdList->RSSetViewports( 1, &Graphics::g_DisplayPlaneViewPort );
    gpuProfiler_cmdList->RSSetScissorRects( 1, &Graphics::g_DisplayPlaneScissorRect );

    gpuProfiler_cmdList->OMSetRenderTargets( 1, &Graphics::g_pDisplayPlaneHandlers[Graphics::g_CurrentDPIdx], FALSE, nullptr );

    TextContext txtCtx;
    txtCtx.Begin( gpuProfiler_cmdList.Get() );
    txtCtx.SetViewSize( ( float ) Core::g_config.swapChainDesc.Width, ( float ) Core::g_config.swapChainDesc.Height );
    txtCtx.ResetCursor( 10, 10 );
    txtCtx.SetTextSize( 15.f );
    for ( uint32_t idx = 0; idx < gpuProfiler_timerCount; idx++ )
    {
        char temp[128];
        GPU_Profiler::GetTimingStr( idx, temp );
        txtCtx.DrawString( string( temp ) );
        txtCtx.NewLine();
    }
    txtCtx.End();

    gpuProfiler_cmdList->ResolveQueryData( gpuProfiler_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2 * gpuProfiler_timerCount, gpuProfiler_readbackBuffer.Get(), 0 );

    gpuProfiler_cmdList->Close();
    
    uint64_t FenceValue = Graphics::g_cmdListMngr.ExecuteCommandList( gpuProfiler_cmdList.Get() );
    Graphics::g_cmdListMngr.DiscardAllocator( FenceValue, gpuProfiler_cmdAllocator );

    HRESULT hr;
    D3D12_RANGE range;
    range.Begin = 0;
    range.End = MAX_TIMER_COUNT * 2 * sizeof( uint64_t );
    V( gpuProfiler_readbackBuffer->Map( 0, &range, reinterpret_cast< void** >( &gpuProfiler_timeStampBuffer ) ) );
    memcpy( gpuProfiler_timeStampBufferCopy, gpuProfiler_timeStampBuffer, gpuProfiler_timerCount*2*sizeof( uint64_t ));
    gpuProfiler_readbackBuffer->Unmap( 0, &range );

}

double GPU_Profiler::ReadTimer( uint32_t idx, double* start, double* stop )
{
    ASSERT( idx < MAX_TIMER_COUNT );

    double _start = gpuProfiler_timeStampBufferCopy[idx * 2] * gpuProfiler_GPUTickDelta;
    double _stop = gpuProfiler_timeStampBufferCopy[idx * 2 + 1] * gpuProfiler_GPUTickDelta;
    if ( start ) *start = _start;
    if ( stop ) *stop = _stop;
    return _stop - _start;
}

uint32_t GPU_Profiler::GetTimingStr( uint32_t idx, char* outStr )
{
    ASSERT( idx < MAX_TIMER_COUNT );
    if ( gpuProfiler_timerNameArray[idx].length() == 0 )
        return 0;
    double result = gpuProfiler_timeStampBufferCopy[idx * 2 + 1] * gpuProfiler_GPUTickDelta - gpuProfiler_timeStampBufferCopy[idx * 2] * gpuProfiler_GPUTickDelta;
    sprintf( outStr, "%s:%4.3fms ", gpuProfiler_timerNameArray[idx].c_str(), result );
    return ( uint32_t ) strlen( outStr );
}
void GPU_Profiler::StartTimeStampPunch( ID3D12GraphicsCommandList* cmdlist, uint32_t idx )
{
    ASSERT( idx < MAX_TIMER_COUNT );
    cmdlist->EndQuery( gpuProfiler_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, idx * 2 );
}

void GPU_Profiler::StopTimeStampPunch( ID3D12GraphicsCommandList* cmdlist, uint32_t idx )
{
    ASSERT( idx < MAX_TIMER_COUNT );
    cmdlist->EndQuery( gpuProfiler_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, idx * 2 + 1 );
}

GPUProfileScope::GPUProfileScope( ID3D12GraphicsCommandList* cmdlist, const char* szName )
    :m_cmdlist( cmdlist )
{
    PIXBeginEvent( cmdlist, 0, szName );
    auto iter = gpuProfiler_timerNameIdxMap.find( szName );
    if ( iter == gpuProfiler_timerNameIdxMap.end() )
    {
        CriticalSectionScope lock( &gpuProfiler_critialSection );
        m_idx = gpuProfiler_timerCount;
        gpuProfiler_timerNameArray[m_idx] = szName;
        gpuProfiler_timerNameIdxMap[szName] = gpuProfiler_timerCount++;
    }
    else
    {
        m_idx = iter->second;
    }
    GPU_Profiler::StartTimeStampPunch( m_cmdlist, m_idx );
}

GPUProfileScope::~GPUProfileScope()
{
    GPU_Profiler::StopTimeStampPunch( m_cmdlist, m_idx );
    PIXEndEvent( m_cmdlist );
}