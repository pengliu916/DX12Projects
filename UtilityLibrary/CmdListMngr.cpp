#include "LibraryHeader.h"

#include "CmdListMngr.h"

CmdListMngr::CmdListMngr() : m_Device( nullptr ), m_CommandQueue( nullptr )
{
    InitializeCriticalSection( &m_AllocatorCS );
    InitializeCriticalSection( &m_FenceCS );
    InitializeCriticalSection( &m_EventCS );
}

CmdListMngr::~CmdListMngr()
{
    Shutdown();
    DeleteCriticalSection( &m_AllocatorCS );
    DeleteCriticalSection( &m_FenceCS );
    DeleteCriticalSection( &m_EventCS );
}

void CmdListMngr::Shutdown()
{
    if ( m_CommandQueue == nullptr ) return;
    
    CloseHandle( m_FenceEventHandle );
    
    for ( size_t i = 0; i < m_AllocatorPool.size(); ++i ) m_AllocatorPool[i]->Release();
    
    m_pFence->Release();
    m_CommandQueue->Release();
    m_CommandQueue = nullptr;
}

HRESULT CmdListMngr::CreateResource( ID3D12Device* pDevice )
{
    HRESULT hr;
    ASSERT( pDevice != nullptr );
    ASSERT( m_CommandQueue == nullptr );
    ASSERT( m_AllocatorPool.size() == 0 );

    m_Device = pDevice;

    D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    QueueDesc.NodeMask = 1;
    VRET( m_Device->CreateCommandQueue( &QueueDesc, IID_PPV_ARGS( &m_CommandQueue ) ) );
    m_CommandQueue->SetName( L"CommandListManager::m_CommandQueue" );

    m_NextFenceValue = 1;
    m_LastCompletedFenceValue = 0;
    VRET( m_Device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &m_pFence ) ) );
    m_pFence->SetName( L"CommandListManager::m_pFence" );
    m_pFence->Signal( 0 );

    m_FenceEventHandle = CreateEvent( nullptr, false, false, nullptr );
    ASSERT( m_FenceEventHandle != INVALID_HANDLE_VALUE );

    ASSERT( m_CommandQueue != nullptr );
    return S_OK;
}

HRESULT CmdListMngr::CreateNewCommandList( ID3D12GraphicsCommandList** List, ID3D12CommandAllocator** Allocator )
{
    HRESULT hr;
    *Allocator = RequestAllocator();
    VRET( m_Device->CreateCommandList( 1, D3D12_COMMAND_LIST_TYPE_DIRECT, *Allocator, nullptr, IID_PPV_ARGS( List ) ) );
    ( *List )->SetName( L"CommandList" );
    return S_OK;
}

uint64_t CmdListMngr::ExecuteCommandList( ID3D12CommandList* List )
{
    CriticalSectionScope LockGuard( &m_FenceCS );

    m_CommandQueue->ExecuteCommandLists( 1, &List );
    m_CommandQueue->Signal( m_pFence, m_NextFenceValue );

    return m_NextFenceValue++;
}

ID3D12CommandAllocator* CmdListMngr::RequestAllocator( void )
{
    CriticalSectionScope LockGuard( &m_AllocatorCS );
    ID3D12CommandAllocator* pAllocator = nullptr;
    HRESULT hr;

    if ( !m_ReadyAllocators.empty() )
    {
        std::pair<uint64_t, ID3D12CommandAllocator*>& AllocatorPair = m_ReadyAllocators.front();
        if ( IsFenceComplete( AllocatorPair.first ) )
        {
            pAllocator = AllocatorPair.second;
            V( pAllocator->Reset() );
            m_ReadyAllocators.pop();
        }
    }
    // If no allocator's were ready to be reused, create a new one
    if ( pAllocator == nullptr )
    {
        V( m_Device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &pAllocator ) ) );
        wchar_t AllocatorName[32];
        swprintf( AllocatorName, 32, L"CommandAllocator %zu", m_AllocatorPool.size() );
        pAllocator->SetName( AllocatorName );
        m_AllocatorPool.push_back( pAllocator );
    }
    return pAllocator;
}

void CmdListMngr::DiscardAllocator( uint64_t FenceValue, ID3D12CommandAllocator* Allocator )
{
    CriticalSectionScope LockGuard( &m_AllocatorCS );

    // That fence value indicates we are free to reset the allocator
    m_ReadyAllocators.push( std::make_pair( FenceValue, Allocator ) );
}

uint64_t CmdListMngr::IncrementFence( void )
{
    CriticalSectionScope LockGuard( &m_FenceCS );
    m_CommandQueue->Signal( m_pFence, m_NextFenceValue );
    return m_NextFenceValue++;
}

bool CmdListMngr::IsFenceComplete( uint64_t FenceValue )
{
    if ( FenceValue > m_LastCompletedFenceValue )
        m_LastCompletedFenceValue = max( m_LastCompletedFenceValue, m_pFence->GetCompletedValue() );

    return FenceValue <= m_LastCompletedFenceValue;
}

void CmdListMngr::WaitForFence( uint64_t FenceValue )
{
    if ( IsFenceComplete( FenceValue ) )
        return;
    {
        CriticalSectionScope LockGuard( &m_EventCS );

        m_pFence->SetEventOnCompletion( FenceValue, m_FenceEventHandle );
        WaitForSingleObject( m_FenceEventHandle, INFINITE );
        m_LastCompletedFenceValue = FenceValue;
    }
}
