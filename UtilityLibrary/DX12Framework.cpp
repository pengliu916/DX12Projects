#include "LibraryHeader.h"
#include "DX12Framework.h"
#include "Utility.h"
#include "DXHelper.h"
#include <shellapi.h>

#include "GPU_Profiler.h"

#ifdef _DEBUG
#define ATTACH_CONSOLE 1
#endif

CRITICAL_SECTION outputCS;

namespace
{
	// temp variable for window resize
	uint32_t _width;
	uint32_t _height;

	// In multi-thread scenario, current thread may read old version of the following boolean due to 
	// unflushed cache etc. So to use flag in multi-thread cases, atomic bool is needed, and memory order semantic is crucial 
	std::atomic<bool> _resize;

	bool _terminated = false;
	bool _hasError = false;

	// DX init finish event handle
	HANDLE _dxReady;

	// Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
	// If no such adapter can be found, *ppAdapter will be set to nullptr.
	_Use_decl_annotations_
		void GetHardwareAdapter( IDXGIFactory4* pFactory, IDXGIAdapter1** ppAdapter )
	{
		ComPtr<IDXGIAdapter1> adapter;
		*ppAdapter = nullptr;
		bool found = false;

		for ( UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1( adapterIndex, &adapter ); ++adapterIndex )
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1( &desc );

			if ( desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE )
			{
				// Don't select the Basic Render Driver adapter.
				// If you want a software adapter, pass in "/warp" on the command line.
				PRINTINFO( L"D3D12-capable hardware found:  %s (%u MB)", desc.Description, desc.DedicatedVideoMemory >> 20 );
				continue;
			}

			// Check to see if the adapter supports Direct3D 12, but don't create the
			// actual device yet.
			if ( SUCCEEDED( D3D12CreateDevice( adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof( ID3D12Device ), nullptr ) ) )
			{
				adapter->GetDesc1( &desc );
				PRINTINFO( L"D3D12-capable hardware found (selected):  %s (%u MB)", desc.Description, desc.DedicatedVideoMemory >> 20 );
				if ( !found )
					*ppAdapter = adapter.Detach();
				found = true;
				//break;
			}
		}
		*ppAdapter = adapter.Detach();
	}
}


DX12Framework::DX12Framework( UINT width, UINT height, std::wstring name )
{
	// Initial system setting with default
	framework_config.enableFullScreen = false;
	framework_config.warpDevice = false;
	framework_config.vsync = false;
	framework_config.swapChainDesc = {};
	framework_config.swapChainDesc.Width = width;
	framework_config.swapChainDesc.Height = height;
	framework_config.swapChainDesc.BufferCount = 3;

	// Initialize output critical section
	InitializeCriticalSection( &outputCS );

#if ATTACH_CONSOLE
	AttachConsole();
#endif

	framework_title = name;

	PRINTINFO( L"%s start", name.c_str() );

	strCustom[0] = L'\0';
	WCHAR assetsPath[512];
	GetAssetsPath( assetsPath, _countof( assetsPath ) );
	framework_assetsPath = assetsPath;
}

DX12Framework::~DX12Framework()
{
	// Delete output critical section
	DeleteCriticalSection( &outputCS );
}

void DX12Framework::OnConfiguration( Settings* config )
{
	// Subclass can override this one to modify the default settings
}

HRESULT DX12Framework::FrameworkInit()
{
	HRESULT hr;

	framework_config.swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	framework_config.swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	framework_config.swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	framework_config.swapChainDesc.Stereo = FALSE;
	framework_config.swapChainDesc.SampleDesc.Count = 1;
	framework_config.swapChainDesc.SampleDesc.Quality = 0;
	framework_config.swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	framework_config.swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED; // Not used
	framework_config.swapChainDesc.Flags = 0;

	// Giving app a chance to modify the framework level resource settings
	OnConfiguration( &framework_config );

	ParseCommandLineArgs();

#ifdef _DEBUG
	// Enable the D3D12 debug layer.
	ComPtr<ID3D12Debug> debugController;
	if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &debugController ) ) ) )
		debugController->EnableDebugLayer();
	else
		PRINTWARN( L"Unable to enable D3D12 debug validation layer." )
#endif

		// Create directx device
		ComPtr<IDXGIFactory4> factory;
	VRET( CreateDXGIFactory1( IID_PPV_ARGS( &factory ) ) );
	if ( framework_config.warpDevice )
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		VRET( factory->EnumWarpAdapter( IID_PPV_ARGS( &warpAdapter ) ) );
		VRET( D3D12CreateDevice( warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &framework_gfxDevice ) ) );
		PRINTWARN( L"Warp Device created." )
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter( factory.Get(), &hardwareAdapter );
		VRET( D3D12CreateDevice( hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &framework_gfxDevice ) ) );
	}

	// Check Direct3D 12 feature hardware support (more usage refer Direct3D 12 sdk Capability Querying)
	D3D12_FEATURE_DATA_D3D12_OPTIONS options;
	framework_gfxDevice->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof( options ) );
	switch ( options.ResourceBindingTier )
	{
	case D3D12_RESOURCE_BINDING_TIER_1:
		PRINTWARN( L"Tier 1 is supported." );
		break;
	case D3D12_RESOURCE_BINDING_TIER_2:
		PRINTWARN( L"Tier 1 and 2 are supported." );
		break;
	case D3D12_RESOURCE_BINDING_TIER_3:
		PRINTWARN( L"Tier 1, 2 and 3 are supported." );
		break;
	default:
		break;
	}

	// Describe and create the graphics command queue which will update the backbuffer.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	VRET( framework_gfxDevice->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &framework_gfxBackbufferGfxCmdQueue ) ) );
	DXDebugName( framework_gfxBackbufferGfxCmdQueue );

	ASSERT( framework_config.swapChainDesc.BufferCount <= DXGI_MAX_SWAP_CHAIN_BUFFERS );
	// Create the swap chain
	ComPtr<IDXGISwapChain1> swapChain;
	// Swap chain needs the queue so that it can force a flush on it.
	VRET( factory->CreateSwapChainForHwnd( framework_gfxBackbufferGfxCmdQueue.Get(), framework_hwnd, &framework_config.swapChainDesc,NULL,NULL, &swapChain ) );
	VRET( swapChain.As( &framework_gfxSwapChain ) );
	DXDebugName( framework_gfxSwapChain );

	// Enable or disable full screen
	if(!framework_config.enableFullScreen) VRET( factory->MakeWindowAssociation( framework_hwnd, DXGI_MWA_NO_ALT_ENTER ) );

#ifndef RELEASE
	// Initialize GPU profiler system
	GPU_Profiler::Init( framework_gfxDevice.Get() );
#endif
	// Initialize the sample. OnInit is defined in each child-implementation of DXSample.
	OnInit( framework_gfxDevice.Get() );

	return hr;
}

void DX12Framework::FrameworkUpdate()
{
	OnUpdate();
}

void DX12Framework::FrameworkRender()
{
	OnRender();
#ifndef RELEASE
	GPU_Profiler::ProcessAndReadback();
#endif
}

void DX12Framework::FrameworkDestory()
{
	OnDestroy();
	CloseHandle( _dxReady );
}

void DX12Framework::FrameworkHandleEvent( MSG* msg )
{
	OnEvent( msg );
}

void DX12Framework::FrameworkResize()
{
	framework_config.swapChainDesc.Width = _width;
	framework_config.swapChainDesc.Height = _height;
	OnSizeChanged();
	PRINTINFO( "Window resize to %d x %d", framework_config.swapChainDesc.Width, framework_config.swapChainDesc.Height );
}

void DX12Framework::RenderLoop()
{
	SetThreadName( "Render Thread" );

	FrameworkInit();
	framework_title = framework_title + ( framework_config.warpDevice ? L" (WARP)" : L"" );
	// Tell UI thread DX is ready, so it can show the window
	SetEvent( _dxReady );

	// Initialize performance counters
	UINT64 perfCounterFreq = 0;
	UINT64 lastPerfCount = 0;
	QueryPerformanceFrequency( ( LARGE_INTEGER* ) &perfCounterFreq );
	QueryPerformanceCounter( ( LARGE_INTEGER* ) &lastPerfCount );

	// main loop
	double elapsedTime = 0.0;
	double frameTime = 0.0;

	while ( !_terminated && !_hasError )
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
			wchar_t buffer[512];
			swprintf( buffer, 512, L"-%4.1f ms  %.0f fps %s", 1000.f * frameTime, 1.0f / frameTime, strCustom );
			SetCustomWindowText( buffer );
		}

		if ( _resize.load( memory_order_acquire ) )
		{
			_resize.store( false, memory_order_relaxed );
			FrameworkResize();
		}
		FrameworkUpdate();
		FrameworkRender();
	}
	FrameworkDestory();
}

int DX12Framework::Run( HINSTANCE hInstance, int nCmdShow )
{
	SetThreadName( "UI Thread" );
	// Initialize the window class.
	WNDCLASSEX windowClass = { 0 };
	windowClass.cbSize = sizeof( WNDCLASSEX );
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = hInstance;
	windowClass.hCursor = LoadCursor( NULL, IDC_ARROW );
	windowClass.lpszClassName = L"WindowClass1";
	RegisterClassEx( &windowClass );

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

	RECT windowRect = { left, top, left + static_cast< LONG >( framework_config.swapChainDesc.Width ), 
		top + static_cast< LONG >( framework_config.swapChainDesc.Height ) };
	AdjustWindowRect( &windowRect, WS_OVERLAPPEDWINDOW, FALSE );

	// Create the window and store a handle to it.
	framework_hwnd = CreateWindowEx( NULL,
							 L"WindowClass1",
							 framework_title.c_str(),
							 WS_OVERLAPPEDWINDOW,
							 windowRect.left,
							 windowRect.top,
							 windowRect.right - windowRect.left,
							 windowRect.bottom - windowRect.top,
							 NULL,		// We have no parent window, NULL.
							 NULL,		// We aren't using menus, NULL.
							 hInstance,
							 this );		// We aren't using multiple windows, NULL.


	// Create an event handle to use for frame synchronization.
	_dxReady = CreateEvent( nullptr, FALSE, FALSE, nullptr );
	if ( _dxReady == nullptr )
	{
		HRESULT hr;
		VRET( HRESULT_FROM_WIN32( GetLastError() ) );
	}


	std::thread renderThread( &DX12Framework::RenderLoop, this );
	thread_guard g( renderThread );

	// Wait for DX init to finish, so window will show up with meaningful content
	WaitForSingleObject( _dxReady, INFINITE );
	ShowWindow( framework_hwnd, nCmdShow );

	EnableMouseInPointer( TRUE );

	// Main sample loop.
	MSG msg = { 0 };
	while ( msg.message != WM_QUIT )
	{
		// Process any messages in the queue.
		if ( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) )
		{
			TranslateMessage( &msg );
			DispatchMessage( &msg );

			// Pass events into our sample.
			FrameworkHandleEvent( &msg );
		}
	}
	_terminated = true;
	// Return this part of the WM_QUIT message to Windows.
	return static_cast< char >( msg.wParam );
}

// Helper function for resolving the full path of assets.
std::wstring DX12Framework::GetAssetFullPath( LPCWSTR assetName )
{
	return framework_assetsPath + assetName;
}

HRESULT DX12Framework::ResizeBackBuffer()
{
	HRESULT hr;
	// Resize the swap chain to the desired dimensions.
	DXGI_SWAP_CHAIN_DESC desc = {};
	framework_gfxSwapChain->GetDesc( &desc );
	VRET( framework_gfxSwapChain->ResizeBuffers( framework_config.swapChainDesc.BufferCount,
									  framework_config.swapChainDesc.Width, 
									  framework_config.swapChainDesc.Height,
									  framework_config.swapChainDesc.Format,
									  framework_config.swapChainDesc.Flags ) );
	return hr;
}

// Helper function for setting the window's title text.
void DX12Framework::SetCustomWindowText( LPCWSTR text )
{
	std::wstring windowText = framework_title + L" " + text;
	SetWindowText( framework_hwnd, windowText.c_str() );
}

// Helper function for parsing any supplied command line args.
void DX12Framework::ParseCommandLineArgs()
{
	int argc;
	LPWSTR *argv = CommandLineToArgvW( GetCommandLineW(), &argc );
	for ( int i = 1; i < argc; ++i )
	{
		if ( _wcsnicmp( argv[i], L"-warp", wcslen( argv[i] ) ) == 0 ||
			 _wcsnicmp( argv[i], L"/warp", wcslen( argv[i] ) ) == 0 )
		{
			framework_config.warpDevice = true;
		}
	}
	LocalFree( argv );
}

// Main message handler for the sample.
LRESULT CALLBACK DX12Framework::WindowProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
	DX12Framework* pSample = reinterpret_cast< DX12Framework* >( GetWindowLongPtr( hWnd, GWLP_USERDATA ) );

	// Handle destroy/shutdown messages.
	switch ( message )
	{
	case WM_CREATE:
		{
			// Save a pointer to the DXSample passed in to CreateWindow.
			LPCREATESTRUCT pCreateStruct = reinterpret_cast< LPCREATESTRUCT >( lParam );
			SetWindowLongPtr( hWnd, GWLP_USERDATA, reinterpret_cast< LONG_PTR >( pCreateStruct->lpCreateParams ) );
		}
		return 0;

	case WM_SIZE:
		if ( wParam != SIZE_MINIMIZED && pSample )
		{
			RECT clientRect = {};
			GetClientRect( hWnd, &clientRect );
			_width = clientRect.right - clientRect.left;
			_height = clientRect.bottom - clientRect.top;
			if ( pSample->framework_config.swapChainDesc.Width != _width || pSample->framework_config.swapChainDesc.Height != _height )
				_resize.store( true, memory_order_release );
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
			pSample->framework_config.vsync = !pSample->framework_config.vsync;
			PRINTINFO( "Vsync is %s", pSample->framework_config.vsync ? "on" : "off" );
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
