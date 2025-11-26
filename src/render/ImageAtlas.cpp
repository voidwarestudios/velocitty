#include "ImageAtlas.h"

bool ImageAtlas::init(ID3D11Device* device, uint32_t atlasWidth, uint32_t atlasHeight) {
    device_ = device;
    atlasWidth_ = atlasWidth;
    atlasHeight_ = atlasHeight;
    return true;
}

bool ImageAtlas::ensureTexture() {
    if (atlasTexture_) return true;

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = atlasWidth_;
    texDesc.Height = atlasHeight_;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device_->CreateTexture2D(&texDesc, nullptr, &atlasTexture_);
    if (FAILED(hr)) return false;

    hr = device_->CreateShaderResourceView(atlasTexture_.Get(), nullptr, &atlasSRV_);
    if (FAILED(hr)) return false;

    return true;
}

uint32_t ImageAtlas::addImage(const uint8_t* rgba, uint32_t width, uint32_t height,
                               uint32_t cellX, uint32_t cellY, uint32_t cellW, uint32_t cellH) {
    if (!ensureTexture()) return 0;

    uint32_t x, y;
    if (!findSpace(width, height, x, y)) {
        return 0;
    }

    ComPtr<ID3D11DeviceContext> context;
    device_->GetImmediateContext(&context);

    D3D11_BOX box = {};
    box.left = x;
    box.top = y;
    box.right = x + width;
    box.bottom = y + height;
    box.front = 0;
    box.back = 1;

    context->UpdateSubresource(atlasTexture_.Get(), 0, &box, rgba, width * 4, 0);

    ImageInfo info;
    info.id = nextId_++;
    info.u0 = static_cast<float>(x) / atlasWidth_;
    info.v0 = static_cast<float>(y) / atlasHeight_;
    info.u1 = static_cast<float>(x + width) / atlasWidth_;
    info.v1 = static_cast<float>(y + height) / atlasHeight_;
    info.width = width;
    info.height = height;
    info.cellX = cellX;
    info.cellY = cellY;
    info.cellWidth = cellW;
    info.cellHeight = cellH;
    info.valid = true;

    images_[info.id] = info;
    return info.id;
}

const ImageInfo* ImageAtlas::getImage(uint32_t id) const {
    auto it = images_.find(id);
    if (it != images_.end()) {
        return &it->second;
    }
    return nullptr;
}

void ImageAtlas::removeImage(uint32_t id) {
    images_.erase(id);
}

void ImageAtlas::clear() {
    images_.clear();
    cursorX_ = 0;
    cursorY_ = 0;
    rowHeight_ = 0;
}

bool ImageAtlas::findSpace(uint32_t width, uint32_t height, uint32_t& outX, uint32_t& outY) {
    if (cursorX_ + width > atlasWidth_) {
        cursorX_ = 0;
        cursorY_ += rowHeight_;
        rowHeight_ = 0;
    }

    if (cursorY_ + height > atlasHeight_) {
        return false;
    }

    outX = cursorX_;
    outY = cursorY_;

    cursorX_ += width;
    rowHeight_ = std::max(rowHeight_, height);

    return true;
}
