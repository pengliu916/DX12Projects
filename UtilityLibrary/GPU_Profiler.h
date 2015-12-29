#pragma once

using namespace DirectX;
using namespace Microsoft::WRL;

namespace GPU_Profiler
{
    const uint16_t MAX_TIMER_NAME_LENGTH = 64;
    const uint16_t MAX_TIMER_COUNT = 512;

    HRESULT CreateResource();
    void ShutDown();
    void StartTimeStampPunch( ID3D12GraphicsCommandList* cmdlist, uint32_t idx );
    void StopTimeStampPunch( ID3D12GraphicsCommandList* cmdlist, uint32_t idx );
    void ProcessAndReadback();
    double ReadTimer( uint32_t idx, double* start = nullptr, double* stop = nullptr );
    uint32_t GetTimingStr( uint32_t idx, char* outStr );
};

class GPUProfileScope
{
public:
    GPUProfileScope( ID3D12GraphicsCommandList* cmdlist, const char* szName );
    ~GPUProfileScope();

    // Prevent copying
    GPUProfileScope( GPUProfileScope const& ) = delete;
    GPUProfileScope& operator= ( GPUProfileScope const& ) = delete;

private:
    ID3D12GraphicsCommandList* m_cmdlist;
    uint32_t m_idx;
};

// Anon macros, used to create anonymous variables in macros.
#define ANON_INTERMEDIATE(a,b) a##b
#define ANON(a) ANON_INTERMEDIATE(a,__LINE__)

// attention: need to scope this macro and make sure their whole life span is during cmdlist record state
#ifndef RELEASE
#define GPU_PROFILE(d,x)						GPUProfileScope ANON(pixProfile)(d, x)
#define GPU_PROFILE_FUNCTION(d)					GPUProfileScope ANON(pixProfile)(d, __FUNCTION__ )
#else
#define GPU_PROFILE(d,x)  ((void)0)
#define GPU_PROFILE_FUNCTION(d)	 ((void)0)
#endif