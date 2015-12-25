// Ported from MSFT miniEngine
#pragma once
#include "LibraryHeader.h"
#include "DescriptorHeap.h"
#include <unordered_map>

using namespace Microsoft::WRL;
using namespace DirectX;
using namespace std;


namespace TextRenderer
{
#include "TextRenderer_SharedHeader.inl"

    struct Texture
    {
        DescriptorHandle m_srvHandle;
        D3D12_GPU_VIRTUAL_ADDRESS m_GpuVirtualAddress;
        ComPtr<ID3D12Resource> m_pResource;

        HRESULT Create( size_t Width, size_t Height, DXGI_FORMAT Format, const void* InitialData );
        HRESULT Initilize( Texture* tex, D3D12_SUBRESOURCE_DATA SubData[] );
    };

    HRESULT CreateResource();
    void ShutDown();

    class Font
    {
    public:
        struct FontHeader
        {
            char FileDescriptor[8];		// "SDFFONT\0"
            uint8_t  majorVersion;		// '1'
            uint8_t  minorVersion;		// '0'
            uint16_t borderSize;		// Pixel empty space border width
            uint16_t textureWidth;		// Width of texture buffer
            uint16_t textureHeight;		// Height of texture buffer
            uint16_t fontHeight;		// Font height in 12.4
            uint16_t advanceY;			// Line height in 12.4
            uint16_t numGlyphs;			// Glyph count in texture
            uint16_t searchDist;		// Range of search space 12.4
        };

        // Each character has an XY start offset, a width, and they all share the same height
        struct Glyph
        {
            uint16_t x, y, w;
            int16_t bearing;
            uint16_t advance;
        };

        Font();
        ~Font();

        void LoadFromBinary( const wchar_t*, const uint8_t*, const size_t );
        bool Load( const wstring& );
        const Glyph* GetGlyph( wchar_t ch ) const;
        uint16_t GetHeight() const;
        uint16_t GetBorderSize() const;
        float GetVerticalSpacing( float ) const;
        Texture& GetTexture();
        float GetXNormalizationFactor() const;
        float GetYNormalizationFactor() const;
        float GetAntialiasRange( float ) const;
       
    private:
        float                           m_NormalizeXCoord;
        float                           m_NormalizeYCoord;
        float                           m_FontLineSpacing;
        float                           m_AntialiasRange;
        uint16_t                        m_FontHeight;
        uint16_t                        m_BorderSize;
        uint16_t                        m_TextureWidth;
        uint16_t                        m_TextureHeight;
        Texture                         m_Texture;
        unordered_map<wchar_t, Glyph>   m_Dictionary;
    };
};

class TextContext
{
public:
    TextContext( float canvasWidth, float canvasHeight );

    HRESULT CreateResource();

    void Release();

    // Put settings back to the defaults.
    void ResetSettings( void );

    //
    // Control various text properties
    //

    // Choose a font from the Fonts folder.  Previously loaded fonts are cached in memory.
    void SetFont( const std::wstring& fontName, float TextSize = 0.0f );

    // Resize the view space.  This determines the coordinate space of the cursor position and font size.  You can always
    // set the view size to the same dimensions regardless of actual display resolution.  It is assumed, however, that the
    // aspect ratio of this virtual coordinate system matches the actual display aspect ratio.
    void SetViewSize( float ViewWidth, float ViewHeight );

    // Set the size of the text relative to the ViewHeight.  The aspect of the text is preserved from
    // the TTF as long as the aspect ratio of the view space is the same as the actual viewport.
    void SetTextSize( float PixelHeight );

    // Move the cursor position--the upper-left anchor for the text.
    void ResetCursor( float x, float y );
    void SetLeftMargin( float x );
    void SetCursorX( float x );
    void SetCursorY( float y );
    void NewLine( void );
    float GetLeftMargin( void );
    float GetCursorX( void );
    float GetCursorY( void );

    // Set the color and transparency of text.
    void SetColor( XMFLOAT4 color );

    // Get the amount to advance the Y position to begin a new line
    float GetVerticalSpacing( void );

    //
    // Rendering commands
    //

    // Begin and end drawing commands
    void Begin( ID3D12GraphicsCommandList* cmdList );
    void End( void );

    // Draw a string
    void DrawString( const std::wstring& str );
    void DrawString( const std::string& str );

    // A more powerful function which formats text like printf().  Very slow by comparison, so use it
    // only if you're going to format text anyway.
    void DrawFormattedString( const wchar_t* format, ... );
    void DrawFormattedString( const char* format, ... );

private:
    void SetRenderState( void );
    // 16 Byte structure to represent an entire glyph in the text vertex buffer
    __declspec( align( 16 ) ) struct TextVert
    {
        float X, Y;				// Upper-left glyph position in screen space
        uint16_t U, V, W, H;	// Upper-left glyph UV and the width in texture space
    };

    UINT FillVertexBuffer( TextVert volatile* verts, const char* str, size_t stride, size_t slen );

    const uint64_t                      MAX_VB_SIZE = 0x200000;
    TextRenderer::Font*                 m_CurrentFont;
    TextRenderer::VertexShaderParams    m_VSParams;
    TextRenderer::PixelShaderParams     m_PSParams;

    TextRenderer::VertexShaderParams*   m_MappedCBVS;
    TextRenderer::PixelShaderParams*    m_MappedCBPS;

    ID3D12GraphicsCommandList*          m_cmdList;

    ComPtr<ID3D12Resource>              m_cbVS;
    ComPtr<ID3D12Resource>              m_cbPS;
    ComPtr<ID3D12Resource>              m_vb;

    TextVert*                           m_vbData;

    DescriptorHandle                    m_dhCBVS;
    DescriptorHandle                    m_dhCBPS;

    HANDLE                              textRender_fenceEvent;
    uint64_t                            textRender_fenceValue;
    uint64_t                            textRender_lastCompletedFenceValue;

    bool m_VSConstantBufferIsStale;	// Tracks when the CB needs updating
    bool m_PSConstantBufferIsStale;	// Tracks when the CB needs updating
    bool m_TextureIsStale;
    float m_LeftMargin;
    float m_TextPosX;
    float m_TextPosY;
    float m_LineHeight;
    float m_ViewWidth;				// Width of the drawable area
    float m_ViewHeight;				// Height of the drawable area
    BOOL m_HDR;

};