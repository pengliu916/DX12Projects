#include "LibraryHeader.h"
#include "DX12Framework.h"
#include "Utility.h"
#include "DXHelper.h"
#include "GPU_Profiler.h"

namespace{

	double m_GPUTickDelta;
	uint32_t m_timerCount;
	unordered_map<string, uint32_t> m_timerNameIdxMap;
	string* m_timerNameArray;

	ComPtr<ID3D12CommandAllocator> m_cmdAllocator;
	ComPtr<ID3D12CommandQueue> m_cmdQueue;
	ComPtr<ID3D12GraphicsCommandList> m_cmdList;
	ComPtr<ID3D12Resource> m_readbackBuffer;
	ComPtr<ID3D12QueryHeap> m_queryHeap;
	ComPtr<ID3D12Fence> m_fence;
	uint64_t* m_timeStampBuffer;

	CRITICAL_SECTION m_profilerCS;
	uint64_t m_fenceValue;
	uint64_t m_lastCompletedFenceValue;
	HANDLE m_fenceEvent;

	bool IsFenceCompleted( uint64_t fenceValue )
	{
		if ( fenceValue > m_lastCompletedFenceValue )
			m_lastCompletedFenceValue = max( m_lastCompletedFenceValue, m_fence->GetCompletedValue() );
		return fenceValue <= m_lastCompletedFenceValue;
	}

	void WaitForFence( uint64_t fenceValue )
	{
		if ( IsFenceCompleted( fenceValue ) ) return;
		m_fence->SetEventOnCompletion( fenceValue, m_fenceEvent );
		WaitForSingleObject( m_fenceEvent, INFINITE );
		m_lastCompletedFenceValue = fenceValue;
	}
}

HRESULT GPU_Profiler::Init( ID3D12Device* device)
{
	HRESULT hr;

	// Initialize the array to store all timer name
	m_timerNameArray = new string[MAX_TIMER_COUNT];

	// Initialize output critical section
	InitializeCriticalSection( &m_profilerCS );

	// Describe and create the graphics command queue.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	VRET( device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &m_cmdQueue ) ) );
	DXDebugName( m_cmdQueue );

	uint64_t freq;
	m_cmdQueue->GetTimestampFrequency( &freq );
	m_GPUTickDelta = 1000.0 / static_cast< double >( freq );

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

	VRET( device->CreateCommittedResource( &HeapProps, D3D12_HEAP_FLAG_NONE, &BufferDesc,
										   D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &m_readbackBuffer ) ) );
	DXDebugName( m_readbackBuffer );

	D3D12_QUERY_HEAP_DESC QueryHeapDesc;
	QueryHeapDesc.Count = MAX_TIMER_COUNT * 2;
	QueryHeapDesc.NodeMask = 1;
	QueryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	VRET( device->CreateQueryHeap( &QueryHeapDesc, IID_PPV_ARGS( &m_queryHeap ) ) );
	PRINTINFO( "QueryHeap created" );
	DXDebugName( m_queryHeap );

	D3D12_RANGE range;
	range.Begin = 0;
	range.End = MAX_TIMER_COUNT * 2 * sizeof( uint64_t );
	V( m_readbackBuffer->Map( 0, &range, reinterpret_cast< void** >( &m_timeStampBuffer ) ) );


	VRET( device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &m_cmdAllocator ) ) );
	DXDebugName( m_cmdAllocator );

	// Create the graphics command list.
	VRET( device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_cmdAllocator.Get(), nullptr, IID_PPV_ARGS( &m_cmdList ) ) );
	DXDebugName( m_cmdList );

	// Close the commandlist since it's in record state after creation, and we have nothing to record yet
	VRET( m_cmdList->Close() );

	m_lastCompletedFenceValue = 0;
	m_fenceValue = 0;
	VRET( device->CreateFence( m_fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &m_fence ) ) );
	DXDebugName( m_fence );

	// Create an event handle to use for frame synchronization.
	m_fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );
	if ( m_fenceEvent == nullptr )
	{
		VRET( HRESULT_FROM_WIN32( GetLastError() ) );
	}
	return S_OK;
}

void GPU_Profiler::ShutDown()
{
	delete m_timerNameArray;
	DeleteCriticalSection( &m_profilerCS );
}

void GPU_Profiler::ProcessAndReadback()
{
	WaitForFence( m_fenceValue );
	m_cmdAllocator->Reset();
	m_cmdList->Reset( m_cmdAllocator.Get(), nullptr );
	m_cmdList->ResolveQueryData( m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2 * m_timerCount, m_readbackBuffer.Get(), 0 );
	m_cmdList->Close();
	ID3D12CommandList* ppCmdList[] = { m_cmdList.Get() };
	m_cmdQueue->ExecuteCommandLists( _countof( ppCmdList ), ppCmdList );
	m_cmdQueue->Signal( m_fence.Get(), ++m_fenceValue );
}

double GPU_Profiler::ReadTimer( uint32_t idx, double* start, double* stop )
{
	if ( idx >= MAX_TIMER_COUNT )
	{
		PRINTERROR( "idx = %d is beyond the bounderay %d", idx, MAX_TIMER_COUNT );
		return 0;
	}
	double _start = m_timeStampBuffer[idx * 2] * m_GPUTickDelta;
	double _stop = m_timeStampBuffer[idx * 2 + 1] * m_GPUTickDelta;
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
	if ( m_timerNameArray[idx].length() == 0 )
		return 0;
	double result = m_timeStampBuffer[idx * 2 + 1] * m_GPUTickDelta - m_timeStampBuffer[idx * 2] * m_GPUTickDelta;
	sprintf( outStr, "%s:%4.3fms ", m_timerNameArray[idx].c_str(), result );
	return (uint32_t)strlen(outStr);
}
void GPU_Profiler::StartTimeStampPunch( ID3D12GraphicsCommandList* cmdlist, uint32_t idx )
{
	if ( idx >= MAX_TIMER_COUNT )
	{
		PRINTERROR( "idx = %d is beyond the bounderay %d", idx, MAX_TIMER_COUNT );
		return ;
	}
	cmdlist->EndQuery( m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, idx * 2 );
}

void GPU_Profiler::StopTimeStampPunch( ID3D12GraphicsCommandList* cmdlist, uint32_t idx )
{
	if ( idx >= MAX_TIMER_COUNT )
	{
		PRINTERROR( "idx = %d is beyond the bounderay %d", idx, MAX_TIMER_COUNT );
		return;
	}
	cmdlist->EndQuery( m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, idx * 2 + 1 );
}

GPUProfileScope::GPUProfileScope( ID3D12GraphicsCommandList* cmdlist, const char* szName )
	:m_cmdlist( cmdlist )
{
	PIXBeginEvent( cmdlist, 0, szName );
	auto iter = m_timerNameIdxMap.find( szName );
	if ( iter == m_timerNameIdxMap.end() )
	{
		CriticalSectionScope lock( &m_profilerCS );
		m_idx = m_timerCount;
		m_timerNameArray[m_idx] = szName;
		m_timerNameIdxMap[szName] = m_timerCount++;
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