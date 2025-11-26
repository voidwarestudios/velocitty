#pragma once

#include "../../framework.h"
#include <vector>
#include <string>
#include <unordered_map>

struct LigatureResult {
    std::vector<uint16_t> glyphIndices;
    std::vector<float> glyphAdvances;
    std::vector<DWRITE_GLYPH_OFFSET> glyphOffsets;
    uint32_t clusterCount;
    bool hasLigatures;
};

class LigatureHandler {
public:
    LigatureHandler() = default;
    ~LigatureHandler() = default;

    bool init(IDWriteFactory* dwFactory, const wchar_t* fontFamily, float fontSize);

    LigatureResult shapeText(const std::wstring& text, bool bold, bool italic);

    bool isLigatureFont() const { return isLigatureFont_; }
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

private:
    ComPtr<IDWriteFactory> dwFactory_;
    ComPtr<IDWriteFontFace> fontFace_;
    ComPtr<IDWriteFontFace> fontFaceBold_;
    ComPtr<IDWriteFontFace> fontFaceItalic_;
    ComPtr<IDWriteFontFace> fontFaceBoldItalic_;
    ComPtr<IDWriteTextAnalyzer> analyzer_;

    float fontSize_ = 14.0f;
    bool isLigatureFont_ = false;
    bool enabled_ = true;

    static const std::vector<std::wstring> commonLigatures_;
};
