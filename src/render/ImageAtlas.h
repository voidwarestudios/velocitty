#pragma once

#include "../../framework.h"
#include <unordered_map>
#include <vector>

struct ImageInfo {
    uint32_t id;
    float u0, v0, u1, v1;
    uint32_t width, height;
    uint32_t cellX, cellY;
    uint32_t cellWidth, cellHeight;
    bool valid;
};

class ImageAtlas {
public:
    ImageAtlas() = default;
    ~ImageAtlas() = default;

    bool init(ID3D11Device* device, uint32_t atlasWidth = 2048, uint32_t atlasHeight = 2048);

    uint32_t addImage(const uint8_t* rgba, uint32_t width, uint32_t height,
                      uint32_t cellX, uint32_t cellY, uint32_t cellW, uint32_t cellH);

    const ImageInfo* getImage(uint32_t id) const;
    void removeImage(uint32_t id);
    void clear();

    ID3D11ShaderResourceView* getTextureSRV() const { return atlasSRV_.Get(); }

    const std::unordered_map<uint32_t, ImageInfo>& getImages() const { return images_; }

private:
    struct AtlasRegion {
        uint32_t x, y, width, height;
        bool used;
    };

    bool ensureTexture();
    bool findSpace(uint32_t width, uint32_t height, uint32_t& outX, uint32_t& outY);

    ID3D11Device* device_ = nullptr;
    ComPtr<ID3D11Texture2D> atlasTexture_;
    ComPtr<ID3D11ShaderResourceView> atlasSRV_;

    std::unordered_map<uint32_t, ImageInfo> images_;
    std::vector<AtlasRegion> regions_;

    uint32_t atlasWidth_ = 2048;
    uint32_t atlasHeight_ = 2048;
    uint32_t cursorX_ = 0;
    uint32_t cursorY_ = 0;
    uint32_t rowHeight_ = 0;
    uint32_t nextId_ = 1;
};
