#pragma once

#include "DXHelper.h"

class DX12Framework
{
public:
	DX12Framework(UINT width, UINT height, std::wstring name);
	virtual ~DX12Framework();

	int Run(HINSTANCE hInstance, int nCmdShow);
	void SetCustomWindowText(LPCWSTR text);

protected:

	void RenderLoop();
	
	virtual void OnInit() = 0;
	virtual void OnUpdate() = 0;
	virtual void OnRender() = 0;
	virtual void OnSizeChanged() = 0;
	virtual void OnDestroy() = 0;
	virtual bool OnEvent(MSG msg) = 0;

	std::wstring GetAssetFullPath(LPCWSTR assetName);

	void GetHardwareAdapter( _In_ IDXGIFactory4* pFactory, _Outptr_result_maybenull_ IDXGIAdapter1** ppAdapter );

	static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	// In multi-thread scenario, current thread may read old version of the following boolean due to 
	// unflushed cache, but these are not critical variables so we can live without heavy synchronization 
	// stuffs like std::atomic<bool> 
	volatile bool _stopped;
	volatile bool _error;
	// Viewport dimensions.
	volatile UINT m_width;
	volatile UINT m_height;
	volatile bool _resize;
	float m_aspectRatio;

	// Window handle.
	HWND m_hwnd;

	// Adapter info.
	bool m_useWarpDevice;

private:
	void ParseCommandLineArgs();

	// Root assets path.
	std::wstring m_assetsPath;

	// Window title.
	std::wstring m_title;

};
