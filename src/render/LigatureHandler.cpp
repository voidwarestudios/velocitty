#include "LigatureHandler.h"
#include <dwrite_1.h>

const std::vector<std::wstring> LigatureHandler::commonLigatures_ = {
    L"==", L"!=", L"===", L"!==", L"=>", L"->", L"<-", L"<=", L">=",
    L"++", L"--", L"&&", L"||", L"??", L"::", L"<>", L"|>", L"<|",
    L"//", L"/*", L"*/", L"<!--", L"-->", L"<=>", L"<->", L"==>",
    L"..", L"...", L":::", L"www", L"ff", L"fi", L"fl", L"ffi", L"ffl"
};

bool LigatureHandler::init(IDWriteFactory* dwFactory, const wchar_t* fontFamily, float fontSize) {
    dwFactory_ = dwFactory;
    fontSize_ = fontSize;

    HRESULT hr = dwFactory_->CreateTextAnalyzer(&analyzer_);
    if (FAILED(hr)) return false;

    ComPtr<IDWriteFontCollection> fontCollection;
    hr = dwFactory_->GetSystemFontCollection(&fontCollection);
    if (FAILED(hr)) return false;

    UINT32 fontIndex;
    BOOL fontExists;
    hr = fontCollection->FindFamilyName(fontFamily, &fontIndex, &fontExists);
    if (FAILED(hr) || !fontExists) return false;

    ComPtr<IDWriteFontFamily> fontFamily2;
    hr = fontCollection->GetFontFamily(fontIndex, &fontFamily2);
    if (FAILED(hr)) return false;

    ComPtr<IDWriteFont> font;
    hr = fontFamily2->GetFirstMatchingFont(
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        &font
    );
    if (FAILED(hr)) return false;

    hr = font->CreateFontFace(&fontFace_);
    if (FAILED(hr)) return false;

    ComPtr<IDWriteFont> fontBold;
    hr = fontFamily2->GetFirstMatchingFont(
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STRETCH_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        &fontBold
    );
    if (SUCCEEDED(hr)) {
        fontBold->CreateFontFace(&fontFaceBold_);
    }

    ComPtr<IDWriteFont> fontItalic;
    hr = fontFamily2->GetFirstMatchingFont(
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        DWRITE_FONT_STYLE_ITALIC,
        &fontItalic
    );
    if (SUCCEEDED(hr)) {
        fontItalic->CreateFontFace(&fontFaceItalic_);
    }

    ComPtr<IDWriteFont> fontBoldItalic;
    hr = fontFamily2->GetFirstMatchingFont(
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STRETCH_NORMAL,
        DWRITE_FONT_STYLE_ITALIC,
        &fontBoldItalic
    );
    if (SUCCEEDED(hr)) {
        fontBoldItalic->CreateFontFace(&fontFaceBoldItalic_);
    }

    isLigatureFont_ = true;

    return true;
}

LigatureResult LigatureHandler::shapeText(const std::wstring& text, bool bold, bool italic) {
    LigatureResult result;
    result.hasLigatures = false;
    result.clusterCount = 0;

    if (text.empty() || !enabled_) {
        return result;
    }

    IDWriteFontFace* fontFace = fontFace_.Get();
    if (bold && italic && fontFaceBoldItalic_) {
        fontFace = fontFaceBoldItalic_.Get();
    } else if (bold && fontFaceBold_) {
        fontFace = fontFaceBold_.Get();
    } else if (italic && fontFaceItalic_) {
        fontFace = fontFaceItalic_.Get();
    }

    if (!fontFace) return result;

    UINT32 textLength = static_cast<UINT32>(text.length());

    std::vector<UINT16> clusterMap(textLength);
    std::vector<DWRITE_SHAPING_TEXT_PROPERTIES> textProps(textLength);

    UINT32 maxGlyphCount = textLength * 3;
    std::vector<UINT16> glyphIndices(maxGlyphCount);
    std::vector<DWRITE_SHAPING_GLYPH_PROPERTIES> glyphProps(maxGlyphCount);

    UINT32 actualGlyphCount;

    DWRITE_SCRIPT_ANALYSIS scriptAnalysis = {};
    scriptAnalysis.script = DWRITE_SCRIPT_SHAPES_DEFAULT;

    HRESULT hr = analyzer_->GetGlyphs(
        text.c_str(),
        textLength,
        fontFace,
        FALSE,
        FALSE,
        &scriptAnalysis,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        0,
        maxGlyphCount,
        clusterMap.data(),
        textProps.data(),
        glyphIndices.data(),
        glyphProps.data(),
        &actualGlyphCount
    );

    if (FAILED(hr)) {
        return result;
    }

    glyphIndices.resize(actualGlyphCount);
    glyphProps.resize(actualGlyphCount);

    std::vector<float> glyphAdvances(actualGlyphCount);
    std::vector<DWRITE_GLYPH_OFFSET> glyphOffsets(actualGlyphCount);

    hr = analyzer_->GetGlyphPlacements(
        text.c_str(),
        clusterMap.data(),
        textProps.data(),
        textLength,
        glyphIndices.data(),
        glyphProps.data(),
        actualGlyphCount,
        fontFace,
        fontSize_,
        FALSE,
        FALSE,
        &scriptAnalysis,
        nullptr,
        nullptr,
        nullptr,
        0,
        glyphAdvances.data(),
        glyphOffsets.data()
    );

    if (FAILED(hr)) {
        return result;
    }

    result.glyphIndices = std::move(glyphIndices);
    result.glyphAdvances = std::move(glyphAdvances);
    result.glyphOffsets = std::move(glyphOffsets);
    result.clusterCount = actualGlyphCount;
    result.hasLigatures = (actualGlyphCount < textLength);

    return result;
}
