#include "LibraryHeader.h"
#include "DX12Framework.h"
#include "Utility.h"
#include "DXHelper.h"
#include "GPU_Profiler.h"
#include "CmdListMngr.h"
#include "Graphics.h"

#include <unordered_map>
#include <string>

using namespace std;

namespace{

    double                              gpuProfiler_GPUTickDelta;
    uint32_t                            gpuProfiler_timerCount;
    unordered_map<string, uint32_t>     gpuProfiler_timerNameIdxMap;
    string*                             gpuProfiler_timerNameArray;

    ID3D12CommandAllocator*             gpuProfiler_cmdAllocator;

    ComPtr<ID3D12GraphicsCommandList>   gpuProfiler_cmdList;
    ComPtr<ID3D12Resource>              gpuProfiler_readbackBuffer;
    ComPtr<ID3D12QueryHeap>             gpuProfiler_queryHeap;
    uint64_t*                           gpuProfiler_timeStampBuffer;

    CRITICAL_SECTION                    gpuProfiler_critialSection;
}

HRESULT GPU_Profiler::CreateResource()
{
    HRESULT hr;

    // Initialize the array to store all timer name
    gpuProfiler_timerNameArray = new string[MAX_TIMER_COUNT];

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
    DeleteCriticalSection( &gpuProfiler_critialSection );
}

void GPU_Profiler::ProcessAndReadback()
{
    gpuProfiler_cmdAllocator = Graphics::g_cmdListMngr.RequestAllocator();
    gpuProfiler_cmdList->Reset( gpuProfiler_cmdAllocator, nullptr );
    gpuProfiler_cmdList->ResolveQueryData( gpuProfiler_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2 * gpuProfiler_timerCount, gpuProfiler_readbackBuffer.Get(), 0 );
    gpuProfiler_cmdList->Close();
    
    uint64_t FenceValue = Graphics::g_cmdListMngr.ExecuteCommandList( gpuProfiler_cmdList.Get() );
    Graphics::g_cmdListMngr.DiscardAllocator( FenceValue, gpuProfiler_cmdAllocator );
}

void GPU_Profiler::BeginReadBack()
{
    HRESULT hr;

    D3D12_RANGE range;
    range.Begin = 0;
    range.End = MAX_TIMER_COUNT * 2 * sizeof( uint64_t );
    V( gpuProfiler_readbackBuffer->Map( 0, &range, reinterpret_cast< void** >( &gpuProfiler_timeStampBuffer ) ) );
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

void GPU_Profiler::EndReadBack()
{
    D3D12_RANGE range = {};
    gpuProfiler_readbackBuffer->Unmap( 0, &range);
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