#include "LibraryHeader.h"
#include "DX12Framework.h"
#include "Utility.h"
#include "DXHelper.h"
#include <shellapi.h>

#ifdef _DEBUG
#define ATTACH_CONSOLE 1
#endif
using namespace Microsoft::WRL;

CRITICAL_SECTION outputCS;

DX12Framework::DX12Framework(UINT width, UINT height, std::wstring name):
	_stopped(false),_error(false),m_width(width),m_height(height),
	m_newWidth(width),m_newHeight(height),m_vsync(false),m_useWarpDevice(false)
{
	// Initialize output critical section
	InitializeCriticalSection( &outputCS );

#if ATTACH_CONSOLE
	AttachConsole();
#endif

	ParseCommandLineArgs();

	m_title = name + (m_useWarpDevice ? L" (WARP)" : L"");
	PRINTINFO( L"%s start", m_title.c_str() );
	WCHAR assetsPath[512];
	GetAssetsPath(assetsPath, _countof(assetsPath));
	m_assetsPath = assetsPath;
	m_aspectRatio = static_cast< float >( m_width ) / static_cast< float >( m_height );
}

DX12Framework::~DX12Framework()
{
	// Delete output critical section
	DeleteCriticalSection( &outputCS );
}

void DX12Framework::RenderLoop()
{
	// Initialize the sample. OnInit is defined in each child-implementation of DXSample.
	OnInit();

	// Tell UI thread DX is ready, so it can show the window
	SetEvent( m_dxReady );

	// Initialize performance counters
	UINT64 perfCounterFreq = 0;
	UINT64 lastPerfCount = 0;
	QueryPerformanceFrequency( ( LARGE_INTEGER* ) &perfCounterFreq );
	QueryPerformanceCounter( ( LARGE_INTEGER* ) &lastPerfCount );

	// main loop
	double elapsedTime = 0.0;
	double frameTime = 0.0;

	while ( !_stopped && !_error )
	{
		// Get time delta
		UINT64 count;
		QueryPerformanceCounter( ( LARGE_INTEGER* ) &count );
		auto rawFrameTime = ( double ) ( count - lastPerfCount ) / perfCounterFreq;
		elapsedTime += rawFrameTime;
		lastPerfCount = count;

		// Maintaining absolute time sync is not important in this demo so we can err on the "smoother" side
		double alpha = 0.1f;
		frameTime = alpha * rawFrameTime + ( 1.0f - alpha ) * frameTime;

		// Update GUI
		{
			wchar_t buffer[256];
			swprintf( buffer, 256, L"%ls - %4.1f ms  %.0f fps", m_title.c_str(), 1000.f * frameTime, 1.0f / frameTime );
			SetWindowText( m_hwnd, buffer );
		}

		if ( _resize.load( memory_order_acquire ) )
		{
			_resize.store( false, memory_order_relaxed );
			m_width = m_newWidth;
			m_height = m_newHeight;
			this->m_aspectRatio = static_cast< float >( m_width ) / static_cast< float >( m_height );
			OnSizeChanged();
			PRINTINFO( "Window resize to %d x %d", m_width, m_height );
		}
		OnUpdate();
		OnRender();
	}
	OnDestroy();
}

int DX12Framework::Run(HINSTANCE hInstance, int nCmdShow)
{
	// Initialize the window class.
	WNDCLASSEX windowClass = { 0 };
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = hInstance;
	windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	windowClass.lpszClassName = L"WindowClass1";
	RegisterClassEx(&windowClass);

	LONG top = 300;
	LONG left = 300;

#if ATTACH_CONSOLE
	RECT rect;
	HWND con_hwnd = GetConsoleWindow();
	if ( GetWindowRect( con_hwnd, &rect ) )
	{
		top = rect.top + GetSystemMetrics( SM_CYFRAME ) + GetSystemMetrics( SM_CYCAPTION ) +
			GetSystemMetrics( SM_CXPADDEDBORDER );
		left = rect.right;
	}
#endif

	RECT windowRect = { left, top, left + static_cast<LONG>(m_width), top + static_cast<LONG>(m_height) };
	AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	// Create the window and store a handle to it.
	m_hwnd = CreateWindowEx(NULL,
		L"WindowClass1",
		m_title.c_str(),
		WS_OVERLAPPEDWINDOW,
		windowRect.left,
		windowRect.top,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		NULL,		// We have no parent window, NULL.
		NULL,		// We aren't using menus, NULL.
		hInstance,
		this);		// We aren't using multiple windows, NULL.


	// Create an event handle to use for frame synchronization.
	m_dxReady = CreateEvent( nullptr, FALSE, FALSE, nullptr );
	if ( m_dxReady == nullptr )
	{
		HRESULT hr;
		VRET( HRESULT_FROM_WIN32( GetLastError() ) );
	}


	std::thread renderThread( &DX12Framework::RenderLoop, this );
	thread_guard g( renderThread );

	// Wait for DX init to finish, so window will show up with meaningful content
	WaitForSingleObject( m_dxReady, INFINITE );
	ShowWindow(m_hwnd, nCmdShow);

	EnableMouseInPointer( TRUE );

	// Main sample loop.
	MSG msg = { 0 };
	while ( msg.message != WM_QUIT )
	{
		// Process any messages in the queue.
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			// Pass events into our sample.
			OnEvent(msg);
		}
	}
	_stopped = true;
	// Return this part of the WM_QUIT message to Windows.
	return static_cast<char>(msg.wParam);
}

// Helper function for resolving the full path of assets.
std::wstring DX12Framework::GetAssetFullPath(LPCWSTR assetName)
{
	return m_assetsPath + assetName;
}

// Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
// If no such adapter can be found, *ppAdapter will be set to nullptr.
_Use_decl_annotations_
void DX12Framework::GetHardwareAdapter( IDXGIFactory4* pFactory, IDXGIAdapter1** ppAdapter )
{
	ComPtr<IDXGIAdapter1> adapter;
	*ppAdapter = nullptr;

	for ( UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1( adapterIndex, &adapter ); ++adapterIndex )
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1( &desc );

		if ( desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE )
		{
			// Don't select the Basic Render Driver adapter.
			// If you want a software adapter, pass in "/warp" on the command line.
			continue;
		}

		// Check to see if the adapter supports Direct3D 12, but don't create the
		// actual device yet.
		if ( SUCCEEDED( D3D12CreateDevice( adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof( ID3D12Device ), nullptr ) ) )
		{
			adapter->GetDesc1( &desc );
			PRINTINFO( L"D3D12-capable hardware found:  %s (%u MB)", desc.Description, desc.DedicatedVideoMemory >> 20 );
			break;
		}
	}

	*ppAdapter = adapter.Detach();
}

// Helper function for setting the window's title text.
void DX12Framework::SetCustomWindowText(LPCWSTR text)
{
	std::wstring windowText = m_title + L": " + text;
	SetWindowText(m_hwnd, windowText.c_str());
}

// Helper function for parsing any supplied command line args.
void DX12Framework::ParseCommandLineArgs()
{
	int argc;
	LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	for (int i = 1; i < argc; ++i)
	{
		if (_wcsnicmp(argv[i], L"-warp", wcslen(argv[i])) == 0 || 
			_wcsnicmp(argv[i], L"/warp", wcslen(argv[i])) == 0)
		{
			m_useWarpDevice = true;
		}
	}
	LocalFree(argv);
}

// Main message handler for the sample.
LRESULT CALLBACK DX12Framework::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	DX12Framework* pSample = reinterpret_cast< DX12Framework* >( GetWindowLongPtr( hWnd, GWLP_USERDATA ) );

	// Handle destroy/shutdown messages.
	switch (message)
	{
	case WM_CREATE:
		{
			// Save a pointer to the DXSample passed in to CreateWindow.
			LPCREATESTRUCT pCreateStruct = reinterpret_cast< LPCREATESTRUCT >( lParam );
			SetWindowLongPtr( hWnd, GWLP_USERDATA, reinterpret_cast< LONG_PTR >( pCreateStruct->lpCreateParams ) );
		}
		return 0;

	case WM_SIZE:
		if(  wParam!= SIZE_MINIMIZED && pSample )
		{
			RECT clientRect = {};
			GetClientRect( hWnd, &clientRect );
			UINT _width = clientRect.right - clientRect.left;
			UINT _height = clientRect.bottom - clientRect.top;
			if ( pSample->m_width != _width || pSample->m_height != _height )
			{
				pSample->m_newWidth = _width;
				pSample->m_newHeight = _height;
				pSample->_resize.store( true, memory_order_release );
			}
		}
		return 0;

	case WM_KEYDOWN:
		if ( lParam & ( 1 << 30 ) ) {
			// Ignore repeats
			return 0;
		}
		switch ( wParam ) {
			//case VK_SPACE:
		case VK_ESCAPE:
			SendMessage( hWnd, WM_CLOSE, 0, 0 );
			return 0;
			// toggle vsync;
		case 'V':
			pSample->m_vsync = !pSample->m_vsync;
			PRINTINFO( "Vsync is %s", pSample->m_vsync ? "on" : "off" );
			return 0;
		} // Switch on key code
		return 0;

	case WM_DESTROY:
		PostQuitMessage( 0 );
		return 0;
	}

	// Handle any messages the switch statement didn't.
	return DefWindowProc( hWnd, message, wParam, lParam );
}
