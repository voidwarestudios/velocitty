#include "GlyphAtlas.h"
#include "BoxDrawing.h"
#include <d2d1.h>
#include <dwrite_1.h>

bool GlyphAtlas::init(ID3D11Device* device, IDWriteFactory* dwFactory,
                      const wchar_t* fontFamily, float fontSize) {
    device_ = device;
    dwFactory_ = dwFactory;
    fontSize_ = fontSize;
    fontFamily_ = fontFamily;

    HRESULT hr = dwFactory_->CreateTextFormat(
        fontFamily,
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        fontSize_,
        L"en-US",
        &textFormat_
    );

    if (FAILED(hr)) {
        hr = dwFactory_->CreateTextFormat(
            L"Consolas",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            fontSize_,
            L"en-US",
            &textFormat_
        );
        if (FAILED(hr)) return false;
    }

    ComPtr<IDWriteTextLayout> layout;
    hr = dwFactory_->CreateTextLayout(L"M", 1, textFormat_.Get(), 1000.0f, 1000.0f, &layout);
    if (FAILED(hr)) return false;

    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    float dpiScale = static_cast<float>(GetDpiForSystem()) / 96.0f;
    cellWidth_ = metrics.width * dpiScale;
    cellHeight_ = metrics.height * dpiScale;

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = atlasWidth_;
    texDesc.Height = atlasHeight_;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    hr = device_->CreateTexture2D(&texDesc, nullptr, &atlasTexture_);
    if (FAILED(hr)) return false;

    hr = device_->CreateShaderResourceView(atlasTexture_.Get(), nullptr, &atlasSRV_);
    if (FAILED(hr)) return false;

    // cache D2D and WIC factories for glyph rasterization
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&wicFactory_));
    if (FAILED(hr)) return false;

    for (char32_t c = 32; c < 127; ++c) {
        rasterizeGlyph({c, false, false});
    }

    return true;
}

const GlyphInfo& GlyphAtlas::getGlyph(char32_t codepoint, bool bold, bool italic) {
    GlyphKey key{codepoint, bold, italic};

    auto it = glyphCache_.find(key);
    if (it != glyphCache_.end()) {
        return it->second;
    }

    if (isSpecialGlyph(codepoint)) {
        if (rasterizeBoxDrawing(key)) {
            return glyphCache_[key];
        }
    }

    if (rasterizeGlyph(key)) {
        return glyphCache_[key];
    }

    return invalidGlyph_;
}

bool GlyphAtlas::isSpecialGlyph(char32_t codepoint) const {
    return BoxDrawing::isBoxDrawing(codepoint) ||
           BoxDrawing::isBlockElement(codepoint) ||
           BoxDrawing::isPowerline(codepoint) ||
           (codepoint >= 0x2800 && codepoint <= 0x28FF);
}

bool GlyphAtlas::rasterizeBoxDrawing(const GlyphKey& key) {
    uint32_t glyphWidth = static_cast<uint32_t>(std::ceil(cellWidth_));
    uint32_t glyphHeight = static_cast<uint32_t>(std::ceil(cellHeight_));
    // cellWidth_/cellHeight_ are already DPI-scaled from init()

    if (cursorX_ + glyphWidth > atlasWidth_) {
        cursorX_ = 0;
        cursorY_ += rowHeight_;
        rowHeight_ = 0;
    }

    if (cursorY_ + glyphHeight > atlasHeight_) {
        if (!growAtlas()) {
            GlyphInfo info{};
            info.valid = false;
            glyphCache_[key] = info;
            return false;
        }
    }

    std::vector<uint8_t> alphaData = BoxDrawing::renderGlyph(key.codepoint, glyphWidth, glyphHeight);

    ComPtr<ID3D11DeviceContext> context;
    device_->GetImmediateContext(&context);

    D3D11_BOX box = {};
    box.left = cursorX_;
    box.top = cursorY_;
    box.right = cursorX_ + glyphWidth;
    box.bottom = cursorY_ + glyphHeight;
    box.front = 0;
    box.back = 1;

    context->UpdateSubresource(atlasTexture_.Get(), 0, &box, alphaData.data(), glyphWidth, 0);

    GlyphInfo info;
    info.u0 = static_cast<float>(cursorX_) / atlasWidth_;
    info.v0 = static_cast<float>(cursorY_) / atlasHeight_;
    info.u1 = static_cast<float>(cursorX_ + glyphWidth) / atlasWidth_;
    info.v1 = static_cast<float>(cursorY_ + glyphHeight) / atlasHeight_;
    info.width = static_cast<float>(glyphWidth);
    info.height = static_cast<float>(glyphHeight);
    info.offsetX = 0.0f;
    info.offsetY = 0.0f;
    info.valid = true;

    glyphCache_[key] = info;

    cursorX_ += glyphWidth;
    rowHeight_ = std::max(rowHeight_, glyphHeight);

    return true;
}

void GlyphAtlas::setFontFamily(const std::wstring& fontFamily) {
    fontFamily_ = fontFamily;
}

void GlyphAtlas::setFontSize(float fontSize) {
    fontSize_ = fontSize;
}

bool GlyphAtlas::rasterizeGlyph(const GlyphKey& key) {
    DWRITE_FONT_WEIGHT weight = key.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STYLE style = key.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;

    float systemDpi = static_cast<float>(GetDpiForSystem());

    ComPtr<IDWriteTextFormat> format;
    HRESULT hr = dwFactory_->CreateTextFormat(
        fontFamily_.c_str(),
        nullptr,
        weight,
        style,
        DWRITE_FONT_STRETCH_NORMAL,
        fontSize_,
        L"en-US",
        &format
    );

    if (FAILED(hr)) {
        // fallback to Consolas if the configured font isn't available
        hr = dwFactory_->CreateTextFormat(
            L"Consolas",
            nullptr,
            weight,
            style,
            DWRITE_FONT_STRETCH_NORMAL,
            fontSize_,
            L"en-US",
            &format
        );
        if (FAILED(hr)) return false;
    }

    wchar_t str[3] = {0, 0, 0};
    UINT32 strLen = 1;
    if (key.codepoint <= 0xFFFF) {
        str[0] = static_cast<wchar_t>(key.codepoint);
    } else if (key.codepoint <= 0x10FFFF) {
        // encode as utf-16 surrogate pair
        char32_t cp = key.codepoint - 0x10000;
        str[0] = static_cast<wchar_t>(0xD800 | (cp >> 10));
        str[1] = static_cast<wchar_t>(0xDC00 | (cp & 0x3FF));
        strLen = 2;
    } else {
        str[0] = L'?';
    }

    ComPtr<IDWriteTextLayout> layout;
    hr = dwFactory_->CreateTextLayout(str, strLen, format.Get(), cellWidth_ * 2, cellHeight_ * 2, &layout);
    if (FAILED(hr)) return false;

    DWRITE_TEXT_METRICS textMetrics;
    layout->GetMetrics(&textMetrics);

    // cellWidth_/cellHeight_ are already DPI-scaled from init()
    // use generous padding to prevent clipping on italic overhangs and descenders
    constexpr uint32_t glyphPadding = 4;
    uint32_t glyphWidth = static_cast<uint32_t>(std::ceil(cellWidth_)) + glyphPadding * 2;
    uint32_t glyphHeight = static_cast<uint32_t>(std::ceil(cellHeight_)) + glyphPadding * 2;

    if (cursorX_ + glyphWidth > atlasWidth_) {
        cursorX_ = 0;
        cursorY_ += rowHeight_;
        rowHeight_ = 0;
    }

    if (cursorY_ + glyphHeight > atlasHeight_) {
        if (!growAtlas()) {
            GlyphInfo info{};
            info.valid = false;
            glyphCache_[key] = info;
            return false;
        }
    }

    ComPtr<IWICBitmap> wicBitmap;
    hr = wicFactory_->CreateBitmap(glyphWidth, glyphHeight, GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapCacheOnLoad, &wicBitmap);
    if (FAILED(hr)) return false;

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        systemDpi,
        systemDpi
    );

    ComPtr<ID2D1RenderTarget> rt;
    hr = d2dFactory_->CreateWicBitmapRenderTarget(wicBitmap.Get(), rtProps, &rt);
    if (FAILED(hr)) return false;

    ComPtr<ID2D1SolidColorBrush> brush;
    rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &brush);

    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    rt->DrawTextLayout(D2D1::Point2F(glyphPadding, glyphPadding), layout.Get(), brush.Get());
    hr = rt->EndDraw();
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapLock> lock;
    WICRect lockRect = {0, 0, static_cast<INT>(glyphWidth), static_cast<INT>(glyphHeight)};
    hr = wicBitmap->Lock(&lockRect, WICBitmapLockRead, &lock);
    if (FAILED(hr)) return false;

    UINT bufferSize;
    BYTE* srcData;
    lock->GetDataPointer(&bufferSize, &srcData);

    UINT stride;
    lock->GetStride(&stride);

    std::vector<uint8_t> alphaData(glyphWidth * glyphHeight);
    for (uint32_t y = 0; y < glyphHeight; ++y) {
        for (uint32_t x = 0; x < glyphWidth; ++x) {
            uint32_t srcIdx = y * stride + x * 4;
            alphaData[y * glyphWidth + x] = srcData[srcIdx + 3];
        }
    }

    lock.Reset();

    ComPtr<ID3D11DeviceContext> context;
    device_->GetImmediateContext(&context);

    D3D11_BOX box = {};
    box.left = cursorX_;
    box.top = cursorY_;
    box.right = cursorX_ + glyphWidth;
    box.bottom = cursorY_ + glyphHeight;
    box.front = 0;
    box.back = 1;

    context->UpdateSubresource(atlasTexture_.Get(), 0, &box,
                               alphaData.data(), glyphWidth, 0);

    GlyphInfo info;
    info.u0 = static_cast<float>(cursorX_) / atlasWidth_;
    info.v0 = static_cast<float>(cursorY_) / atlasHeight_;
    info.u1 = static_cast<float>(cursorX_ + glyphWidth) / atlasWidth_;
    info.v1 = static_cast<float>(cursorY_ + glyphHeight) / atlasHeight_;
    info.width = static_cast<float>(glyphWidth);
    info.height = static_cast<float>(glyphHeight);
    info.offsetX = -static_cast<float>(glyphPadding);
    info.offsetY = -static_cast<float>(glyphPadding);
    info.valid = true;

    glyphCache_[key] = info;

    cursorX_ += glyphWidth;
    rowHeight_ = std::max(rowHeight_, glyphHeight);

    return true;
}

bool GlyphAtlas::growAtlas() {
    // double the atlas size, up to 4096x4096 max
    uint32_t newWidth = std::min(atlasWidth_ * 2, 4096u);
    uint32_t newHeight = std::min(atlasHeight_ * 2, 4096u);

    if (newWidth == atlasWidth_ && newHeight == atlasHeight_) {
        // already at max size
        return false;
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = newWidth;
    texDesc.Height = newHeight;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    ComPtr<ID3D11Texture2D> newTexture;
    HRESULT hr = device_->CreateTexture2D(&texDesc, nullptr, &newTexture);
    if (FAILED(hr)) return false;

    // copy old atlas content to new texture
    ComPtr<ID3D11DeviceContext> context;
    device_->GetImmediateContext(&context);

    D3D11_BOX srcBox = {};
    srcBox.left = 0;
    srcBox.top = 0;
    srcBox.right = atlasWidth_;
    srcBox.bottom = atlasHeight_;
    srcBox.front = 0;
    srcBox.back = 1;

    context->CopySubresourceRegion(newTexture.Get(), 0, 0, 0, 0,
                                    atlasTexture_.Get(), 0, &srcBox);

    ComPtr<ID3D11ShaderResourceView> newSRV;
    hr = device_->CreateShaderResourceView(newTexture.Get(), nullptr, &newSRV);
    if (FAILED(hr)) return false;

    // update atlas dimensions and textures
    atlasTexture_ = newTexture;
    atlasSRV_ = newSRV;
    atlasWidth_ = newWidth;
    atlasHeight_ = newHeight;

    // recalculate UVs for all cached glyphs since atlas size changed
    float invWidth = 1.0f / atlasWidth_;
    float invHeight = 1.0f / atlasHeight_;

    for (auto& [key, info] : glyphCache_) {
        if (info.valid) {
            // convert back to pixel coords, then to new UVs
            float pixelX0 = info.u0 * (atlasWidth_ / 2);  // old atlas was half the new size
            float pixelY0 = info.v0 * (atlasHeight_ / 2);
            float pixelX1 = info.u1 * (atlasWidth_ / 2);
            float pixelY1 = info.v1 * (atlasHeight_ / 2);

            info.u0 = pixelX0 * invWidth;
            info.v0 = pixelY0 * invHeight;
            info.u1 = pixelX1 * invWidth;
            info.v1 = pixelY1 * invHeight;
        }
    }

    return true;
}
