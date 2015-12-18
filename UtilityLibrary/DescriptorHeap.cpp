#include "LibraryHeader.h"
#include "Graphics.h"
#include "Utility.h"
#include "DescriptorHeap.h"

using namespace Microsoft::WRL;

DescriptorHandle::DescriptorHandle()
{
    mCPUHandle.ptr = ~0ull;
    mGPUHandle.ptr = ~0ull;
    hasGpuHandle = false;
}

DescriptorHandle::DescriptorHandle( CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle, CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle, bool shaderVisible)
    :mCPUHandle(cpuHandle),mGPUHandle(gpuHandle),hasGpuHandle(shaderVisible)
{
}

CD3DX12_CPU_DESCRIPTOR_HANDLE DescriptorHandle::GetCPUHandle()
{
    return mCPUHandle;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE DescriptorHandle::GetGPUHandle()
{
    ASSERT( hasGpuHandle );
    return mGPUHandle;
}

DescriptorHeap::DescriptorHeap( ID3D12Device* device, UINT maxDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible )
    : mDevice( device ), mMaxSize( maxDescriptors )
{
    HRESULT hr;

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = type;
    desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NumDescriptors = mMaxSize;

    V( mDevice->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &mHeap ) ) );

    mShaderVisible = shaderVisible;

    mCPUBegin = mHeap->GetCPUDescriptorHandleForHeapStart();
    mHandleIncrementSize = mDevice->GetDescriptorHandleIncrementSize( desc.Type );

    if ( shaderVisible ) {
        mGPUBegin = mHeap->GetGPUDescriptorHandleForHeapStart();
    }
}

DescriptorHandle DescriptorHeap::Append()
{
    ASSERT( mCurrentSize < mMaxSize );
    DescriptorHandle ret( CPU( mCurrentSize ), GPU( mCurrentSize ), mShaderVisible );
    mCurrentSize++;
    return ret;
}