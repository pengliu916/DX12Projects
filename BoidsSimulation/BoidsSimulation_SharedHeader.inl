
#define BLOCK_SIZE 256

// Do not modify below this line
#if __cplusplus
#define CBUFFER_ALIGN __declspec(align(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
#else
#define CBUFFER_ALIGN
#endif

#if __hlsl
#define REGISTER(x) :register(x)
#define STRUCT(x) x
#else 
typedef XMMATRIX	matrix;
typedef XMINT4		int4;
typedef XMINT3		int3;
typedef XMFLOAT4	float4;
typedef XMFLOAT3	float3;
typedef XMFLOAT2	float2;
typedef UINT		uint;
#define REGISTER(x)
#define STRUCT(x) struct
#endif

CBUFFER_ALIGN STRUCT( cbuffer ) RenderCB REGISTER( b0 )
{
	matrix	mWorldViewProj;
#if __cplusplus
	void* operator new(size_t i){return _aligned_malloc( i,16 );};
	void operator delete(void* p) { _aligned_free( p ); };
#endif
};

CBUFFER_ALIGN STRUCT( cbuffer ) SimulationCB REGISTER( b1 )
{
	float	fAvoidanceFactor;
	float	fSeperationFactor;
	float	fCohesionFactor;
	float	fAlignmentFactor;

	float	fSeekingFactor;
	float3	f3SeekSourcePos;

	float	fFleeFactor;
	float3	f3FleeSourcePos;

	float	fMaxForce;
	float3	f3CenterPos;

	float	fMaxSpeed;
	float3	f3xyzExpand;

	float	fMinSpeed;
	float	fVisionDist;
	float	fVisionAngleCos;
	float	fDeltaT;

	uint	uNumInstance;
	float	fFishSize;

#if __cplusplus
	void* operator new(size_t i){return _aligned_malloc( i,16 );};
	void operator delete(void* p) { _aligned_free( p ); };
#endif
};

struct FishData
{
	float3 pos;
	float3 vel;
};