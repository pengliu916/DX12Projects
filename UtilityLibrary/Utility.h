#pragma once
#include <windows.h>
#include <string>
#include <stdlib.h>
#include <io.h>
#include <iostream>
#include <fcntl.h>

using namespace std;

extern  CRITICAL_SECTION outputCS;
class CriticalSectionScope
{
public:
	CriticalSectionScope( CRITICAL_SECTION *cs ) :m_cs( cs )
	{
		EnterCriticalSection( m_cs );
	}
	~CriticalSectionScope()
	{
		LeaveCriticalSection( m_cs );
	}
private:
	CRITICAL_SECTION *m_cs;
};

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

// maximum number of lines the output console should have

static const WORD MAX_CONSOLE_LINES = 500;
static WORD g_defaultWinConsoleAttrib;

inline void AttachConsole() {
	bool has_console = ::AttachConsole( ATTACH_PARENT_PROCESS ) == TRUE;
	if ( !has_console ) {
		// We weren't launched from a console, so make one.
		has_console = AllocConsole() == TRUE;
	}
	if ( !has_console ) {
		return;
	}
	// set the screen buffer to be big enough to let us scroll text
	CONSOLE_SCREEN_BUFFER_INFO coninfo;
	GetConsoleScreenBufferInfo( GetStdHandle( STD_OUTPUT_HANDLE ), &coninfo );
	coninfo.dwSize.Y = MAX_CONSOLE_LINES;
	g_defaultWinConsoleAttrib = coninfo.wAttributes;
	SetConsoleScreenBufferSize( GetStdHandle( STD_OUTPUT_HANDLE ), coninfo.dwSize );

	for ( auto &file : { stdout, stderr } ) {
		freopen( "CONOUT$", "w", file );
		setvbuf( file, nullptr, _IONBF, 0 );
	}
}

enum consoleTextColor
{
	CONTXTCOLOR_DEFAULT = 0,
	CONTXTCOLOR_RED,
	CONTXTCOLOR_GREEN,
	CONTXTCOLOR_YELLOW,
	CONTXTCOLOR_BLUE,
	CONTXTCOLOR_AQUA,
	CONTXTCOLOR_FUSCHIA,
};

inline void ConsoleColorSet( int colorcode )
{
	HANDLE stdout_handle;
	CONSOLE_SCREEN_BUFFER_INFO info;
	WORD attrib = 0;

	stdout_handle = GetStdHandle( STD_OUTPUT_HANDLE );
	GetConsoleScreenBufferInfo( stdout_handle, &info );

	switch ( colorcode )
	{
	case CONTXTCOLOR_RED:
		attrib = FOREGROUND_INTENSITY | FOREGROUND_RED;
		break;
	case CONTXTCOLOR_GREEN:
		attrib = FOREGROUND_INTENSITY | FOREGROUND_GREEN;
		break;
	case CONTXTCOLOR_YELLOW:
		attrib = FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN;
		break;
	case CONTXTCOLOR_BLUE:
		attrib = FOREGROUND_INTENSITY | FOREGROUND_BLUE;
		break;
	case CONTXTCOLOR_AQUA:
		attrib = FOREGROUND_INTENSITY | FOREGROUND_GREEN | FOREGROUND_BLUE;
		break;
	case CONTXTCOLOR_FUSCHIA:
		attrib = FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_BLUE;
		break;
	default:
		attrib = g_defaultWinConsoleAttrib;
		break;
	}

	SetConsoleTextAttribute( stdout_handle, attrib );
}

enum MessageType
{
	MSG_WARNING = 0,
	MSG_ERROR,
	MSG_INFO,

	MSGTYPECOUNT,
};

#define MAX_MSG_LENGTH 1024
inline void PrintMsg( MessageType msgType, const wchar_t* szFormat, va_list arg )
{
	wchar_t szBuff[MAX_MSG_LENGTH];
	size_t preStrLen = 0;
	wchar_t wcsWarning[] =	L"[ WARN	]: ";
	wchar_t wcsError[] =	L"[ ERROR	]: ";
	wchar_t wcsInfo[] =		L"[ INFO	]: ";
	switch ( msgType )
	{
	case MSG_WARNING:
		preStrLen = wcslen( wcsWarning );
		wcsncpy( szBuff, wcsWarning, preStrLen );
		break;
	case MSG_ERROR:
		preStrLen = wcslen( wcsError );
		wcsncpy( szBuff,  wcsError, preStrLen );
		break;
	case MSG_INFO:
		preStrLen = wcslen( wcsInfo );
		wcsncpy( szBuff,  wcsInfo, preStrLen );
		break;
	}
	_vswprintf( szBuff + preStrLen, szFormat, arg );
	size_t length = wcslen( szBuff );
	assert( length < MAX_MSG_LENGTH - 1 );
	szBuff[length] = L'\n';
	szBuff[length + 1] = L'\0';
	wprintf( szBuff );
	fflush( stdout );
}

inline void PrintWarning( const wchar_t *fmt, ... )
{
	CriticalSectionScope lock( &outputCS );
	va_list ap;
	ConsoleColorSet( CONTXTCOLOR_YELLOW );
	va_start( ap, fmt );
	PrintMsg( MSG_WARNING, fmt, ap );
	va_end( ap );
	ConsoleColorSet( CONTXTCOLOR_DEFAULT );
}

inline void PrintError( const wchar_t *fmt, ... )
{
	CriticalSectionScope lock( &outputCS );
	va_list ap;
	ConsoleColorSet( CONTXTCOLOR_RED );
	va_start( ap, fmt );
	PrintMsg( MSG_ERROR, fmt, ap );
	va_end( ap );
	ConsoleColorSet( CONTXTCOLOR_DEFAULT );
}

inline void PrintInfo( const wchar_t *fmt, ... )
{
	CriticalSectionScope lock( &outputCS );
	va_list ap;
	ConsoleColorSet( CONTXTCOLOR_GREEN );
	va_start( ap, fmt );
	PrintMsg( MSG_INFO, fmt, ap );
	va_end( ap );
	ConsoleColorSet( CONTXTCOLOR_DEFAULT );
}
