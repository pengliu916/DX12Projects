
// Feature flag

// Set this to 1 will lose perf due to nvidia hw specific architecture
// See the following link for more detail
// http://www.gamedev.net/topic/673103-using-small-static-arrays-in-shader-cause-heavily-perf-drop/
//
#define STATIC_ARRAY 0

// Toggle between using octahedron animation or sphere animation
#define SPHERE_VOLUME_ANIMATION 0

// Params
#define THREAD_X 8
#define THREAD_Y 8
#define THREAD_Z 8

#define VOLUME_SIZE_X 256
#define VOLUME_SIZE_Y 256
#define VOLUME_SIZE_Z 256

#define VOLUME_SIZE_SCALE 0.01f
#define COLOR_COUNT 7

#define DEFAULT_ORBIT_RADIUS 10.f
#define MAX_ORBIT_RADIUS  100.f
#define MIN_ORBIT_RADIUS  2.f

// Do not modify below this line
#if __cplusplus
#define CBUFFER_ALIGN __declspec(align(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
#else
#define CBUFFER_ALIGN
#endif

#if __hlsl
typedef matrix XMMATRIX;

#define REGISTER(x) :register(x)
#define STRUCT(x) x
#else 
#define REGISTER(x)
#define STRUCT(x) struct
#endif

#if __cplusplus || (__hlsl && STATIC_ARRAY)
static XMINT4 shiftingColVals[] =
{
	XMINT4( 1, 0, 0, 0 ),
	XMINT4( 0, 1, 0, 1 ),
	XMINT4( 0, 0, 1, 2 ),
	XMINT4( 1, 1, 0, 3 ),
	XMINT4( 1, 0, 1, 4 ),
	XMINT4( 0, 1, 1, 5 ),
	XMINT4( 1, 1, 1, 6 ),
};
#endif


CBUFFER_ALIGN STRUCT(cbuffer) ConstantBuffer REGISTER(b0)
{
	XMMATRIX wvp;
	XMMATRIX invWorld;
	XMFLOAT4 viewPos;
	XMINT4 bgCol;
#if !STATIC_ARRAY
	XMINT4 shiftingColVals[COLOR_COUNT];
#endif // !STATIC_ARRAY
#if __cplusplus
	void * operator new(size_t i)
	{
		return _aligned_malloc( i, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT );
	};
	void operator delete( void* p )
	{
		_aligned_free( p );
	};
#endif
};