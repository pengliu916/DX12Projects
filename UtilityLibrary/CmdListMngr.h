#pragma once

#include <vector>
#include <queue>

#include "Utility.h"

class CmdListMngr
{
public:
    CmdListMngr();
    ~CmdListMngr();

    // Prevent copying
    CmdListMngr( CmdListMngr const& ) = delete;
    CmdListMngr& operator= ( CmdListMngr const& ) = delete;

    HRESULT CreateResource( ID3D12Device* pDevice );
    void Shutdown();
    uint64_t IncrementFence( void );
    bool IsFenceComplete( uint64_t FenceValue );
    void WaitForFence( uint64_t FenceValue );
    void IdleGPU( void ) { WaitForFence( IncrementFence() ); }

    ID3D12CommandQueue* GetCommandQueue() { return m_CommandQueue; }

    HRESULT CreateNewCommandList( ID3D12GraphicsCommandList** List, ID3D12CommandAllocator** Allocator );
    uint64_t ExecuteCommandList( ID3D12CommandList* List );
    ID3D12CommandAllocator* RequestAllocator( void );
    void DiscardAllocator( uint64_t FenceValueForReset, ID3D12CommandAllocator* Allocator );

private:
    ID3D12Device* m_Device;
    ID3D12CommandQueue* m_CommandQueue;

    std::vector<ID3D12CommandAllocator*> m_AllocatorPool;
    std::queue<std::pair<uint64_t, ID3D12CommandAllocator*>> m_ReadyAllocators;
    CRITICAL_SECTION m_AllocatorCS;
    CRITICAL_SECTION m_FenceCS;
    CRITICAL_SECTION m_EventCS;

    ID3D12Fence* m_pFence;
    uint64_t m_NextFenceValue;
    uint64_t m_LastCompletedFenceValue;
    HANDLE m_FenceEventHandle;
};

