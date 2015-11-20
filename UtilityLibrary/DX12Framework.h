#pragma once
// To use this framework, create new project in this solution
// 1. right click new project's References and add UtilityLibrary as reference
// 2. open project property and open configuration manager delete all 
//    Win32 configurations and for each configuration only build 
//    UtilityLibrary and the new project not any others
// 3. go to Configuration Properties -> General, change Target Platform
//    Version to 10.0+
// 4. go to C/C++ -> General, add '../UtilityLibrary' to Include Directories
// 5. go to C/C++ -> Preprocessor add '_CRT_SECURE_NO_WARNINGS;NDEBUG;_NDEBUG;'
//    to Release and '_CRT_SECURE_NO_WARNINGS;_DEBUG;DEBUG;' to Debug
// 6. go to C/C++ -> Precompiled Headers change Precompiled Header to 'Use'
//    and for stdafx.cpp change it to 'Create'
// 7. go to Linker -> Input, add 'd3d12.lib;dxgi.lib;d3dcompiler.lib;dxguid.lib;'
// 8. for .hlsl files change it item type to Custom Build Tool, and change the 
//    Content attribute to Yes
// 9. go to hlsl files' Configuration Properties -> Custom Build Tool -> General
//    add Command Line 'copy %(Identity) "$(OutDir)" >NUL'
//    add Outputs '$(OutDir)\%(Identity)' and Treat Output As Content 'Yes'

#include "DXHelper.h"

using namespace DirectX;
using namespace Microsoft::WRL;

class DX12Framework
{
public:
	DX12Framework( UINT width, UINT height, std::wstring name );
	virtual ~DX12Framework();

	int Run( HINSTANCE hInstance, int nCmdShow );
	void SetCustomWindowText( LPCWSTR text );

protected:
	struct Settings
	{
		bool enableFullScreen;
		bool warpDevice;
		DXGI_SWAP_CHAIN_DESC swapChainDesc;
		
		// Free to be changed after init
		//Vsync
		bool vsync;

	};

	// Framework interface for rendering loop
	virtual void ParseCommandLineArgs();
	virtual void OnConfiguration(Settings* config);
	virtual HRESULT OnInit( ID3D12Device* device ) = 0;
	virtual HRESULT OnSizeChanged() = 0;
	virtual void OnUpdate() = 0;
	virtual void OnRender() = 0;
	virtual void OnDestroy() = 0;
	virtual bool OnEvent( MSG* msg ) = 0;

	std::wstring GetAssetFullPath( LPCWSTR assetName );
	HRESULT ResizeBackBuffer();

	// temp variable for display GPU timing
	wchar_t strCustom[256];
	
	// Framework level gfx resource
	ComPtr<ID3D12Device> framework_gfxDevice;
	ComPtr<IDXGISwapChain3> framework_gfxSwapChain;
	ComPtr<ID3D12CommandQueue> framework_gfxBackbufferGfxCmdQueue;
	// gfx settings
	Settings framework_config;
	// Window handle.
	HWND framework_hwnd;

private:

	// Root assets path.
	std::wstring framework_assetsPath;
	// Window title.
	std::wstring framework_title;

	static LRESULT CALLBACK WindowProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam );
	void RenderLoop();
	HRESULT FrameworkInit();
	void FrameworkUpdate();
	void FrameworkRender();
	void FrameworkDestory();
	void FrameworkResize();
	void FrameworkHandleEvent( MSG* msg );
};
