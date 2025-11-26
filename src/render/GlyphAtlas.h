#pragma once

#include "../../framework.h"
#include <unordered_map>
#include <string>

struct GlyphKey {
    char32_t codepoint;
    bool bold;
    bool italic;

    bool operator==(const GlyphKey& other) const {
        return codepoint == other.codepoint && bold == other.bold && italic == other.italic;
    }
};

template<>
struct std::hash<GlyphKey> {
    size_t operator()(const GlyphKey& k) const {
        return std::hash<char32_t>()(k.codepoint) ^
               (std::hash<bool>()(k.bold) << 1) ^
               (std::hash<bool>()(k.italic) << 2);
    }
};

struct GlyphInfo {
    float u0, v0, u1, v1;
    float width, height;
    float offsetX, offsetY;
    bool valid;
};

class GlyphAtlas {
public:
    GlyphAtlas() = default;
    ~GlyphAtlas() = default;

    bool init(ID3D11Device* device, IDWriteFactory* dwFactory,
              const wchar_t* fontFamily, float fontSize);

    const GlyphInfo& getGlyph(char32_t codepoint, bool bold = false, bool italic = false);

    ID3D11ShaderResourceView* getTextureSRV() const { return atlasSRV_.Get(); }
    float getCellWidth() const { return cellWidth_; }
    float getCellHeight() const { return cellHeight_; }

    void setFontFamily(const std::wstring& fontFamily);
    void setFontSize(float fontSize);

private:
    bool rasterizeGlyph(const GlyphKey& key);
    bool rasterizeBoxDrawing(const GlyphKey& key);
    bool growAtlas();
    void createFontFace(bool bold, bool italic);
    bool isSpecialGlyph(char32_t codepoint) const;

    ID3D11Device* device_ = nullptr;
    ComPtr<ID3D11Texture2D> atlasTexture_;
    ComPtr<ID3D11ShaderResourceView> atlasSRV_;

    ComPtr<IDWriteFactory> dwFactory_;
    ComPtr<IDWriteFontFace> fontFaceRegular_;
    ComPtr<IDWriteFontFace> fontFaceBold_;
    ComPtr<IDWriteFontFace> fontFaceItalic_;
    ComPtr<IDWriteFontFace> fontFaceBoldItalic_;
    ComPtr<IDWriteTextFormat> textFormat_;

    // cached factories to avoid recreation per glyph
    ComPtr<ID2D1Factory> d2dFactory_;
    ComPtr<IWICImagingFactory> wicFactory_;

    std::unordered_map<GlyphKey, GlyphInfo> glyphCache_;

    uint32_t atlasWidth_ = 512;
    uint32_t atlasHeight_ = 512;
    uint32_t cursorX_ = 0;
    uint32_t cursorY_ = 0;
    uint32_t rowHeight_ = 0;

    float cellWidth_ = 0;
    float cellHeight_ = 0;
    float fontSize_ = 14.0f;
    std::wstring fontFamily_ = L"Cascadia Mono";

    GlyphInfo invalidGlyph_{};
};
