#include "DxRenderer.h"
#include "../config/Config.h"
#include <d3dcompiler.h>
#include <algorithm>
#pragma comment(lib, "d3dcompiler.lib")

static const char* shaderCode = R"(
cbuffer Constants : register(b0) {
    float2 screenSize;
    float2 padding;
};

struct VS_INPUT {
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float4 bgColor : COLOR1;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float4 bgColor : COLOR1;
};

Texture2D glyphTexture : register(t0);
SamplerState glyphSampler : register(s0);

PS_INPUT VSMain(VS_INPUT input) {
    PS_INPUT output;
    float2 ndc = (input.pos / screenSize) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    output.pos = float4(ndc, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    output.bgColor = input.bgColor;
    return output;
}

float4 PSMain(PS_INPUT input) : SV_TARGET {
    float alpha = glyphTexture.Sample(glyphSampler, input.uv).r;
    // use texture alpha for output - padding areas become transparent
    // this allows overlapping glyph quads without overwriting neighbors
    return float4(input.color.rgb, alpha * input.color.a);
}

float4 PSBackgroundMain(PS_INPUT input) : SV_TARGET {
    return input.bgColor;
}
)";

static const char* imageShaderCode = R"(
cbuffer Constants : register(b0) {
    float2 screenSize;
    float2 padding;
};

struct VS_INPUT {
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

Texture2D imageTexture : register(t0);
SamplerState imageSampler : register(s0);

PS_INPUT VSImageMain(VS_INPUT input) {
    PS_INPUT output;
    float2 ndc = (input.pos / screenSize) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    output.pos = float4(ndc, 0.0, 1.0);
    output.uv = input.uv;
    return output;
}

float4 PSImageMain(PS_INPUT input) : SV_TARGET {
    return imageTexture.Sample(imageSampler, input.uv);
}
)";

DxRenderer::~DxRenderer() {
    shutdown();
}

bool DxRenderer::init(HWND hwnd, uint32_t width, uint32_t height) {
    hwnd_ = hwnd;
    width_ = width;
    height_ = height;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    if (!createDeviceResources()) return false;
    if (!createShaders()) return false;
    if (!createVertexBuffer()) return false;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                             __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(dwFactory_.GetAddressOf()));
    if (FAILED(hr)) return false;

    const auto& fontConfig = Config::instance().getFont();
    if (!glyphAtlas_.init(device_.Get(), dwFactory_.Get(), fontConfig.family.c_str(), fontConfig.size)) {
        return false;
    }
    fontSize_ = fontConfig.size;

    if (!imageAtlas_.init(device_.Get())) {
        return false;
    }

    updateProjectionMatrix();
    return true;
}

uint32_t DxRenderer::addImage(const uint8_t* rgba, uint32_t width, uint32_t height,
                               uint32_t cellX, uint32_t cellY) {
    float cellW = glyphAtlas_.getCellWidth();
    float cellH = glyphAtlas_.getCellHeight();
    uint32_t cellsW = static_cast<uint32_t>(std::ceil(width / cellW));
    uint32_t cellsH = static_cast<uint32_t>(std::ceil(height / cellH));
    return imageAtlas_.addImage(rgba, width, height, cellX, cellY, cellsW, cellsH);
}

void DxRenderer::removeImage(uint32_t id) {
    imageAtlas_.removeImage(id);
}

bool DxRenderer::createDeviceResources() {
    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createFlags,
        featureLevels,
        1,
        D3D11_SDK_VERSION,
        &device_,
        &featureLevel,
        &context_
    );

    if (FAILED(hr)) return false;

    ComPtr<IDXGIDevice> dxgiDevice;
    device_.As(&dxgiDevice);

    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(&adapter);

    ComPtr<IDXGIFactory2> factory;
    adapter->GetParent(IID_PPV_ARGS(&factory));

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width = width_;
    scd.Height = height_;
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    hr = factory->CreateSwapChainForHwnd(
        device_.Get(),
        hwnd_,
        &scd,
        nullptr,
        nullptr,
        &swapchain_
    );

    if (FAILED(hr)) return false;

    ComPtr<ID3D11Texture2D> backBuffer;
    swapchain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &rtv_);

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device_->CreateBlendState(&blendDesc, &blendState_);

    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_NONE;
    device_->CreateRasterizerState(&rastDesc, &rasterizerState_);

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device_->CreateSamplerState(&sampDesc, &sampler_);

    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    device_->CreateSamplerState(&sampDesc, &linearSampler_);

    return true;
}

bool DxRenderer::createShaders() {
    ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;

    HRESULT hr = D3DCompile(shaderCode, strlen(shaderCode), nullptr, nullptr, nullptr,
                            "VSMain", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
        }
        return false;
    }

    hr = D3DCompile(shaderCode, strlen(shaderCode), nullptr, nullptr, nullptr,
                    "PSMain", "ps_5_0", 0, 0, &psBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
        }
        return false;
    }

    hr = device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                     nullptr, &vertexShader_);
    if (FAILED(hr)) return false;

    hr = device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                    nullptr, &pixelShader_);
    if (FAILED(hr)) return false;

    ComPtr<ID3DBlob> psBgBlob;
    hr = D3DCompile(shaderCode, strlen(shaderCode), nullptr, nullptr, nullptr,
                    "PSBackgroundMain", "ps_5_0", 0, 0, &psBgBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
        }
        return false;
    }

    hr = device_->CreatePixelShader(psBgBlob->GetBufferPointer(), psBgBlob->GetBufferSize(),
                                    nullptr, &backgroundPixelShader_);
    if (FAILED(hr)) return false;

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    hr = device_->CreateInputLayout(layout, 4, vsBlob->GetBufferPointer(),
                                    vsBlob->GetBufferSize(), &inputLayout_);
    if (FAILED(hr)) return false;

    ComPtr<ID3DBlob> vsImageBlob, psImageBlob;

    hr = D3DCompile(imageShaderCode, strlen(imageShaderCode), nullptr, nullptr, nullptr,
                    "VSImageMain", "vs_5_0", 0, 0, &vsImageBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
        }
        return false;
    }

    hr = D3DCompile(imageShaderCode, strlen(imageShaderCode), nullptr, nullptr, nullptr,
                    "PSImageMain", "ps_5_0", 0, 0, &psImageBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
        }
        return false;
    }

    hr = device_->CreateVertexShader(vsImageBlob->GetBufferPointer(), vsImageBlob->GetBufferSize(),
                                      nullptr, &imageVertexShader_);
    if (FAILED(hr)) return false;

    hr = device_->CreatePixelShader(psImageBlob->GetBufferPointer(), psImageBlob->GetBufferSize(),
                                    nullptr, &imagePixelShader_);
    if (FAILED(hr)) return false;

    D3D11_INPUT_ELEMENT_DESC imageLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    hr = device_->CreateInputLayout(imageLayout, 2, vsImageBlob->GetBufferPointer(),
                                    vsImageBlob->GetBufferSize(), &imageInputLayout_);
    if (FAILED(hr)) return false;

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = 16;
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device_->CreateBuffer(&cbDesc, nullptr, &constantBuffer_);
    if (FAILED(hr)) return false;

    return true;
}

bool DxRenderer::createVertexBuffer() {
    vertexBufferCapacity_ = 80 * 30 * 12; // default terminal size in chars (from what i saw anyways)

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = static_cast<UINT>(vertexBufferCapacity_ * sizeof(Vertex));
    vbDesc.Usage = D3D11_USAGE_DYNAMIC;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = device_->CreateBuffer(&vbDesc, nullptr, &vertexBuffer_);
    if (FAILED(hr)) return false;

    vbDesc.ByteWidth = static_cast<UINT>(1024 * sizeof(ImageVertex));
    hr = device_->CreateBuffer(&vbDesc, nullptr, &imageVertexBuffer_);
    if (FAILED(hr)) return false;

    return true;
}

bool DxRenderer::ensureVertexBufferCapacity(size_t required) {
    if (required <= vertexBufferCapacity_) return true;

    size_t newCapacity = std::max(required, vertexBufferCapacity_ * 3 / 2);

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = static_cast<UINT>(newCapacity * sizeof(Vertex));
    vbDesc.Usage = D3D11_USAGE_DYNAMIC;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    ComPtr<ID3D11Buffer> newBuffer;
    HRESULT hr = device_->CreateBuffer(&vbDesc, nullptr, &newBuffer);
    if (FAILED(hr)) return false;

    vertexBuffer_ = newBuffer;
    vertexBufferCapacity_ = newCapacity;
    return true;
}

void DxRenderer::updateProjectionMatrix() {
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context_->Map(constantBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        float* data = static_cast<float*>(mapped.pData);
        data[0] = static_cast<float>(width_);
        data[1] = static_cast<float>(height_);
        data[2] = 0;
        data[3] = 0;
        context_->Unmap(constantBuffer_.Get(), 0);
    }
}

void DxRenderer::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;

    width_ = width;
    height_ = height;

    rtv_.Reset();
    swapchain_->ResizeBuffers(0, width_, height_, DXGI_FORMAT_UNKNOWN, 0);

    ComPtr<ID3D11Texture2D> backBuffer;
    swapchain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &rtv_);

    updateProjectionMatrix();
}

void DxRenderer::shutdown() {
    rtv_.Reset();
    swapchain_.Reset();
    context_.Reset();
    device_.Reset();
}

void DxRenderer::beginFrame() {
    vertices_.resize(0);
    backgroundVertices_.resize(0);
    underlineVertices_.resize(0);
    titlebarVertices_.resize(0);
    titlebarTextVertices_.resize(0);
    overlayVertices_.resize(0);
    overlayTextVertices_.resize(0);
    imageVertices_.resize(0);

    float clearColor[] = {0.118f, 0.118f, 0.118f, 1.0f};
    context_->ClearRenderTargetView(rtv_.Get(), clearColor);
}

const GlyphInfo& DxRenderer::getSpaceGlyph() {
    if (!spaceGlyphCached_) {
        cachedSpaceGlyph_ = glyphAtlas_.getGlyph(' ', false, false);
        spaceGlyphCached_ = true;
    }
    return cachedSpaceGlyph_;
}

void DxRenderer::addColoredQuad(float x, float y, float w, float h, uint32_t color) {
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = (color & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;

    const GlyphInfo& glyph = getSpaceGlyph();

    Vertex v0 = {x, y, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};
    Vertex v1 = {x + w, y, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};
    Vertex v2 = {x, y + h, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};
    Vertex v3 = {x + w, y + h, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};

    titlebarVertices_.push_back(v0);
    titlebarVertices_.push_back(v1);
    titlebarVertices_.push_back(v2);
    titlebarVertices_.push_back(v2);
    titlebarVertices_.push_back(v1);
    titlebarVertices_.push_back(v3);
}

void DxRenderer::renderTitlebarText(const std::wstring& text, float x, float y, uint32_t color, uint32_t bgColor) {
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = (color & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;

    float bgR = ((bgColor >> 16) & 0xFF) / 255.0f;
    float bgG = ((bgColor >> 8) & 0xFF) / 255.0f;
    float bgB = (bgColor & 0xFF) / 255.0f;
    float bgA = ((bgColor >> 24) & 0xFF) / 255.0f;

    float cellW = glyphAtlas_.getCellWidth();
    float currentX = x;

    for (wchar_t ch : text) {
        const GlyphInfo& glyph = glyphAtlas_.getGlyph(ch, false, false);
        if (!glyph.valid) {
            currentX += cellW;
            continue;
        }

        float gx = std::floor(currentX + glyph.offsetX);
        float gy = std::floor(y + glyph.offsetY);
        float gw = glyph.width;
        float gh = glyph.height;

        Vertex v0 = {gx, gy, glyph.u0, glyph.v0, r, g, b, a, bgR, bgG, bgB, bgA};
        Vertex v1 = {gx + gw, gy, glyph.u1, glyph.v0, r, g, b, a, bgR, bgG, bgB, bgA};
        Vertex v2 = {gx, gy + gh, glyph.u0, glyph.v1, r, g, b, a, bgR, bgG, bgB, bgA};
        Vertex v3 = {gx + gw, gy + gh, glyph.u1, glyph.v1, r, g, b, a, bgR, bgG, bgB, bgA};

        titlebarTextVertices_.push_back(v0);
        titlebarTextVertices_.push_back(v1);
        titlebarTextVertices_.push_back(v2);
        titlebarTextVertices_.push_back(v2);
        titlebarTextVertices_.push_back(v1);
        titlebarTextVertices_.push_back(v3);

        currentX += cellW;
    }
}

void DxRenderer::renderTitlebar(const Titlebar& titlebar) {
    const auto& colors = titlebar.getColors();
    const auto& metrics = titlebar.getMetrics();
    const auto& tabs = titlebar.getTabs();

    uint32_t bgColor = titlebar.isActive() ? colors.background : colors.backgroundInactive;
    uint32_t textColor = titlebar.isActive() ? colors.text : colors.textInactive;

    addColoredQuad(0, 0, static_cast<float>(width_), metrics.height, bgColor);

    int hoveredTab = titlebar.getHoveredTab();
    int pressedTab = titlebar.getPressedTab();
    int hoveredTabClose = titlebar.getHoveredTabClose();
    int pressedTabClose = titlebar.getPressedTabClose();

    for (size_t i = 0; i < tabs.size(); ++i) {
        auto tabRect = titlebar.getTabRect(i);
        uint32_t tabBgColor;

        bool isHovered = (static_cast<int>(i) == hoveredTab || static_cast<int>(i) == hoveredTabClose);

        if (tabs[i].isActive) {
            tabBgColor = colors.tabActive;
        } else if (static_cast<int>(i) == pressedTab) {
            tabBgColor = colors.tabHover;
        } else if (isHovered) {
            tabBgColor = colors.tabHover;
        } else {
            tabBgColor = colors.tabInactive;
        }

        addColoredQuad(tabRect.x, tabRect.y, tabRect.width, tabRect.height, tabBgColor);

        if (i > 0) {
            addColoredQuad(tabRect.x, tabRect.y + 6.0f, 1.0f, tabRect.height - 12.0f, colors.divider);
        }

        bool showCloseButton = tabs[i].isActive || isHovered;
        float textX = tabRect.x + metrics.tabPadding;
        float textY = (metrics.height - glyphAtlas_.getCellHeight()) / 2.0f;
        float maxTextWidth = tabRect.width - metrics.tabPadding * 2.0f;

        if (showCloseButton) {
            maxTextWidth -= metrics.tabCloseSize + metrics.tabClosePadding;
        }

        std::wstring displayTitle = tabs[i].title;
        float charWidth = glyphAtlas_.getCellWidth();
        size_t maxChars = static_cast<size_t>(maxTextWidth / charWidth);
        if (maxChars > 0) {
            if (displayTitle.length() > maxChars && maxChars > 3) {
                displayTitle = displayTitle.substr(0, maxChars - 3) + L"...";
            } else if (displayTitle.length() > maxChars) {
                displayTitle = displayTitle.substr(0, maxChars);
            }
        } else {
            displayTitle = L"";
        }

        renderTitlebarText(displayTitle, textX, textY, textColor, tabBgColor);

        if (showCloseButton) {
            auto closeRect = titlebar.getTabCloseRect(i);
            bool closeHovered = (static_cast<int>(i) == hoveredTabClose);
            bool closePressed = (static_cast<int>(i) == pressedTabClose);

            if (closeHovered || closePressed) {
                addColoredQuad(closeRect.x, closeRect.y, closeRect.width, closeRect.height, colors.tabCloseHover);
            }

            float crossCenterX = closeRect.x + closeRect.width / 2.0f;
            float crossCenterY = closeRect.y + closeRect.height / 2.0f;
            float crossSize = 8.0f;

            for (int j = 0; j < static_cast<int>(crossSize); ++j) {
                addColoredQuad(crossCenterX - crossSize / 2.0f + j, crossCenterY - crossSize / 2.0f + j, 1.0f, 1.0f, textColor);
                addColoredQuad(crossCenterX + crossSize / 2.0f - j - 1.0f, crossCenterY - crossSize / 2.0f + j, 1.0f, 1.0f, textColor);
            }
        }
    }

    auto newTabRect = titlebar.getNewTabRect();
    TitlebarButton hovered = titlebar.getHoveredButton();
    TitlebarButton pressed = titlebar.getPressedButton();

    if (hovered == TitlebarButton::NewTab || pressed == TitlebarButton::NewTab) {
        uint32_t btnColor = (pressed == TitlebarButton::NewTab) ? colors.buttonPressed : colors.buttonHover;
        addColoredQuad(newTabRect.x, newTabRect.y, newTabRect.width, newTabRect.height, btnColor);
    }

    float plusCenterX = newTabRect.x + newTabRect.width / 2.0f;
    float plusCenterY = metrics.height / 2.0f;
    float plusSize = 10.0f;
    addColoredQuad(plusCenterX - plusSize / 2.0f, plusCenterY - 0.5f, plusSize, 1.0f, textColor);
    addColoredQuad(plusCenterX - 0.5f, plusCenterY - plusSize / 2.0f, 1.0f, plusSize, textColor);

    auto minRect = titlebar.getMinimizeRect();
    auto maxRect = titlebar.getMaximizeRect();
    auto closeRect = titlebar.getCloseRect();

    if (hovered == TitlebarButton::Minimize || pressed == TitlebarButton::Minimize) {
        uint32_t btnColor = (pressed == TitlebarButton::Minimize) ? colors.buttonPressed : colors.buttonHover;
        addColoredQuad(minRect.x, minRect.y, minRect.width, minRect.height, btnColor);
    }

    if (hovered == TitlebarButton::Maximize || pressed == TitlebarButton::Maximize) {
        uint32_t btnColor = (pressed == TitlebarButton::Maximize) ? colors.buttonPressed : colors.buttonHover;
        addColoredQuad(maxRect.x, maxRect.y, maxRect.width, maxRect.height, btnColor);
    }

    if (hovered == TitlebarButton::Close || pressed == TitlebarButton::Close) {
        uint32_t btnColor = (pressed == TitlebarButton::Close) ? colors.closePressed : colors.closeHover;
        addColoredQuad(closeRect.x, closeRect.y, closeRect.width, closeRect.height, btnColor);
    }

    float iconCenterX = minRect.x + minRect.width / 2.0f;
    float iconCenterY = minRect.height / 2.0f;
    float iconSize = 10.0f;

    addColoredQuad(iconCenterX - iconSize / 2.0f, iconCenterY, iconSize, 1.0f, textColor);

    iconCenterX = maxRect.x + maxRect.width / 2.0f;

    if (titlebar.isMaximized()) {
        float smallSize = 8.0f;
        float offset = 2.0f;

        addColoredQuad(iconCenterX - smallSize / 2.0f + offset, iconCenterY - smallSize / 2.0f - offset, smallSize, 1.0f, textColor);
        addColoredQuad(iconCenterX - smallSize / 2.0f + offset, iconCenterY - smallSize / 2.0f - offset, 1.0f, smallSize, textColor);
        addColoredQuad(iconCenterX + smallSize / 2.0f + offset - 1.0f, iconCenterY - smallSize / 2.0f - offset, 1.0f, smallSize - offset, textColor);
        addColoredQuad(iconCenterX - smallSize / 2.0f + offset, iconCenterY + smallSize / 2.0f - offset - 1.0f, smallSize - offset, 1.0f, textColor);

        addColoredQuad(iconCenterX - smallSize / 2.0f, iconCenterY - smallSize / 2.0f + offset, smallSize, 1.0f, textColor);
        addColoredQuad(iconCenterX - smallSize / 2.0f, iconCenterY - smallSize / 2.0f + offset, 1.0f, smallSize, textColor);
        addColoredQuad(iconCenterX + smallSize / 2.0f - 1.0f, iconCenterY - smallSize / 2.0f + offset, 1.0f, smallSize, textColor);
        addColoredQuad(iconCenterX - smallSize / 2.0f, iconCenterY + smallSize / 2.0f + offset - 1.0f, smallSize, 1.0f, textColor);
    } else {
        addColoredQuad(iconCenterX - iconSize / 2.0f, iconCenterY - iconSize / 2.0f, iconSize, 1.0f, textColor);
        addColoredQuad(iconCenterX - iconSize / 2.0f, iconCenterY - iconSize / 2.0f, 1.0f, iconSize, textColor);
        addColoredQuad(iconCenterX + iconSize / 2.0f - 1.0f, iconCenterY - iconSize / 2.0f, 1.0f, iconSize, textColor);
        addColoredQuad(iconCenterX - iconSize / 2.0f, iconCenterY + iconSize / 2.0f - 1.0f, iconSize, 1.0f, textColor);
    }

    iconCenterX = closeRect.x + closeRect.width / 2.0f;
    uint32_t closeIconColor = (hovered == TitlebarButton::Close || pressed == TitlebarButton::Close) ? 0xFFFFFFFF : textColor;
    float crossSize = 10.0f;
    float lineWidth = 1.0f;

    for (int i = 0; i < static_cast<int>(crossSize); ++i) {
        addColoredQuad(iconCenterX - crossSize / 2.0f + i, iconCenterY - crossSize / 2.0f + i, lineWidth, lineWidth, closeIconColor);
        addColoredQuad(iconCenterX + crossSize / 2.0f - i - 1.0f, iconCenterY - crossSize / 2.0f + i, lineWidth, lineWidth, closeIconColor);
    }

    // renders on top of terminal content
    {
        float r = ((colors.divider >> 16) & 0xFF) / 255.0f;
        float g = ((colors.divider >> 8) & 0xFF) / 255.0f;
        float b = (colors.divider & 0xFF) / 255.0f;
        float a = ((colors.divider >> 24) & 0xFF) / 255.0f;

        const GlyphInfo& glyph = getSpaceGlyph();
        float x = 0, y = metrics.height, w = static_cast<float>(width_), h = 1.0f;

        Vertex v0 = {x, y, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};
        Vertex v1 = {x + w, y, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};
        Vertex v2 = {x, y + h, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};
        Vertex v3 = {x + w, y + h, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};

        overlayVertices_.push_back(v0);
        overlayVertices_.push_back(v1);
        overlayVertices_.push_back(v2);
        overlayVertices_.push_back(v2);
        overlayVertices_.push_back(v1);
        overlayVertices_.push_back(v3);
    }
}

void DxRenderer::renderBorder(uint32_t color) {
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = (color & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;

    const GlyphInfo& glyph = getSpaceGlyph();
    float w = static_cast<float>(width_);
    float h = static_cast<float>(height_);

    auto addQuad = [&](float x, float y, float qw, float qh) {
        Vertex v0 = {x, y, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};
        Vertex v1 = {x + qw, y, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};
        Vertex v2 = {x, y + qh, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};
        Vertex v3 = {x + qw, y + qh, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};
        overlayVertices_.push_back(v0);
        overlayVertices_.push_back(v1);
        overlayVertices_.push_back(v2);
        overlayVertices_.push_back(v2);
        overlayVertices_.push_back(v1);
        overlayVertices_.push_back(v3);
    };

    addQuad(0, 0, w, 1);         // top
    addQuad(0, h - 1, w, 1);     // bottom
    addQuad(0, 0, 1, h);         // left
    addQuad(w - 1, 0, 1, h);     // right
}

void DxRenderer::renderPaneDivider(float x, float y, float length, bool vertical, uint32_t color) {
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = (color & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;

    const GlyphInfo& glyph = getSpaceGlyph();

    float w = vertical ? 1.0f : length;
    float h = vertical ? length : 1.0f;

    Vertex v0 = {x, y, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};
    Vertex v1 = {x + w, y, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};
    Vertex v2 = {x, y + h, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};
    Vertex v3 = {x + w, y + h, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};
    overlayVertices_.push_back(v0);
    overlayVertices_.push_back(v1);
    overlayVertices_.push_back(v2);
    overlayVertices_.push_back(v2);
    overlayVertices_.push_back(v1);
    overlayVertices_.push_back(v3);
}

void DxRenderer::renderBuffer(const ScreenBuffer& buffer, float xOffset, float yOffset, const Selection* selection) {
    float cellW = glyphAtlas_.getCellWidth();
    float cellH = glyphAtlas_.getCellHeight();

    uint32_t viewportOffset = buffer.getViewportOffset();
    uint32_t scrollbackSize = buffer.getScrollbackSize();
    uint32_t startAbsoluteRow = (scrollbackSize > viewportOffset) ? (scrollbackSize - viewportOffset) : 0;

    static constexpr uint32_t defaultBg = 0xFF1E1E1E;
    const GlyphInfo& spaceGlyph = getSpaceGlyph();
    const float spaceU = spaceGlyph.u0;
    const float spaceV = spaceGlyph.v0;

    const uint16_t rows = buffer.getRows();
    const uint16_t cols = buffer.getCols();

    for (uint16_t row = 0; row < rows; ++row) {
        float baseY = row * cellH + yOffset + topPadding_;

        for (uint16_t col = 0; col < cols; ++col) {
            const Cell& cell = (viewportOffset == 0)
                ? buffer.at(col, row)
                : buffer.atAbsolute(col, startAbsoluteRow + row);

            bool isSelected = selection && selection->isSelected(col, row);
            bool isEmptyDefault = (cell.codepoint == U' ' || cell.codepoint == 0) &&
                                  cell.attrs.background == defaultBg &&
                                  cell.attrs.flags == 0 &&
                                  !isSelected;
            if (isEmptyDefault) continue;

            float x = col * cellW + xOffset + leftPadding_;

            uint32_t fg = cell.attrs.foreground;
            uint32_t bg = cell.attrs.background;

            if (cell.attrs.flags & CellAttributes::Inverse) {
                std::swap(fg, bg);
            }
            if (isSelected) {
                std::swap(fg, bg);
            }

            float fgR = ((fg >> 16) & 0xFF) * (1.0f / 255.0f);
            float fgG = ((fg >> 8) & 0xFF) * (1.0f / 255.0f);
            float fgB = (fg & 0xFF) * (1.0f / 255.0f);
            float fgA = ((fg >> 24) & 0xFF) * (1.0f / 255.0f);

            float bgR = ((bg >> 16) & 0xFF) * (1.0f / 255.0f);
            float bgG = ((bg >> 8) & 0xFF) * (1.0f / 255.0f);
            float bgB = (bg & 0xFF) * (1.0f / 255.0f);
            float bgA = ((bg >> 24) & 0xFF) * (1.0f / 255.0f);

            if (bg != defaultBg || isSelected) {
                Vertex b0 = {x, baseY, spaceU, spaceV, bgR, bgG, bgB, bgA, bgR, bgG, bgB, bgA};
                Vertex b1 = {x + cellW, baseY, spaceU, spaceV, bgR, bgG, bgB, bgA, bgR, bgG, bgB, bgA};
                Vertex b2 = {x, baseY + cellH, spaceU, spaceV, bgR, bgG, bgB, bgA, bgR, bgG, bgB, bgA};
                Vertex b3 = {x + cellW, baseY + cellH, spaceU, spaceV, bgR, bgG, bgB, bgA, bgR, bgG, bgB, bgA};
                backgroundVertices_.push_back(b0);
                backgroundVertices_.push_back(b1);
                backgroundVertices_.push_back(b2);
                backgroundVertices_.push_back(b2);
                backgroundVertices_.push_back(b1);
                backgroundVertices_.push_back(b3);
            }

            bool bold = (cell.attrs.flags & CellAttributes::Bold) != 0;
            bool italic = (cell.attrs.flags & CellAttributes::Italic) != 0;

            const GlyphInfo& glyph = glyphAtlas_.getGlyph(cell.codepoint, bold, italic);
            if (!glyph.valid) continue;

            float gx = std::floor(x + glyph.offsetX);
            float gy = std::floor(baseY + glyph.offsetY);
            float gw = glyph.width;
            float gh = glyph.height;

            Vertex v0 = {gx, gy, glyph.u0, glyph.v0, fgR, fgG, fgB, fgA, bgR, bgG, bgB, bgA};
            Vertex v1 = {gx + gw, gy, glyph.u1, glyph.v0, fgR, fgG, fgB, fgA, bgR, bgG, bgB, bgA};
            Vertex v2 = {gx, gy + gh, glyph.u0, glyph.v1, fgR, fgG, fgB, fgA, bgR, bgG, bgB, bgA};
            Vertex v3 = {gx + gw, gy + gh, glyph.u1, glyph.v1, fgR, fgG, fgB, fgA, bgR, bgG, bgB, bgA};

            vertices_.push_back(v0);
            vertices_.push_back(v1);
            vertices_.push_back(v2);
            vertices_.push_back(v2);
            vertices_.push_back(v1);
            vertices_.push_back(v3);

            uint16_t flags = cell.attrs.flags;
            if (flags & (CellAttributes::Underline | CellAttributes::Hyperlink)) {
                float underlineY = baseY + cellH - 2.0f;
                Vertex u0 = {x, underlineY, spaceU, spaceV, fgR, fgG, fgB, fgA, fgR, fgG, fgB, fgA};
                Vertex u1 = {x + cellW, underlineY, spaceU, spaceV, fgR, fgG, fgB, fgA, fgR, fgG, fgB, fgA};
                Vertex u2 = {x, underlineY + 1.0f, spaceU, spaceV, fgR, fgG, fgB, fgA, fgR, fgG, fgB, fgA};
                Vertex u3 = {x + cellW, underlineY + 1.0f, spaceU, spaceV, fgR, fgG, fgB, fgA, fgR, fgG, fgB, fgA};
                underlineVertices_.push_back(u0);
                underlineVertices_.push_back(u1);
                underlineVertices_.push_back(u2);
                underlineVertices_.push_back(u2);
                underlineVertices_.push_back(u1);
                underlineVertices_.push_back(u3);
            }

            if (flags & CellAttributes::Strikethrough) {
                float strikeY = baseY + cellH * 0.5f;
                Vertex s0 = {x, strikeY, spaceU, spaceV, fgR, fgG, fgB, fgA, fgR, fgG, fgB, fgA};
                Vertex s1 = {x + cellW, strikeY, spaceU, spaceV, fgR, fgG, fgB, fgA, fgR, fgG, fgB, fgA};
                Vertex s2 = {x, strikeY + 1.0f, spaceU, spaceV, fgR, fgG, fgB, fgA, fgR, fgG, fgB, fgA};
                Vertex s3 = {x + cellW, strikeY + 1.0f, spaceU, spaceV, fgR, fgG, fgB, fgA, fgR, fgG, fgB, fgA};
                underlineVertices_.push_back(s0);
                underlineVertices_.push_back(s1);
                underlineVertices_.push_back(s2);
                underlineVertices_.push_back(s2);
                underlineVertices_.push_back(s1);
                underlineVertices_.push_back(s3);
            }
        }
    }
}

void DxRenderer::drawCursor(uint16_t col, uint16_t row, float xOffset, float yOffset, float opacity) {
    if (opacity <= 0.0f) return;

    float cellW = glyphAtlas_.getCellWidth();
    float cellH = glyphAtlas_.getCellHeight();

    float x = col * cellW + xOffset + leftPadding_;
    float y = row * cellH + cellH - 2.0f + yOffset + topPadding_;
    float w = cellW;
    float h = 2.0f;

    const GlyphInfo& glyph = getSpaceGlyph();

    Vertex v0 = {x, y, glyph.u0, glyph.v0, 1, 1, 1, opacity, 1, 1, 1, opacity};
    Vertex v1 = {x + w, y, glyph.u0, glyph.v0, 1, 1, 1, opacity, 1, 1, 1, opacity};
    Vertex v2 = {x, y + h, glyph.u0, glyph.v0, 1, 1, 1, opacity, 1, 1, 1, opacity};
    Vertex v3 = {x + w, y + h, glyph.u0, glyph.v0, 1, 1, 1, opacity, 1, 1, 1, opacity};

    overlayVertices_.push_back(v0);
    overlayVertices_.push_back(v1);
    overlayVertices_.push_back(v2);
    overlayVertices_.push_back(v2);
    overlayVertices_.push_back(v1);
    overlayVertices_.push_back(v3);
}

void DxRenderer::renderScrollbar(const ScreenBuffer& buffer, float xOffset, float yOffset, float opacity) {
    if (opacity <= 0.0f) return;

    uint32_t scrollbackSize = buffer.getScrollbackSize();
    if (scrollbackSize == 0) return;

    uint32_t totalLines = buffer.getTotalLines();
    uint32_t visibleLines = buffer.getRows();
    uint32_t viewportOffset = buffer.getViewportOffset();

    float cellH = glyphAtlas_.getCellHeight();
    float cellW = glyphAtlas_.getCellWidth();
    float viewportHeight = visibleLines * cellH + bottomPadding_;
    float paneWidth = buffer.getCols() * cellW + leftPadding_;

    float scrollbarWidth = 6.0f;
    float scrollbarPadding = 2.0f;
    float minThumbHeight = 20.0f;

    float trackX = xOffset + paneWidth - scrollbarWidth - scrollbarPadding;
    float trackY = yOffset + topPadding_;
    float trackHeight = viewportHeight;

    float thumbRatio = static_cast<float>(visibleLines) / static_cast<float>(totalLines);
    float thumbHeight = std::max(minThumbHeight, trackHeight * thumbRatio);

    float scrollableRange = trackHeight - thumbHeight;
    float maxOffset = static_cast<float>(scrollbackSize);
    float scrollPosition = maxOffset > 0 ? (1.0f - static_cast<float>(viewportOffset) / maxOffset) : 1.0f;
    float thumbY = trackY + scrollPosition * scrollableRange;

    const GlyphInfo& glyph = getSpaceGlyph();

    float r = 0.6f, g = 0.6f, b = 0.6f, a = opacity * 0.5f;

    float x = trackX;
    float y = thumbY;
    float w = scrollbarWidth;
    float h = thumbHeight;

    Vertex v0 = {x, y, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};
    Vertex v1 = {x + w, y, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};
    Vertex v2 = {x, y + h, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};
    Vertex v3 = {x + w, y + h, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};

    overlayVertices_.push_back(v0);
    overlayVertices_.push_back(v1);
    overlayVertices_.push_back(v2);
    overlayVertices_.push_back(v2);
    overlayVertices_.push_back(v1);
    overlayVertices_.push_back(v3);
}

void DxRenderer::endFrame() {
    bool hasImages = !imageAtlas_.getImages().empty();
    if (vertices_.empty() && backgroundVertices_.empty() && underlineVertices_.empty() &&
        titlebarVertices_.empty() && titlebarTextVertices_.empty() &&
        overlayVertices_.empty() && overlayTextVertices_.empty() && !hasImages) return;
    if (!rtv_ || !context_ || !vertexBuffer_) return;

    size_t totalVertices = titlebarVertices_.size() + titlebarTextVertices_.size() +
                           backgroundVertices_.size() + vertices_.size() +
                           underlineVertices_.size() + overlayVertices_.size() +
                           overlayTextVertices_.size();

    if (!ensureVertexBufferCapacity(totalVertices)) return;

    stagingVertices_.resize(0);
    stagingVertices_.reserve(totalVertices);

    size_t titlebarStart = 0;
    size_t titlebarCount = titlebarVertices_.size();
    stagingVertices_.insert(stagingVertices_.end(), titlebarVertices_.begin(), titlebarVertices_.end());

    size_t titlebarTextStart = stagingVertices_.size();
    size_t titlebarTextCount = titlebarTextVertices_.size();
    stagingVertices_.insert(stagingVertices_.end(), titlebarTextVertices_.begin(), titlebarTextVertices_.end());

    size_t bgStart = stagingVertices_.size();
    size_t bgCount = backgroundVertices_.size();
    stagingVertices_.insert(stagingVertices_.end(), backgroundVertices_.begin(), backgroundVertices_.end());

    size_t glyphStart = stagingVertices_.size();
    size_t glyphCount = vertices_.size();
    stagingVertices_.insert(stagingVertices_.end(), vertices_.begin(), vertices_.end());

    size_t underlineStart = stagingVertices_.size();
    size_t underlineCount = underlineVertices_.size();
    stagingVertices_.insert(stagingVertices_.end(), underlineVertices_.begin(), underlineVertices_.end());

    size_t overlayStart = stagingVertices_.size();
    size_t overlayCount = overlayVertices_.size();
    stagingVertices_.insert(stagingVertices_.end(), overlayVertices_.begin(), overlayVertices_.end());

    size_t overlayTextStart = stagingVertices_.size();
    size_t overlayTextCount = overlayTextVertices_.size();
    stagingVertices_.insert(stagingVertices_.end(), overlayTextVertices_.begin(), overlayTextVertices_.end());

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(context_->Map(vertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    memcpy(mapped.pData, stagingVertices_.data(), stagingVertices_.size() * sizeof(Vertex));
    context_->Unmap(vertexBuffer_.Get(), 0);

    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(width_);
    vp.Height = static_cast<float>(height_);
    vp.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &vp);

    context_->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);
    context_->OMSetBlendState(blendState_.Get(), nullptr, 0xFFFFFFFF);
    context_->RSSetState(rasterizerState_.Get());

    context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, constantBuffer_.GetAddressOf());

    ID3D11ShaderResourceView* srv = glyphAtlas_.getTextureSRV();
    context_->PSSetShaderResources(0, 1, &srv);
    context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());

    context_->IASetInputLayout(inputLayout_.Get());
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    context_->IASetVertexBuffers(0, 1, vertexBuffer_.GetAddressOf(), &stride, &offset);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    if (titlebarCount > 0) {
        context_->PSSetShader(backgroundPixelShader_.Get(), nullptr, 0);
        context_->Draw(static_cast<UINT>(titlebarCount), static_cast<UINT>(titlebarStart));
    }

    if (titlebarTextCount > 0) {
        context_->PSSetShader(pixelShader_.Get(), nullptr, 0);
        context_->Draw(static_cast<UINT>(titlebarTextCount), static_cast<UINT>(titlebarTextStart));
    }

    if (bgCount > 0) {
        context_->PSSetShader(backgroundPixelShader_.Get(), nullptr, 0);
        context_->Draw(static_cast<UINT>(bgCount), static_cast<UINT>(bgStart));
    }

    if (glyphCount > 0) {
        context_->PSSetShader(pixelShader_.Get(), nullptr, 0);
        context_->Draw(static_cast<UINT>(glyphCount), static_cast<UINT>(glyphStart));
    }

    if (underlineCount > 0) {
        context_->PSSetShader(backgroundPixelShader_.Get(), nullptr, 0);
        context_->Draw(static_cast<UINT>(underlineCount), static_cast<UINT>(underlineStart));
    }

    if (overlayCount > 0) {
        context_->PSSetShader(backgroundPixelShader_.Get(), nullptr, 0);
        context_->Draw(static_cast<UINT>(overlayCount), static_cast<UINT>(overlayStart));
    }

    if (overlayTextCount > 0) {
        context_->PSSetShader(pixelShader_.Get(), nullptr, 0);
        context_->Draw(static_cast<UINT>(overlayTextCount), static_cast<UINT>(overlayTextStart));
    }

    renderImages();
}

void DxRenderer::renderUnderlines() {
}

void DxRenderer::renderImages() {
    const auto& images = imageAtlas_.getImages();
    if (images.empty()) return;

    imageVertices_.clear();

    float cellW = glyphAtlas_.getCellWidth();
    float cellH = glyphAtlas_.getCellHeight();

    for (const auto& [id, img] : images) {
        if (!img.valid) continue;

        float x = img.cellX * cellW;
        float y = img.cellY * cellH;
        float w = static_cast<float>(img.width);
        float h = static_cast<float>(img.height);

        ImageVertex v0 = {x, y, img.u0, img.v0};
        ImageVertex v1 = {x + w, y, img.u1, img.v0};
        ImageVertex v2 = {x, y + h, img.u0, img.v1};
        ImageVertex v3 = {x + w, y + h, img.u1, img.v1};

        imageVertices_.push_back(v0);
        imageVertices_.push_back(v1);
        imageVertices_.push_back(v2);
        imageVertices_.push_back(v2);
        imageVertices_.push_back(v1);
        imageVertices_.push_back(v3);
    }

    if (imageVertices_.empty()) return;

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(context_->Map(imageVertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    memcpy(mapped.pData, imageVertices_.data(), imageVertices_.size() * sizeof(ImageVertex));
    context_->Unmap(imageVertexBuffer_.Get(), 0);

    context_->IASetInputLayout(imageInputLayout_.Get());
    UINT stride = sizeof(ImageVertex);
    UINT offset = 0;
    context_->IASetVertexBuffers(0, 1, imageVertexBuffer_.GetAddressOf(), &stride, &offset);

    context_->VSSetShader(imageVertexShader_.Get(), nullptr, 0);
    context_->PSSetShader(imagePixelShader_.Get(), nullptr, 0);

    ID3D11ShaderResourceView* srv = imageAtlas_.getTextureSRV();
    context_->PSSetShaderResources(0, 1, &srv);
    context_->PSSetSamplers(0, 1, linearSampler_.GetAddressOf());

    context_->Draw(static_cast<UINT>(imageVertices_.size()), 0);

    context_->IASetInputLayout(inputLayout_.Get());
    stride = sizeof(Vertex);
    context_->IASetVertexBuffers(0, 1, vertexBuffer_.GetAddressOf(), &stride, &offset);
    context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
    context_->PSSetShader(pixelShader_.Get(), nullptr, 0);
    srv = glyphAtlas_.getTextureSRV();
    context_->PSSetShaderResources(0, 1, &srv);
    context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
}

void DxRenderer::addOverlayQuad(float x, float y, float w, float h, uint32_t color) {
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = (color & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;

    const GlyphInfo& glyph = getSpaceGlyph();

    Vertex v0 = {x, y, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};
    Vertex v1 = {x + w, y, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};
    Vertex v2 = {x, y + h, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};
    Vertex v3 = {x + w, y + h, glyph.u0, glyph.v0, r, g, b, a, r, g, b, a};

    overlayVertices_.push_back(v0);
    overlayVertices_.push_back(v1);
    overlayVertices_.push_back(v2);
    overlayVertices_.push_back(v2);
    overlayVertices_.push_back(v1);
    overlayVertices_.push_back(v3);
}

void DxRenderer::renderOverlayText(const std::wstring& text, float x, float y,
                                    uint32_t color, uint32_t bgColor) {
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = (color & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;

    float bgR = ((bgColor >> 16) & 0xFF) / 255.0f;
    float bgG = ((bgColor >> 8) & 0xFF) / 255.0f;
    float bgB = (bgColor & 0xFF) / 255.0f;
    float bgA = ((bgColor >> 24) & 0xFF) / 255.0f;

    float cellW = glyphAtlas_.getCellWidth();
    float currentX = x;

    for (wchar_t ch : text) {
        const GlyphInfo& glyph = glyphAtlas_.getGlyph(ch, false, false);
        if (!glyph.valid) {
            currentX += cellW;
            continue;
        }

        float gx = std::floor(currentX + glyph.offsetX);
        float gy = std::floor(y + glyph.offsetY);
        float gw = glyph.width;
        float gh = glyph.height;

        Vertex v0 = {gx, gy, glyph.u0, glyph.v0, r, g, b, a, bgR, bgG, bgB, bgA};
        Vertex v1 = {gx + gw, gy, glyph.u1, glyph.v0, r, g, b, a, bgR, bgG, bgB, bgA};
        Vertex v2 = {gx, gy + gh, glyph.u0, glyph.v1, r, g, b, a, bgR, bgG, bgB, bgA};
        Vertex v3 = {gx + gw, gy + gh, glyph.u1, glyph.v1, r, g, b, a, bgR, bgG, bgB, bgA};

        overlayTextVertices_.push_back(v0);
        overlayTextVertices_.push_back(v1);
        overlayTextVertices_.push_back(v2);
        overlayTextVertices_.push_back(v2);
        overlayTextVertices_.push_back(v1);
        overlayTextVertices_.push_back(v3);

        currentX += cellW;
    }
}

void DxRenderer::renderOverlayTextHighlighted(const std::wstring& text, float x, float y,
                                               uint32_t normalColor, uint32_t highlightColor,
                                               size_t highlightStart, size_t highlightLen,
                                               uint32_t bgColor) {
    float cellW = glyphAtlas_.getCellWidth();
    float currentX = x;

    float bgR = ((bgColor >> 16) & 0xFF) / 255.0f;
    float bgG = ((bgColor >> 8) & 0xFF) / 255.0f;
    float bgB = (bgColor & 0xFF) / 255.0f;
    float bgA = ((bgColor >> 24) & 0xFF) / 255.0f;

    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t ch = text[i];

        bool isHighlighted = (i >= highlightStart && i < highlightStart + highlightLen);
        uint32_t color = isHighlighted ? highlightColor : normalColor;

        float r = ((color >> 16) & 0xFF) / 255.0f;
        float g = ((color >> 8) & 0xFF) / 255.0f;
        float b = (color & 0xFF) / 255.0f;
        float a = ((color >> 24) & 0xFF) / 255.0f;

        const GlyphInfo& glyph = glyphAtlas_.getGlyph(ch, false, false);
        if (!glyph.valid) {
            currentX += cellW;
            continue;
        }

        float gx = std::floor(currentX + glyph.offsetX);
        float gy = std::floor(y + glyph.offsetY);
        float gw = glyph.width;
        float gh = glyph.height;

        Vertex v0 = {gx, gy, glyph.u0, glyph.v0, r, g, b, a, bgR, bgG, bgB, bgA};
        Vertex v1 = {gx + gw, gy, glyph.u1, glyph.v0, r, g, b, a, bgR, bgG, bgB, bgA};
        Vertex v2 = {gx, gy + gh, glyph.u0, glyph.v1, r, g, b, a, bgR, bgG, bgB, bgA};
        Vertex v3 = {gx + gw, gy + gh, glyph.u1, glyph.v1, r, g, b, a, bgR, bgG, bgB, bgA};

        overlayTextVertices_.push_back(v0);
        overlayTextVertices_.push_back(v1);
        overlayTextVertices_.push_back(v2);
        overlayTextVertices_.push_back(v2);
        overlayTextVertices_.push_back(v1);
        overlayTextVertices_.push_back(v3);

        currentX += cellW;
    }
}

void DxRenderer::renderFileSearchOverlay(const FileSearchOverlay& overlay) {
    if (!overlay.isVisible()) return;

    float winW = static_cast<float>(width_);
    float winH = static_cast<float>(height_);
    float cellH = glyphAtlas_.getCellHeight();
    float cellW = glyphAtlas_.getCellWidth();

    constexpr uint32_t dimBg = 0x80000000;
    constexpr uint32_t panelBg = 0xF0252526;
    constexpr uint32_t searchBoxBg = 0xFF3C3C3C;
    constexpr uint32_t borderColor = 0xFF007ACC;
    constexpr uint32_t textColor = 0xFFCCCCCC;
    constexpr uint32_t textDim = 0xFF808080;
    constexpr uint32_t selectedBg = 0xFF094771;
    constexpr uint32_t highlightColor = 0xFFE8AB53;
    constexpr uint32_t folderColor = 0xFFDCB67A;
    constexpr uint32_t fileColor = 0xFF75BEFF;

    addOverlayQuad(0, 0, winW, winH, dimBg);

    auto panel = overlay.getOverlayRect(winW, winH);
    addOverlayQuad(panel.x, panel.y, panel.w, panel.h, panelBg);

    auto searchBox = overlay.getSearchBoxRect(winW, winH);
    addOverlayQuad(searchBox.x, searchBox.y, searchBox.w, searchBox.h, searchBoxBg);

    addOverlayQuad(searchBox.x, searchBox.y, searchBox.w, 2, borderColor);

    float textX = searchBox.x + 12;
    float textY = searchBox.y + (searchBox.h - cellH) / 2;
    const std::wstring& query = overlay.getQuery();

    if (query.empty()) {
        renderOverlayText(L"Search files...", textX, textY, textDim, 0);
    } else {
        renderOverlayText(query, textX, textY, textColor, 0);
    }

        float cursorX = textX + query.length() * cellW;
    addOverlayQuad(cursorX, textY, 2, cellH, textColor);

    if (overlay.isIndexing()) {
        float progress = overlay.getIndexProgress();
        addOverlayQuad(searchBox.x, searchBox.y + searchBox.h - 2,
                       searchBox.w * progress, 2, borderColor);
    }

    const auto& results = overlay.getResults();
    int scrollOffset = overlay.getScrollOffset();
    int selectedIndex = overlay.getSelectedIndex();
    int maxVisible = overlay.getMaxVisible();

    auto resultsRect = overlay.getResultsRect(winW, winH);
    float y = resultsRect.y;

    for (int i = scrollOffset; i < std::min(scrollOffset + maxVisible, static_cast<int>(results.size())); ++i) {
        const auto& r = results[i];

        uint32_t rowBg = (i == selectedIndex) ? selectedBg : panelBg;
        addOverlayQuad(resultsRect.x, y, resultsRect.w, FileSearchOverlay::resultHeight_, rowBg);

        float iconX = resultsRect.x + 8;
        float iconY = y + (FileSearchOverlay::resultHeight_ - cellH) / 2;
        uint32_t iconColor = r.isDirectory ? folderColor : fileColor;
        renderOverlayText(r.isDirectory ? L">" : L"#", iconX, iconY, iconColor, 0);

        float nameX = resultsRect.x + 28;
        float nameY = y + (FileSearchOverlay::resultHeight_ - cellH) / 2;
        float maxRowWidth = resultsRect.w - 28 - 8;
        size_t maxRowChars = static_cast<size_t>(maxRowWidth / cellW);

        std::wstring displayName = r.displayName;
        size_t matchStart = r.matchStart;
        size_t matchLen = r.matchLen;

        size_t reserveForPath = std::min(static_cast<size_t>(20), maxRowChars / 3);
        size_t maxNameChars = maxRowChars > reserveForPath ? maxRowChars - reserveForPath - 1 : maxRowChars;

        if (displayName.length() > maxNameChars && maxNameChars > 3) {
            displayName = displayName.substr(0, maxNameChars - 3) + L"...";
            if (matchStart + matchLen > displayName.length()) {
                if (matchStart >= displayName.length()) {
                    matchLen = 0;
                } else {
                    matchLen = displayName.length() - matchStart;
                }
            }
        }

        renderOverlayTextHighlighted(displayName, nameX, nameY,
                                     textColor, highlightColor,
                                     matchStart, matchLen, 0);

        size_t nameLen = displayName.length();
        size_t remainingChars = maxRowChars > nameLen + 1 ? maxRowChars - nameLen - 1 : 0;

        if (remainingChars > 5) {
            float pathX = nameX + (nameLen + 1) * cellW;
            std::wstring displayPath = r.fullPath;

            if (displayPath.length() > remainingChars && remainingChars > 3) {
                displayPath = L"..." + displayPath.substr(displayPath.length() - remainingChars + 3);
            }
            renderOverlayText(displayPath, pathX, nameY, textDim, 0);
        }

        y += FileSearchOverlay::resultHeight_;
    }

    auto hintBar = overlay.getHintBarRect(winW, winH);
    addOverlayQuad(hintBar.x, hintBar.y - 4, hintBar.w, 1, 0xFF3C3C3C);

    std::wstring hint = L"Enter: select | Shift+Enter: cd parent | Esc: close";
    renderOverlayText(hint, hintBar.x, hintBar.y + 6, textDim, 0);
    if (!results.empty()) {
        std::wstring countStr = std::to_wstring(results.size()) + L" results";
        float countX = hintBar.x + hintBar.w - countStr.length() * cellW - 8;
        renderOverlayText(countStr, countX, hintBar.y + 6, textDim, 0);
    } else if (overlay.isIndexing()) {
        int pct = static_cast<int>(overlay.getIndexProgress() * 100);
        std::wstring statusStr = L"Indexing... " + std::to_wstring(pct) + L"%";
        float statusX = hintBar.x + hintBar.w - statusStr.length() * cellW - 8;
        renderOverlayText(statusStr, statusX, hintBar.y + 6, textDim, 0);
    } else if (results.empty() && !query.empty()) {
        renderOverlayText(L"No results", hintBar.x + hintBar.w - 11 * cellW, hintBar.y + 6, textDim, 0);
    }
}

void DxRenderer::present(bool vsync) {
    swapchain_->Present(vsync ? 1 : 0, 0);
}
