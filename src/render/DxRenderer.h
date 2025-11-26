#pragma once

#include "../../framework.h"
#include "../core/Cell.h"
#include "../core/ScreenBuffer.h"
#include "../core/Selection.h"
#include "../ui/Titlebar.h"
#include "../ui/FileSearchOverlay.h"
#include "GlyphAtlas.h"
#include "ImageAtlas.h"

struct Vertex {
    float x, y;
    float u, v;
    float r, g, b, a;
    float bgR, bgG, bgB, bgA;
};

struct ImageVertex {
    float x, y;
    float u, v;
};

class DxRenderer {
public:
    DxRenderer() = default;
    ~DxRenderer();

    DxRenderer(const DxRenderer&) = delete;
    DxRenderer& operator=(const DxRenderer&) = delete;

    bool init(HWND hwnd, uint32_t width, uint32_t height);
    void resize(uint32_t width, uint32_t height);
    void shutdown();

    void beginFrame();
    void renderBuffer(const ScreenBuffer& buffer, float xOffset, float yOffset, const Selection* selection = nullptr);
    void renderTitlebar(const Titlebar& titlebar);
    void renderBorder(uint32_t color);
    void renderScrollbar(const ScreenBuffer& buffer, float xOffset, float yOffset, float opacity = 1.0f);
    void drawCursor(uint16_t col, uint16_t row, float xOffset, float yOffset, float opacity = 1.0f);
    void renderPaneDivider(float x, float y, float length, bool vertical, uint32_t color);
    void renderFileSearchOverlay(const FileSearchOverlay& overlay);
    void endFrame();
    void present(bool vsync = true);

    uint32_t addImage(const uint8_t* rgba, uint32_t width, uint32_t height,
                      uint32_t cellX, uint32_t cellY);
    void removeImage(uint32_t id);

    float getCellWidth() const { return glyphAtlas_.getCellWidth(); }
    float getCellHeight() const { return glyphAtlas_.getCellHeight(); }
    uint32_t getWidth() const { return width_; }
    uint32_t getHeight() const { return height_; }

    ID3D11Device* getDevice() const { return device_.Get(); }

private:
    bool createDeviceResources();
    bool createShaders();
    bool createVertexBuffer();
    void updateProjectionMatrix();
    void renderUnderlines();
    void renderImages();
    void addColoredQuad(float x, float y, float w, float h, uint32_t color);
    void addOverlayQuad(float x, float y, float w, float h, uint32_t color);
    void renderTitlebarText(const std::wstring& text, float x, float y, uint32_t color, uint32_t bgColor);
    void renderOverlayText(const std::wstring& text, float x, float y, uint32_t color, uint32_t bgColor = 0);
    void renderOverlayTextHighlighted(const std::wstring& text, float x, float y,
                                      uint32_t normalColor, uint32_t highlightColor,
                                      size_t highlightStart, size_t highlightLen, uint32_t bgColor = 0);
    const GlyphInfo& getSpaceGlyph();

    HWND hwnd_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain1> swapchain_;
    ComPtr<ID3D11RenderTargetView> rtv_;

    ComPtr<ID3D11VertexShader> vertexShader_;
    ComPtr<ID3D11PixelShader> pixelShader_;
    ComPtr<ID3D11PixelShader> backgroundPixelShader_;
    ComPtr<ID3D11VertexShader> imageVertexShader_;
    ComPtr<ID3D11PixelShader> imagePixelShader_;
    ComPtr<ID3D11InputLayout> inputLayout_;
    ComPtr<ID3D11InputLayout> imageInputLayout_;
    ComPtr<ID3D11Buffer> vertexBuffer_;
    ComPtr<ID3D11Buffer> imageVertexBuffer_;
    ComPtr<ID3D11Buffer> constantBuffer_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11SamplerState> linearSampler_;
    ComPtr<ID3D11BlendState> blendState_;
    ComPtr<ID3D11RasterizerState> rasterizerState_;

    ComPtr<IDWriteFactory> dwFactory_;
    GlyphAtlas glyphAtlas_;
    ImageAtlas imageAtlas_;

    std::vector<Vertex> vertices_;
    std::vector<Vertex> backgroundVertices_;
    std::vector<Vertex> underlineVertices_;
    std::vector<Vertex> titlebarVertices_;
    std::vector<Vertex> titlebarTextVertices_;
    std::vector<Vertex> overlayVertices_;
    std::vector<Vertex> overlayTextVertices_;
    std::vector<ImageVertex> imageVertices_;
    size_t vertexBufferCapacity_ = 0;

    bool ensureVertexBufferCapacity(size_t required);

    GlyphInfo cachedSpaceGlyph_;
    bool spaceGlyphCached_ = false;

    std::vector<Vertex> stagingVertices_;

    float fontSize_ = 14.0f;
    float leftPadding_ = 8.0f;
    float topPadding_ = 8.0f;
    float bottomPadding_ = 8.0f;

public:
    float getLeftPadding() const { return leftPadding_; }
    float getTopPadding() const { return topPadding_; }
    float getBottomPadding() const { return bottomPadding_; }
};
