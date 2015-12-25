// Do not modify below this line
#if __cplusplus
#define CBUFFER_ALIGN __declspec(align(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
#else
#define CBUFFER_ALIGN
#endif

#if __hlsl
typedef float2  XMFLOAT2;
typedef float4  XMFLOAT4;
typedef uint    UINT;

#define REGISTER(x) :register(x)
#define STRUCT(x) x
#else 
#define REGISTER(x)
#define STRUCT(x) struct
#endif

#if __cplusplus || ( __hlsl && Vertex_Shader )
CBUFFER_ALIGN STRUCT( cbuffer ) VertexShaderParams REGISTER( b0 )
{
    XMFLOAT2    Scale;			// Scale and offset for transforming coordinates
    XMFLOAT2    Offset;
    XMFLOAT2    InvTexDim;		// Normalizes texture coordinates
    float       TextSize;		// Height of text in destination pixels
    float       TextScale;		// TextSize / FontHeight
    float       DstBorder;		// Extra space around a glyph measured in screen space coordinates
    UINT        SrcBorder;		// Extra spacing around glyphs to avoid sampling neighboring glyphs
#if __cplusplus
    void * operator new( size_t i )
    {
        return _aligned_malloc( i, 16 );
    };
    void operator delete( void* p )
    {
        _aligned_free( p );
    };
#endif // __cplusplus
};
#endif // __cplusplus || (__hlsl && Vertex_Shader)

#if __cplusplus || ( __hlsl && Pixel_Shader )
CBUFFER_ALIGN STRUCT( cbuffer ) PixelShaderParams REGISTER( b0 )
{
    XMFLOAT4    Color;
    float       HeightRange;	// The range of the signed distance field.
#if __cplusplus
    void * operator new( size_t i )
    {
        return _aligned_malloc( i, 16 );
    };
    void operator delete( void* p )
    {
        _aligned_free( p );
    };
#endif // __cplusplus
};
#endif // __cplusplus || ( __hlsl && Pixel_Shader )