#pragma once
#include <windows.h>
#include <string>

void Trace( const wchar_t* szFormat, ... )
{
	wchar_t szBuff[1024];
	va_list arg;
	va_start( arg, szFormat );
	_vsnwprintf_s( szBuff, sizeof( szBuff ), szFormat, arg );
	va_end( arg );

	OutputDebugString( szBuff );
}