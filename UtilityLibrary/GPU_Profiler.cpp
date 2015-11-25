#include "LibraryHeader.h"
#include "DX12Framework.h"
#include "Utility.h"
#include "DXHelper.h"
#include "GPU_Profiler.h"
#include "Graphics.h"

#include <unordered_map>
#include <string>

using namespace std;

namespace{

    double                              gpuProfiler_GPUTickDelta;
    uint32_t                            gpuProfiler_timerCount;
    unordered_map<string, uint32_t>     gpuProfiler_timerNameIdxMap;
    string*                             gpuProfiler_timerNameArray;

    ComPtr<ID3D12CommandAllocator>      gpuProfiler_cmdAllocator;
    ComPtr<ID3D12GraphicsCommandList>   gpuProfiler_cmdList;
    ComPtr<ID3D12Resource>              gpuProfiler_readbackBuffer;
    ComPtr<ID3D12QueryHeap>             gpuProfiler_queryHeap;
    ComPtr<ID3D12Fence>                 gpuProfiler_fence;
    uint64_t*                           gpuProfiler_timeStampBuffer;

    CRITICAL_SECTION                    gpuProfiler_critialSection;
    uint64_t                            gpuProfiler_fenceValue;
    uint64_t                            gpuProfiler_lastCompletedFenceValue;
    HANDLE                              gpuProfiler_fenceEvent;

    bool IsFenceCompleted( uint64_t fenceValue )
    {
        if ( fenceValue > gpuProfiler_lastCompletedFenceValue )
            gpuProfiler_lastCompletedFenceValue = max( gpuProfiler_lastCompletedFenceValue, gpuProfiler_fence->GetCompletedValue() );
        return fenceValue <= gpuProfiler_lastCompletedFenceValue;
    }

    void WaitForFence( uint64_t fenceValue )
    {
        if ( IsFenceCompleted( fenceValue ) ) return;
        gpuProfiler_fence->SetEventOnCompletion( fenceValue, gpuProfiler_fenceEvent );
        WaitForSingleObject( gpuProfiler_fenceEvent, INFINITE );
        gpuProfiler_lastCompletedFenceValue = fenceValue;
    }
}

HRESULT GPU_Profiler::CreateResource()
{
    HRESULT hr;

    // Initialize the array to store all timer name
    gpuProfiler_timerNameArray = new string[MAX_TIMER_COUNT];

    // Initialize output critical section
    InitializeCriticalSection( &gpuProfiler_critialSection );

    uint64_t freq;
    Graphics::g_cmdQueue->GetTimestampFrequency( &freq );
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

    D3D12_RANGE range;
    range.Begin = 0;
    range.End = MAX_TIMER_COUNT * 2 * sizeof( uint64_t );
    V( gpuProfiler_readbackBuffer->Map( 0, &range, reinterpret_cast< void** >( &gpuProfiler_timeStampBuffer ) ) );


    VRET( Graphics::g_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &gpuProfiler_cmdAllocator ) ) );
    DXDebugName( gpuProfiler_cmdAllocator );

    // Create the graphics command list.
    VRET( Graphics::g_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, gpuProfiler_cmdAllocator.Get(), nullptr, IID_PPV_ARGS( &gpuProfiler_cmdList ) ) );
    DXDebugName( gpuProfiler_cmdList );

    // Close the commandlist since it's in record state after creation, and we have nothing to record yet
    VRET( gpuProfiler_cmdList->Close() );

    gpuProfiler_lastCompletedFenceValue = 0;
    gpuProfiler_fenceValue = 0;
    VRET( Graphics::g_device->CreateFence( gpuProfiler_fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &gpuProfiler_fence ) ) );
    DXDebugName( gpuProfiler_fence );

    // Create an event handle to use for frame synchronization.
    gpuProfiler_fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );
    if ( gpuProfiler_fenceEvent == nullptr )
        VRET( HRESULT_FROM_WIN32( GetLastError() ) );

    return S_OK;
}

void GPU_Profiler::ShutDown()
{
    delete gpuProfiler_timerNameArray;
    DeleteCriticalSection( &gpuProfiler_critialSection );
}

void GPU_Profiler::ProcessAndReadback()
{
    WaitForFence( gpuProfiler_fenceValue );
    gpuProfiler_cmdAllocator->Reset();
    gpuProfiler_cmdList->Reset( gpuProfiler_cmdAllocator.Get(), nullptr );
    gpuProfiler_cmdList->ResolveQueryData( gpuProfiler_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2 * gpuProfiler_timerCount, gpuProfiler_readbackBuffer.Get(), 0 );
    gpuProfiler_cmdList->Close();
    ID3D12CommandList* ppCmdList[] = { gpuProfiler_cmdList.Get() };
    Graphics::g_cmdQueue->ExecuteCommandLists( _countof( ppCmdList ), ppCmdList );
    Graphics::g_cmdQueue->Signal( gpuProfiler_fence.Get(), ++gpuProfiler_fenceValue );
}

double GPU_Profiler::ReadTimer( uint32_t idx, double* start, double* stop )
{
    if ( idx >= MAX_TIMER_COUNT )
    {
        PRINTERROR( "idx = %d is beyond the bounderay %d", idx, MAX_TIMER_COUNT );
        return 0;
    }
    double _start = gpuProfiler_timeStampBuffer[idx * 2] * gpuProfiler_GPUTickDelta;
    double _stop = gpuProfiler_timeStampBuffer[idx * 2 + 1] * gpuProfiler_GPUTickDelta;
    if ( start ) *start = _start;
    if ( stop ) *stop = _stop;
    return _stop - _start;
}

uint32_t GPU_Profiler::GetTimingStr( uint32_t idx, char* outStr )
{
    if ( idx >= MAX_TIMER_COUNT )
    {
        PRINTERROR( "idx = %d is beyond the bounderay %d", idx, MAX_TIMER_COUNT );
        return 0;
    }
    if ( gpuProfiler_timerNameArray[idx].length() == 0 )
        return 0;
    double result = gpuProfiler_timeStampBuffer[idx * 2 + 1] * gpuProfiler_GPUTickDelta - gpuProfiler_timeStampBuffer[idx * 2] * gpuProfiler_GPUTickDelta;
    sprintf( outStr, "%s:%4.3fms ", gpuProfiler_timerNameArray[idx].c_str(), result );
    return ( uint32_t ) strlen( outStr );
}
void GPU_Profiler::StartTimeStampPunch( ID3D12GraphicsCommandList* cmdlist, uint32_t idx )
{
    if ( idx >= MAX_TIMER_COUNT )
    {
        PRINTERROR( "idx = %d is beyond the bounderay %d", idx, MAX_TIMER_COUNT );
        return;
    }
    cmdlist->EndQuery( gpuProfiler_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, idx * 2 );
}

void GPU_Profiler::StopTimeStampPunch( ID3D12GraphicsCommandList* cmdlist, uint32_t idx )
{
    if ( idx >= MAX_TIMER_COUNT )
    {
        PRINTERROR( "idx = %d is beyond the bounderay %d", idx, MAX_TIMER_COUNT );
        return;
    }
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