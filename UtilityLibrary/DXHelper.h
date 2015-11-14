#pragma once
#include "Utility.h"

#define CBUFFER_ALIGN __declspec(align(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))

inline void GetAssetsPath(_Out_writes_(pathSize) WCHAR* path, UINT pathSize)
{
	if (path == nullptr)
	{
		throw;
	}

	DWORD size = GetModuleFileName(nullptr, path, pathSize);

	if (size == 0 || size == pathSize)
	{
		// Method failed or path was truncated.
		throw;
	}

	WCHAR* lastSlash = wcsrchr(path, L'\\');
	if (lastSlash)
	{
		*(lastSlash+1) = NULL;
	}
}

inline HRESULT CompileShaderFromFile( LPCWSTR pFileName,const D3D_SHADER_MACRO* pDefines, ID3DInclude* pInclude, 
									  LPCSTR pEntrypoint,LPCSTR pTarget,UINT Flags1, UINT Flags2,ID3DBlob** ppCode )
{
	HRESULT hr;
#if defined( DEBUG ) || defined( _DEBUG )
	// Set the D3DCOMPILE_DEBUG flag to embed debug information in the shaders.
	// Setting this flag improves the shader debugging experience, but still allows 
	// the shaders to be optimized and to run exactly the way they will run in 
	// the release configuration of this program.
	Flags1 |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	ID3DBlob* pErrorBlob = nullptr;
	hr = D3DCompileFromFile( pFileName, pDefines, pInclude, pEntrypoint, pTarget, Flags1, Flags2, ppCode, &pErrorBlob );
	if ( pErrorBlob )
	{
		PRINTERROR( reinterpret_cast< const char* >( pErrorBlob->GetBufferPointer() ) );
		pErrorBlob->Release();
	}

	return hr;
}

inline HRESULT ReadDataFromFile(LPCWSTR filename, byte** data, UINT* size)
{
	using namespace Microsoft::WRL;

	CREATEFILE2_EXTENDED_PARAMETERS extendedParams = { 0 };
	extendedParams.dwSize = sizeof(CREATEFILE2_EXTENDED_PARAMETERS);
	extendedParams.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
	extendedParams.dwFileFlags = FILE_FLAG_SEQUENTIAL_SCAN;
	extendedParams.dwSecurityQosFlags = SECURITY_ANONYMOUS;
	extendedParams.lpSecurityAttributes = nullptr;
	extendedParams.hTemplateFile = nullptr;

	Wrappers::FileHandle file(
		CreateFile2(
			filename,
			GENERIC_READ,
			FILE_SHARE_READ,
			OPEN_EXISTING,
			&extendedParams
			)
		);

	if (file.Get() == INVALID_HANDLE_VALUE)
	{
		throw new std::exception();
	}

	FILE_STANDARD_INFO fileInfo = { 0 };
	if (!GetFileInformationByHandleEx(
		file.Get(),
		FileStandardInfo,
		&fileInfo,
		sizeof(fileInfo)
		))
	{
		throw new std::exception();
	}

	if (fileInfo.EndOfFile.HighPart != 0)
	{
		throw new std::exception();
	}

	*data = reinterpret_cast<byte*>(malloc(fileInfo.EndOfFile.LowPart));
	*size = fileInfo.EndOfFile.LowPart;

	if (!ReadFile(
		file.Get(),
		*data,
		fileInfo.EndOfFile.LowPart,
		nullptr,
		nullptr
		))
	{
		throw new std::exception();
	}

	return S_OK;
}
