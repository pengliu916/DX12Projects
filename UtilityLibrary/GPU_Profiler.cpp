#include "LibraryHeader.h"
#include "DX12Framework.h"
#include "Utility.h"
#include "DXHelper.h"
#include "CommandContext.h"
#include "CmdListMngr.h"
#include "TextRenderer.h"
#include "Graphics.h"

#include <unordered_map>
#include <string>
#include "GPU_Profiler.h"

using namespace Microsoft::WRL;
using namespace std;

namespace{

    double                              gpuProfiler_GPUTickDelta;
    uint32_t                            gpuProfiler_timerCount;
    unordered_map<wstring, uint32_t>    gpuProfiler_timerNameIdxMap;
    wstring*                            gpuProfiler_timerNameArray;
    uint64_t*                           gpuProfiler_timeStampBufferCopy;

    ID3D12Resource*						gpuProfiler_readbackBuffer;
    ID3D12QueryHeap*					gpuProfiler_queryHeap;
    uint64_t*                           gpuProfiler_timeStampBuffer;

	uint64_t							gpuProfiler_fence = 0;

    CRITICAL_SECTION                    gpuProfiler_critialSection;
}

void GPU_Profiler::Initialize()
{
	// Initialize the array to store all timer name
	gpuProfiler_timerNameArray = new wstring[MAX_TIMER_COUNT];

	// Initialize the array to copy from timer buffer
	gpuProfiler_timeStampBufferCopy = new uint64_t[MAX_TIMER_COUNT];

	// Initialize output critical section
	InitializeCriticalSection(&gpuProfiler_critialSection);
}

HRESULT GPU_Profiler::CreateResource()
{
    HRESULT hr;
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
	gpuProfiler_readbackBuffer->SetName(L"GPU_Profiler Readback Buffer");

    D3D12_QUERY_HEAP_DESC QueryHeapDesc;
    QueryHeapDesc.Count = MAX_TIMER_COUNT * 2;
    QueryHeapDesc.NodeMask = 1;
    QueryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    VRET( Graphics::g_device->CreateQueryHeap( &QueryHeapDesc, IID_PPV_ARGS( &gpuProfiler_queryHeap ) ) );
    PRINTINFO( "QueryHeap created" );
	gpuProfiler_queryHeap->SetName(L"GPU_Profiler QueryHeap");

    return S_OK;
}

void GPU_Profiler::ShutDown()
{
	if (gpuProfiler_readbackBuffer != nullptr) gpuProfiler_readbackBuffer->Release();
	if (gpuProfiler_queryHeap != nullptr) gpuProfiler_queryHeap->Release();
    delete[] gpuProfiler_timerNameArray;
    delete[] gpuProfiler_timeStampBufferCopy;
    DeleteCriticalSection( &gpuProfiler_critialSection );
}

void GPU_Profiler::ProcessAndReadback()
{
	Graphics::g_cmdListMngr.WaitForFence(gpuProfiler_fence);

    HRESULT hr;
    D3D12_RANGE range;
    range.Begin = 0;
    range.End = MAX_TIMER_COUNT * 2 * sizeof( uint64_t );
    V( gpuProfiler_readbackBuffer->Map( 0, &range, reinterpret_cast< void** >( &gpuProfiler_timeStampBuffer ) ) );
    memcpy( gpuProfiler_timeStampBufferCopy, gpuProfiler_timeStampBuffer, gpuProfiler_timerCount*2*sizeof( uint64_t ));
	D3D12_RANGE EmptyRange = {};
    gpuProfiler_readbackBuffer->Unmap( 0, &EmptyRange);
	
	CommandContext& context = CommandContext::Begin();
	context.ResolveTimeStamps(gpuProfiler_readbackBuffer, gpuProfiler_queryHeap, 2 * gpuProfiler_timerCount);
	gpuProfiler_fence = context.Finish();
}

void GPU_Profiler::DrawStats(GraphicsContext& gfxContext)
{
	TextContext txtContext(gfxContext);
	txtContext.Begin();
	txtContext.ResetCursor(10, 10);
	txtContext.SetTextSize(15.f);
	for (uint32_t idx = 0; idx < gpuProfiler_timerCount; idx++)
	{
		wchar_t temp[128];
		GPU_Profiler::GetTimingStr(idx, temp);
		txtContext.DrawString(wstring(temp));
		txtContext.NewLine();
	}
	txtContext.End();

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

uint32_t GPU_Profiler::GetTimingStr( uint32_t idx, wchar_t* outStr )
{
    ASSERT( idx < MAX_TIMER_COUNT );
    if ( gpuProfiler_timerNameArray[idx].length() == 0 )
        return 0;
    double result = gpuProfiler_timeStampBufferCopy[idx * 2 + 1] * gpuProfiler_GPUTickDelta - gpuProfiler_timeStampBufferCopy[idx * 2] * gpuProfiler_GPUTickDelta;
    swprintf( outStr, L"%s:%4.3fms \0", gpuProfiler_timerNameArray[idx].c_str(), result );
    return ( uint32_t ) wcslen( outStr );
}

GPUProfileScope::GPUProfileScope(CommandContext& Context, const wchar_t* szName)
    :m_Context(Context)
{
    Context.PIXBeginEvent(szName);
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
	ASSERT(m_idx < GPU_Profiler::MAX_TIMER_COUNT);
	m_Context.InsertTimeStamp(gpuProfiler_queryHeap, m_idx * 2);
}

GPUProfileScope::~GPUProfileScope()
{
	ASSERT(m_idx < GPU_Profiler::MAX_TIMER_COUNT);
	m_Context.InsertTimeStamp(gpuProfiler_queryHeap, m_idx * 2 + 1);
	m_Context.PIXEndEvent();
}