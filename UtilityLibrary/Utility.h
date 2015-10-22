#pragma once
#include <windows.h>
#include <string>

//--------------------------------------------------------------------------------------
// Profiling/instrumentation support
//--------------------------------------------------------------------------------------
#if defined(PROFILE) || defined(DEBUG)
#define  DXDebugName(x)  DX_SetDebugName(x.Get(),L###x)
#endif
// Use DX_SetDebugName() to attach names to D3D objects for use by 
// SDKDebugLayer, PIX's object table, etc.
#if defined(PROFILE) || defined(DEBUG)
inline void DX_SetDebugName( _In_ IDXGIObject* pObj, _In_z_ const WCHAR* pwcsName )
{
	if ( pObj )
		pObj->SetPrivateData( WKPDID_D3DDebugObjectNameW, ( UINT ) wcslen( pwcsName ) * 2, pwcsName );
}
inline void DX_SetDebugName( _In_ ID3D12Device* pObj, _In_z_ const WCHAR* pwcsName )
{
	if ( pObj )
		pObj->SetName( pwcsName );
}
inline void DX_SetDebugName( _In_ ID3D12Resource* pObj, _In_z_ const WCHAR* pwcsName )
{
	if ( pObj )
		pObj->SetName( pwcsName );
}
inline void DX_SetDebugName( _In_ ID3D12DeviceChild* pObj, _In_z_ const WCHAR* pwcsName )
{
	if ( pObj )
		pObj->SetName( pwcsName );
}
#else
#define DX_SetDebugName( pObj, pwcsName )
#endif

void Trace( const wchar_t* szFormat, ... )
{
	wchar_t szBuff[1024];
	va_list arg;
	va_start( arg, szFormat );
	_vsnwprintf_s( szBuff, sizeof( szBuff ), szFormat, arg );
	va_end( arg );

	OutputDebugString( szBuff );
}