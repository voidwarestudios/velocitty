#pragma once

#include "../framework.h"
#include "core/Terminal.h"
#include "core/Selection.h"
#include "core/Pane.h"
#include "render/DxRenderer.h"
#include "config/Config.h"
#include "ui/Titlebar.h"
#include "ui/FileSearchOverlay.h"
#include "search/FileSearchService.h"

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

struct ScrollbarMetrics {
    float trackX;
    float trackY;
    float trackWidth;
    float trackHeight;
    float thumbY;
    float thumbHeight;
    bool hasScrollback;
};

class Application {
public:
    Application() = default;
    ~Application() = default;

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run(HINSTANCE hInstance);

private:
    bool initWindow(HINSTANCE hInstance);
    void initTitlebar();
    void calculateGridSize();
    void loadConfig();

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void onChar(wchar_t ch);
    void onKeyDown(UINT vk);
    void onSize(uint32_t width, uint32_t height);
    void onMouseDown(int x, int y, bool rightButton);
    void onMouseMove(int x, int y);
    void onMouseUp(int x, int y);
    void onMouseDoubleClick(int x, int y);
    void render();

    void handleKeyBinding(const std::string& action);
    void copy();
    void paste();
    ScrollbarMetrics getScrollbarMetrics(const ScreenBuffer& buffer, float yOffset);
    bool isPointOnScrollbar(int x, int y);
    void newTab();
    void closeTab();
    void splitHorizontal();
    void splitVertical();
    void closePane();
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void toggleFullscreen();
    void setupPaneImageCallback(Pane* pane);

    void initFileSearch();
    void toggleFileSearch();
    void executeFileAction();
    void toggleContextMenu();

    HWND hwnd_ = nullptr;
    DxRenderer renderer_;
    TabManager tabManager_;
    Titlebar titlebar_;
    Selection* currentSelection_ = nullptr;
    bool windowActive_ = true;

    ULONGLONG lastInputTime_ = 0;
    ULONGLONG lastBlinkToggle_ = 0;
    ULONGLONG lastScrollTime_ = 0;
    bool cursorBlinkOn_ = true;
    static constexpr ULONGLONG blinkIntervalMs_ = 530;
    static constexpr ULONGLONG solidAfterInputMs_ = 600;
    static constexpr ULONGLONG scrollbarVisibleMs_ = 1500;
    static constexpr ULONGLONG scrollbarFadeMs_ = 300;

    uint32_t windowWidth_ = 1024;
    uint32_t windowHeight_ = 768;
    uint16_t cols_ = 80;
    uint16_t rows_ = 30;

    bool running_ = true;
    bool resizing_ = false;
    bool fullscreen_ = false;
    WINDOWPLACEMENT prevWindowPlacement_ = {};

    bool mouseDown_ = false;
    int lastMouseX_ = 0;
    int lastMouseY_ = 0;
    bool handledCtrlC_ = false;
    bool suppressNextChar_ = false;

    bool draggingScrollbar_ = false;
    float scrollbarDragStartY_ = 0.0f;
    uint32_t scrollbarDragStartOffset_ = 0;

    std::unique_ptr<FileSearchOverlay> fileSearchOverlay_;
    std::unique_ptr<FileSearchService> fileSearchService_;

    std::string commandBuffer_;

    static inline UINT WM_VELOCITTY_COMMAND = 0;
    static constexpr WPARAM CMD_OPEN_SEARCH = 1;
};

extern Application* g_app;
