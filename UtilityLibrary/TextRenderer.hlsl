#include "TextRenderer_SharedHeader.inl"

#if Vertex_Shader
struct VS_INPUT
{
    float2 ScreenPos : POSITION;	// Upper-left position in screen pixel coordinates
    uint4  Glyph : TEXCOORD;		// X, Y, Width, Height in texel space
};

struct VS_OUTPUT
{
    float4 Pos : SV_POSITION;	// Upper-left and lower-right coordinates in clip space
    float2 Tex : TEXCOORD0;		// Upper-left and lower-right normalized UVs
};

VS_OUTPUT main( VS_INPUT input, uint VertID : SV_VertexID )
{
    const float2 xy0 = input.ScreenPos - DstBorder;
    const float2 xy1 = input.ScreenPos + DstBorder + float2( TextScale * input.Glyph.z, TextSize );
    const uint2 uv0 = input.Glyph.xy - SrcBorder;
    const uint2 uv1 = input.Glyph.xy + SrcBorder + input.Glyph.zw;

    float2 uv = float2( VertID & 1, ( VertID >> 1 ) & 1 );

    VS_OUTPUT output;
    output.Pos = float4( lerp( xy0, xy1, uv ) * Scale + Offset, 0, 1 );
    output.Tex = lerp( uv0, uv1, uv ) * InvTexDim;
    return output;
}
#endif

#if Pixel_Shader
Texture2D<float> SignedDistanceFieldTex : register( t0 );
SamplerState LinearSampler : register( s0 );

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float GetAlpha( float2 uv )
{
    return saturate( SignedDistanceFieldTex.Sample( LinearSampler, uv ) * HeightRange + 0.5 );
}

float4 main( PS_INPUT Input ) : SV_Target
{
    return float4( Color.rgb, 1 ) * GetAlpha( Input.uv ) * Color.a;
}
#endif