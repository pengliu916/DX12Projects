#pragma once

#include <vector>
#include <queue>

// Constant blocks must be multiples of 16 constants @ 16 bytes each
#define DEFAULT_ALIGN 256

struct DynAlloc
{
    DynAlloc( ComPtr<ID3D12Resource>& BaseResource ) :m_pGfxResource( BaseResource ){};

    ComPtr<ID3D12Resource>&     m_pGfxResource;
    D3D12_GPU_VIRTUAL_ADDRESS   m_GpuVirtualAddr;

    uint64_t                    m_Offset;
    uint64_t                    m_Size;

    void*                       m_pData;
};

class LinearAllocationPage
{
public:
    LinearAllocationPage( ID3D12Resource* pGfxResource );
    ~LinearAllocationPage();

    LinearAllocationPage& operator=( LinearAllocationPage const& ) = delete;
    LinearAllocationPage( LinearAllocationPage const& ) = delete;

    void*                       m_CpuVirtualAddr;
    D3D12_GPU_VIRTUAL_ADDRESS   m_GpuVirtualAddr;
    ComPtr<ID3D12Resource>      m_pGfxResource;
};

enum LinearAllocatorType
{
    kInvalidAllocator = -1,
    kGpuExclusive = 0,		// DEFAULT   GPU-writable (via UAV)
    kCpuWritable = 1,		// UPLOAD CPU-writable (but write combined)
    kNumAllocatorTypes
};

enum
{
    kGpuAllocatorPageSize = 0x10000,	// 64K
    kCpuAllocatorPageSize = 0x200000	// 2MB
};

class LinearAllocatorPageMngr
{
public:
    LinearAllocatorPageMngr( LinearAllocatorType );
    ~LinearAllocatorPageMngr();

    LinearAllocationPage* RequestPage();
    void DiscardPages( uint64_t FenceID, const vector<LinearAllocationPage*>& Pages );
    void Destory();

    //LinearAllocatorPageMngr( LinearAllocatorPageMngr const& ) = delete;
    LinearAllocatorPageMngr& operator= ( LinearAllocatorPageMngr const& ) = delete;

private:
    LinearAllocationPage* CreateNewPage();

    LinearAllocatorType                             m_AllocationType;
    vector<unique_ptr<LinearAllocationPage>>        m_PagePool;
    queue<pair<uint64_t, LinearAllocationPage*>>    m_RetiredPages;
    queue<LinearAllocationPage*>                    m_AvailablePages;
    CRITICAL_SECTION                                m_CS;
};

class LinearAllocator
{
public:
    LinearAllocator( LinearAllocatorType );

    static void DestroyAll();

    DynAlloc Allocate( uint64_t SizeInByte, uint64_t Alignment = DEFAULT_ALIGN );
    void CleanupUsedPages( uint64_t FenceID );

    LinearAllocator( LinearAllocator const& ) = delete;
    LinearAllocator& operator= ( LinearAllocator const& ) = delete;
private:
    static LinearAllocatorPageMngr  sm_PageMngr[2];

    LinearAllocatorType             m_AllocationType;
    uint64_t                        m_PageSize;
    uint64_t                        m_CurOffset;
    LinearAllocationPage*           m_CurPage;
    vector<LinearAllocationPage*>   m_RetiredPages;
};

