#include "Application.h"
#include "../Resource.h"
#include <algorithm>
#include <shellapi.h>
#include <shlobj.h>

Application* g_app = nullptr;

int Application::run(HINSTANCE hInstance) {
    g_app = this;

    loadConfig();

    if (!initWindow(hInstance)) {
        MessageBoxW(nullptr, L"Failed to create window", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!renderer_.init(hwnd_, windowWidth_, windowHeight_)) {
        MessageBoxW(nullptr, L"Failed to initialize renderer", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    calculateGridSize();

    PaneContainer* firstTab = tabManager_.createTab();
    if (!firstTab) {
        MessageBoxW(nullptr, L"Failed to create tab", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    const auto& termConfig = Config::instance().getTerminal();
    const wchar_t* shell = termConfig.shell.empty() ? nullptr : termConfig.shell.c_str();

    Pane* firstPane = firstTab->createPane(cols_, rows_, shell);
    if (!firstPane) {
        MessageBoxW(nullptr, L"Failed to create pane", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    setupPaneImageCallback(firstPane);

    float titlebarHeight = Config::instance().getTitlebar().customTitlebar
        ? titlebar_.getHeight() + 1.0f
        : 0.0f;

    firstTab->updateLayout(
        static_cast<float>(windowWidth_) - renderer_.getLeftPadding(),
        static_cast<float>(windowHeight_) - titlebarHeight - renderer_.getTopPadding() - renderer_.getBottomPadding(),
        renderer_.getCellWidth(),
        renderer_.getCellHeight()
    );

    currentSelection_ = &firstPane->getSelection();

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    while (running_) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running_ = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!running_) break;

        PaneContainer* activeTab = tabManager_.getActiveTab();
        if (activeTab) {
            for (const auto& pane : activeTab->getPanes()) {
                pane->getTerminal().processOutput();
            }
        }

        if (!resizing_) {
            render();
        }

        bool anyRunning = false;
        if (activeTab) {
            for (const auto& pane : activeTab->getPanes()) {
                if (pane->getTerminal().isRunning()) {
                    anyRunning = true;
                    break;
                }
            }
        }

        if (!anyRunning) {
            Sleep(1);
        }
    }

    Config::instance().save();
    renderer_.shutdown();
    g_app = nullptr;

    return 0;
}

void Application::loadConfig() {
    try {
        Config::instance().load();
        const auto& config = Config::instance();

        if (config.getWindow().width > 0) {
            windowWidth_ = config.getWindow().width;
        }
        if (config.getWindow().height > 0) {
            windowHeight_ = config.getWindow().height;
        }
    } catch (...) {
        // use defaults on config load failure
    }
}

bool Application::initWindow(HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.hCursor = nullptr;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"VelocittyClass";
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_VELOCITTY));
    wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_SMALL));

    if (!RegisterClassExW(&wc)) {
        return false;
    }

    WM_VELOCITTY_COMMAND = RegisterWindowMessageW(L"VELOCITTY_COMMAND");

    const auto& titlebarConfig = Config::instance().getTitlebar();
    bool useCustomTitlebar = titlebarConfig.customTitlebar;

    DWORD style = useCustomTitlebar
        ? (WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU)
        : WS_OVERLAPPEDWINDOW;

    DWORD exStyle = useCustomTitlebar ? WS_EX_APPWINDOW : 0;

    RECT rect = { 0, 0, static_cast<LONG>(windowWidth_), static_cast<LONG>(windowHeight_) };
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);

    int windowW = rect.right - rect.left;
    int windowH = rect.bottom - rect.top;

    int posX = CW_USEDEFAULT;
    int posY = CW_USEDEFAULT;

    if (useCustomTitlebar) {
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        posX = (screenW - windowW) / 2;
        posY = (screenH - windowH) / 2;
    }

    hwnd_ = CreateWindowExW(
        exStyle,
        L"VelocittyClass",
        L"Velocitty",
        style,
        posX, posY,
        windowW,
        windowH,
        nullptr,
        nullptr,
        hInstance,
        this
    );

    if (!hwnd_) return false;

    if (useCustomTitlebar) {
        BOOL darkMode = TRUE;
        DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

        initTitlebar();
    }

    return true;
}

void Application::initTitlebar() {
    const auto& config = Config::instance().getTitlebar();

    TitlebarMetrics metrics;
    metrics.height = config.height;
    metrics.buttonWidth = config.buttonWidth;
    titlebar_.setMetrics(metrics);

    TitlebarColors colors;
    colors.background = config.background;
    colors.backgroundInactive = config.backgroundInactive;
    colors.text = config.text;
    colors.textInactive = config.textInactive;
    colors.buttonHover = config.buttonHover;
    colors.buttonPressed = config.buttonPressed;
    colors.closeHover = config.closeHover;
    titlebar_.setColors(colors);

    titlebar_.setWindowSize(windowWidth_, windowHeight_);
    titlebar_.setTitle(L"Velocitty");
}

void Application::calculateGridSize() {
    if (renderer_.getCellWidth() > 0 && renderer_.getCellHeight() > 0) {
        // must match yOffset calculation in render(): titlebar height + 1px divider
        float titlebarHeight = (Config::instance().getTitlebar().customTitlebar && !fullscreen_)
            ? titlebar_.getHeight() + 1.0f
            : 0.0f;

        float availableWidth = static_cast<float>(windowWidth_) - renderer_.getLeftPadding();
        float availableHeight = static_cast<float>(windowHeight_) - titlebarHeight - renderer_.getTopPadding() - renderer_.getBottomPadding();

        cols_ = static_cast<uint16_t>(availableWidth / renderer_.getCellWidth());
        rows_ = static_cast<uint16_t>(availableHeight / renderer_.getCellHeight());

        if (cols_ < 1) cols_ = 1;
        if (rows_ < 1) rows_ = 1;
    }
}

LRESULT CALLBACK Application::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    auto app = reinterpret_cast<Application*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (app) {
        return app->handleMessage(msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Application::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    const bool useCustomTitlebar = Config::instance().getTitlebar().customTitlebar;

    switch (msg) {
        case WM_GETMINMAXINFO:
            if (useCustomTitlebar) {
                MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
                HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi = { sizeof(mi) };
                if (GetMonitorInfoW(monitor, &mi)) {
                    mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                    mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                    mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                    mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
                }
                return 0;
            }
            break;

        case WM_NCCALCSIZE:
            if (useCustomTitlebar) {
                // return 0 to use entire window as client area (no system-drawn frame)
                return 0;
            }
            break;

        case WM_NCHITTEST:
            if (useCustomTitlebar) {
                LRESULT hit = titlebar_.handleNcHitTest(
                    GET_X_LPARAM(lParam),
                    GET_Y_LPARAM(lParam),
                    hwnd_
                );

                if (hit == HTCLOSE || hit == HTMAXBUTTON || hit == HTMINBUTTON) {
                    return HTCLIENT;
                }

                if (hit != HTCLIENT) {
                    return hit;
                }
            }
            break;

        case WM_NCMOUSEMOVE:
            if (useCustomTitlebar) {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(hwnd_, &pt);
                titlebar_.onMouseMove(pt.x, pt.y);
            }
            break;

        case WM_NCMOUSELEAVE:
            if (useCustomTitlebar) {
                titlebar_.onMouseLeave();
            }
            break;

        case WM_NCLBUTTONDOWN:
            if (useCustomTitlebar) {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(hwnd_, &pt);
                TitlebarButton btn = titlebar_.hitTest(pt.x, pt.y);
                if (btn != TitlebarButton::None) {
                    titlebar_.onMouseDown(pt.x, pt.y);
                    return 0;
                }
            }
            break;

        case WM_NCLBUTTONUP:
            if (useCustomTitlebar) {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(hwnd_, &pt);
                TitlebarButton btn = titlebar_.hitTest(pt.x, pt.y);
                titlebar_.onMouseUp(pt.x, pt.y);

                switch (btn) {
                    case TitlebarButton::Close:
                        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
                        return 0;
                    case TitlebarButton::Maximize:
                        ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
                        return 0;
                    case TitlebarButton::Minimize:
                        ShowWindow(hwnd_, SW_MINIMIZE);
                        return 0;
                    default:
                        break;
                }
            }
            break;

        case WM_NCLBUTTONDBLCLK:
            if (useCustomTitlebar && wParam == HTCAPTION) {
                ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
                resizing_ = false;
                return 0;
            }
            break;

        case WM_SYSCOMMAND:
            {
                WPARAM cmd = wParam & 0xFFF0;
                if (cmd == SC_MAXIMIZE || cmd == SC_RESTORE) {
                    LRESULT result = DefWindowProcW(hwnd_, msg, wParam, lParam);
                    resizing_ = false;
                    return result;
                }
            }
            break;

        case WM_ACTIVATE:
            windowActive_ = (LOWORD(wParam) != WA_INACTIVE);
            titlebar_.setActive(windowActive_);
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;

        case WM_CHAR:
            onChar(static_cast<wchar_t>(wParam));
            return 0;

        case WM_KEYDOWN:
            onKeyDown(static_cast<UINT>(wParam));
            return 0;

        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                onSize(LOWORD(lParam), HIWORD(lParam));
                titlebar_.setWindowSize(windowWidth_, windowHeight_);
                titlebar_.setMaximized(wParam == SIZE_MAXIMIZED);


                if (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED) {
                    resizing_ = false;
                }
            }
            return 0;

        case WM_ENTERSIZEMOVE:
            resizing_ = true;
            return 0;

        case WM_EXITSIZEMOVE:
            resizing_ = false;
            return 0;

        case WM_LBUTTONDOWN:
            {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);

                if (useCustomTitlebar && y < static_cast<int>(titlebar_.getHeight())) {
                    TitlebarButton btn = titlebar_.hitTest(x, y);
                    int tabIndex = titlebar_.hitTestTab(x, y);
                    int tabCloseIndex = titlebar_.hitTestTabClose(x, y);

                    if (btn != TitlebarButton::None || tabIndex >= 0 || tabCloseIndex >= 0) {
                        titlebar_.onMouseDown(x, y);
                        SetCapture(hwnd_);
                        return 0;
                    }
                }

                if (isPointOnScrollbar(x, y)) {
                    PaneContainer* activeTab = tabManager_.getActiveTab();
                    if (activeTab) {
                        Pane* pane = activeTab->getActivePane();
                        if (pane) {
                            auto& buffer = pane->getTerminal().getBuffer();
                            draggingScrollbar_ = true;
                            scrollbarDragStartY_ = static_cast<float>(y);
                            scrollbarDragStartOffset_ = buffer.getViewportOffset();
                            lastScrollTime_ = GetTickCount64();
                            SetCapture(hwnd_);
                            return 0;
                        }
                    }
                }

                onMouseDown(x, y, false);
                SetCapture(hwnd_);
            }
            return 0;

        case WM_RBUTTONDOWN:
            onMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), true);
            return 0;

        case WM_MOUSEMOVE:
            {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);

                if (useCustomTitlebar) {
                    titlebar_.onMouseMove(x, y);
                }

                if (draggingScrollbar_) {
                    PaneContainer* activeTab = tabManager_.getActiveTab();
                    if (activeTab) {
                        Pane* pane = activeTab->getActivePane();
                        if (pane) {
                            auto& buffer = pane->getTerminal().getBuffer();
                            uint32_t scrollbackSize = buffer.getScrollbackSize();

                            if (scrollbackSize > 0) {
                                const bool customTitlebar = Config::instance().getTitlebar().customTitlebar && !fullscreen_;
                                float yOff = customTitlebar ? titlebar_.getHeight() + 1.0f : 0.0f;
                                ScrollbarMetrics m = getScrollbarMetrics(buffer, yOff);

                                float scrollableRange = m.trackHeight - m.thumbHeight;
                                if (scrollableRange > 0) {
                                    float deltaY = static_cast<float>(y) - scrollbarDragStartY_;
                                    float deltaRatio = deltaY / scrollableRange;
                                    int32_t deltaOffset = static_cast<int32_t>(-deltaRatio * scrollbackSize);

                                    int32_t newOffset = static_cast<int32_t>(scrollbarDragStartOffset_) + deltaOffset;
                                    newOffset = std::max(0, std::min(newOffset, static_cast<int32_t>(scrollbackSize)));

                                    if (static_cast<uint32_t>(newOffset) != buffer.getViewportOffset()) {
                                        buffer.setViewportOffset(static_cast<uint32_t>(newOffset));
                                        lastScrollTime_ = GetTickCount64();
                                    }
                                }
                            }
                        }
                    }
                    return 0;
                }

                onMouseMove(x, y);
            }
            return 0;

        case WM_LBUTTONUP:
            {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);

                if (draggingScrollbar_) {
                    draggingScrollbar_ = false;
                    ReleaseCapture();
                    return 0;
                }

                if (useCustomTitlebar && (titlebar_.getPressedButton() != TitlebarButton::None ||
                    titlebar_.getPressedTab() >= 0 || titlebar_.getPressedTabClose() >= 0)) {
                    TitlebarButton btn = titlebar_.hitTest(x, y);
                    int tabIndex = titlebar_.hitTestTab(x, y);
                    int tabCloseIndex = titlebar_.hitTestTabClose(x, y);

                    if (btn == titlebar_.getPressedButton() && btn != TitlebarButton::None) {
                        switch (btn) {
                            case TitlebarButton::Close:
                                PostMessageW(hwnd_, WM_CLOSE, 0, 0);
                                break;
                            case TitlebarButton::Maximize:
                                resizing_ = true;
                                ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
                                resizing_ = false;
                                break;
                            case TitlebarButton::Minimize:
                                ShowWindow(hwnd_, SW_MINIMIZE);
                                break;
                            case TitlebarButton::NewTab:
                                newTab();
                                break;
                            default:
                                break;
                        }
                    }

                    if (tabCloseIndex >= 0 && tabCloseIndex == titlebar_.getPressedTabClose()) {
                        if (tabManager_.getTabCount() > 1) {
                            tabManager_.closeTab(tabManager_.getTabs()[tabCloseIndex].get());
                        } else {
                            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
                        }
                    } else if (tabIndex >= 0 && tabIndex == titlebar_.getPressedTab()) {
                        tabManager_.setActiveTab(static_cast<size_t>(tabIndex));
                    }

                    titlebar_.onMouseUp(x, y);
                    ReleaseCapture();
                    return 0;
                }

                onMouseUp(x, y);
                ReleaseCapture();
            }
            return 0;

        case WM_MOUSELEAVE:
            if (useCustomTitlebar) {
                titlebar_.onMouseLeave();
            }
            break;

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd_, &pt);

                float titlebarHeight = (useCustomTitlebar && !fullscreen_) ? titlebar_.getHeight() : 0.0f;
                if (pt.y < static_cast<int>(titlebarHeight)) {
                    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                } else if (isPointOnScrollbar(pt.x, pt.y)) {
                    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                } else {
                    SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
                }
                return TRUE;
            }
            break;

        case WM_LBUTTONDBLCLK:
            {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);

                if (useCustomTitlebar && y < static_cast<int>(titlebar_.getHeight())) {
                    TitlebarButton btn = titlebar_.hitTest(x, y);
                    if (btn == TitlebarButton::None) {
                        resizing_ = true;
                        ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
                        resizing_ = false;
                        return 0;
                    }
                }

                onMouseDoubleClick(x, y);
            }
            return 0;

        case WM_MOUSEWHEEL:
            {
                short delta = GET_WHEEL_DELTA_WPARAM(wParam);
                PaneContainer* activeTab = tabManager_.getActiveTab();
                if (activeTab) {
                    Pane* pane = activeTab->getActivePane();
                    if (pane) {
                        uint32_t linesToScroll = 3;
                        if (delta > 0) {
                            pane->getTerminal().getBuffer().scrollViewUp(linesToScroll);
                        } else {
                            pane->getTerminal().getBuffer().scrollViewDown(linesToScroll);
                        }
                        lastScrollTime_ = GetTickCount64();
                    }
                }
            }
            return 0;

        case WM_DESTROY:
            running_ = false;
            PostQuitMessage(0);
            return 0;

        default:
            if (msg == WM_VELOCITTY_COMMAND && WM_VELOCITTY_COMMAND != 0) {
                if (wParam == CMD_OPEN_SEARCH) {
                    SetForegroundWindow(hwnd_);
                    toggleFileSearch();
                }
                return 0;
            }
            break;
    }

    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

void Application::onChar(wchar_t ch) {
    if (suppressNextChar_) {
        suppressNextChar_ = false;
        return;
    }

    if (fileSearchOverlay_ && fileSearchOverlay_->isVisible()) {
        if (fileSearchOverlay_->onChar(ch)) {
            if (fileSearchOverlay_->shouldTriggerSearch() && fileSearchService_) {
                fileSearchService_->search(fileSearchOverlay_->getQuery(),
                    [this](const std::vector<SearchResult>& results, bool complete) {
                        if (fileSearchOverlay_) {
                            fileSearchOverlay_->setResults(results, complete);
                        }
                    });
            }
            return;
        }
    }

    PaneContainer* activeTab = tabManager_.getActiveTab();
    if (!activeTab) return;
    Pane* pane = activeTab->getActivePane();
    if (!pane) return;

    lastInputTime_ = GetTickCount64();

    pane->getTerminal().getBuffer().scrollViewToBottom();

    // skip sending Ctrl+C (0x03) if we just handled it for copy
    if (ch == 0x03 && handledCtrlC_) {
        handledCtrlC_ = false;
        return;
    }
    handledCtrlC_ = false;

    // skip control characters that we handle as shortcuts in onKeyDown
    // these would otherwise be sent to the shell which may interpret them
    // 0x14 = Ctrl+T (new tab), 0x16 = Ctrl+V (paste), 0x17 = Ctrl+W (close)
    if (ch == 0x14 || ch == 0x16 || ch == 0x17) {
        return;
    }

    if (ch == '\b' || ch == 0x7F) {
        if (!commandBuffer_.empty()) {
            commandBuffer_.pop_back();
        }
        pane->getTerminal().sendInput("\x7f", 1);
        return;
    }

    if (ch == '\t') {
        commandBuffer_.clear();
        return;
    }

    if (ch < 32) {
        if (ch == '\r' || ch == '\n') {
            if (commandBuffer_ == "vlfind") {
                // erase "vlfind" from the terminal display
                for (size_t i = 0; i < 6; i++) {
                    pane->getTerminal().sendInput("\x7f", 1);
                }
                commandBuffer_.clear();
                toggleFileSearch();
                return;
            }
            if (commandBuffer_ == "vlctx") {
                // erase "vlctx" from the terminal display
                for (size_t i = 0; i < 5; i++) {
                    pane->getTerminal().sendInput("\x7f", 1);
                }
                commandBuffer_.clear();
                toggleContextMenu();
                return;
            }
        }
        commandBuffer_.clear();
        char c = static_cast<char>(ch);
        pane->getTerminal().sendInput(&c, 1);
        return;
    }

    char utf8[4];
    int len = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, utf8, sizeof(utf8), nullptr, nullptr);
    if (len > 0) {
        commandBuffer_.append(utf8, len);
        pane->getTerminal().sendInput(utf8, len);
    }
}

void Application::onKeyDown(UINT vk) {
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

    if (fileSearchOverlay_ && fileSearchOverlay_->isVisible()) {
        if (fileSearchOverlay_->onKeyDown(vk, ctrl, shift)) {
            if (fileSearchOverlay_->shouldTriggerSearch() && fileSearchService_) {
                fileSearchService_->search(fileSearchOverlay_->getQuery(),
                    [this](const std::vector<SearchResult>& results, bool complete) {
                        if (fileSearchOverlay_) {
                            fileSearchOverlay_->setResults(results, complete);
                        }
                    });
            }
            if (fileSearchOverlay_->hasAction()) {
                executeFileAction();
                suppressNextChar_ = true;
            }
            return;
        }
    }

    if (ctrl && shift && vk == 'F') {
        toggleFileSearch();
        suppressNextChar_ = true;
        return;
    }

    if (ctrl && vk == 'C' && currentSelection_ && currentSelection_->hasSelection()) {
        copy();
        handledCtrlC_ = true;
        return;
    }
    if (ctrl && vk == 'V') {
        paste();
        return;
    }
    if (ctrl && vk == 'T') {
        newTab();
        return;
    }
    if (ctrl && vk == 'W') {
        if (alt) {
            closePane();
        } else {
            closeTab();
        }
        return;
    }
    if (ctrl && vk == VK_TAB) {
        if (shift) {
            tabManager_.prevTab();
        } else {
            tabManager_.nextTab();
        }
        return;
    }
    if (ctrl && alt && vk == 'D') {
        splitHorizontal();
        return;
    }
    if (ctrl && shift && vk == 'D') {
        splitVertical();
        return;
    }
    if (ctrl && vk == VK_OEM_PLUS) {
        zoomIn();
        return;
    }
    if (ctrl && vk == VK_OEM_MINUS) {
        zoomOut();
        return;
    }
    if (ctrl && vk == '0') {
        resetZoom();
        return;
    }
    if (vk == VK_F11) {
        toggleFullscreen();
        return;
    }

    PaneContainer* activeTab = tabManager_.getActiveTab();
    if (!activeTab) return;
    Pane* pane = activeTab->getActivePane();
    if (!pane) return;

    // scroll shortcuts
    if (ctrl && !alt && !shift) {
        auto& buffer = pane->getTerminal().getBuffer();
        if (vk == VK_UP) {
            buffer.scrollViewUp(1);
            lastScrollTime_ = GetTickCount64();
            return;
        }
        if (vk == VK_DOWN) {
            buffer.scrollViewDown(1);
            lastScrollTime_ = GetTickCount64();
            return;
        }
        if (vk == VK_PRIOR) {
            buffer.scrollViewUp(buffer.getRows() - 1);
            lastScrollTime_ = GetTickCount64();
            return;
        }
        if (vk == VK_NEXT) {
            buffer.scrollViewDown(buffer.getRows() - 1);
            lastScrollTime_ = GetTickCount64();
            return;
        }
        if (vk == VK_HOME) {
            buffer.scrollViewToTop();
            lastScrollTime_ = GetTickCount64();
            return;
        }
        if (vk == VK_END) {
            buffer.scrollViewToBottom();
            lastScrollTime_ = GetTickCount64();
            return;
        }
    }

    const char* seq = nullptr;

    switch (vk) {
        case VK_UP:    seq = "\x1b[A"; break;
        case VK_DOWN:  seq = "\x1b[B"; break;
        case VK_RIGHT: seq = "\x1b[C"; break;
        case VK_LEFT:  seq = "\x1b[D"; break;
        case VK_HOME:  seq = "\x1b[H"; break;
        case VK_END:   seq = "\x1b[F"; break;
        case VK_DELETE: seq = "\x1b[3~"; break;
        case VK_PRIOR: seq = "\x1b[5~"; break;
        case VK_NEXT:  seq = "\x1b[6~"; break;
        case VK_INSERT: seq = "\x1b[2~"; break;
        case VK_F1:    seq = "\x1bOP"; break;
        case VK_F2:    seq = "\x1bOQ"; break;
        case VK_F3:    seq = "\x1bOR"; break;
        case VK_F4:    seq = "\x1bOS"; break;
        case VK_F5:    seq = "\x1b[15~"; break;
        case VK_F6:    seq = "\x1b[17~"; break;
        case VK_F7:    seq = "\x1b[18~"; break;
        case VK_F8:    seq = "\x1b[19~"; break;
        case VK_F9:    seq = "\x1b[20~"; break;
        case VK_F10:   seq = "\x1b[21~"; break;
        case VK_F12:   seq = "\x1b[24~"; break;
        case VK_TAB:
            pane->getTerminal().getBuffer().scrollViewToBottom();
            if (shift) {
                pane->getTerminal().sendInput("\x1b[Z", 3);
            } else {
                pane->getTerminal().sendInput("\t", 1);
            }
            commandBuffer_.clear();
            return;
        case VK_ESCAPE:
            pane->getTerminal().getBuffer().scrollViewToBottom();
            pane->getTerminal().sendInput("\x1b", 1);
            commandBuffer_.clear();
            return;
        default:
            return;
    }

    if (seq) {
        pane->getTerminal().getBuffer().scrollViewToBottom();
        pane->getTerminal().sendInput(seq, strlen(seq));
        commandBuffer_.clear();
    }
}

void Application::onSize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;

    windowWidth_ = width;
    windowHeight_ = height;

    if (!renderer_.getDevice()) return;

    renderer_.resize(width, height);
    calculateGridSize();

    float titlebarHeight = (Config::instance().getTitlebar().customTitlebar && !fullscreen_)
        ? titlebar_.getHeight() + 1.0f
        : 0.0f;

    PaneContainer* activeTab = tabManager_.getActiveTab();
    if (activeTab) {
        activeTab->updateLayout(
            static_cast<float>(width) - renderer_.getLeftPadding(),
            static_cast<float>(height) - titlebarHeight - renderer_.getTopPadding() - renderer_.getBottomPadding(),
            renderer_.getCellWidth(),
            renderer_.getCellHeight()
        );
    }
}

void Application::render() {
    if (resizing_) return;

    PaneContainer* activeTab = tabManager_.getActiveTab();
    if (!activeTab) return;

    Pane* activePane = activeTab->getActivePane();
    if (!activePane) return;

    const bool useCustomTitlebar = Config::instance().getTitlebar().customTitlebar && !fullscreen_;
    float yOffset = useCustomTitlebar ? titlebar_.getHeight() + 1.0f : 0.0f;

    static std::string lastTitle;
    const std::string& title = activePane->getTerminal().getWindowTitle();
    if (title != lastTitle) {
        lastTitle = title;
        std::wstring displayTitle = L"Velocitty";
        if (!title.empty()) {
            std::wstring wtitle(title.begin(), title.end());
            // don't show raw shell executable paths as the title
            if (wtitle.find(L"\\powershell.exe") == std::wstring::npos &&
                wtitle.find(L"\\cmd.exe") == std::wstring::npos &&
                wtitle.find(L"\\pwsh.exe") == std::wstring::npos) {
                displayTitle = L"Velocitty - " + wtitle;
            }
        }
        if (useCustomTitlebar) {
            titlebar_.setTitle(displayTitle);
        } else {
            SetWindowTextW(hwnd_, displayTitle.c_str());
        }
    }

    renderer_.beginFrame();

    if (useCustomTitlebar) {
        std::vector<TabInfo> tabInfos;
        const auto& tabs = tabManager_.getTabs();
        size_t activeIndex = tabManager_.getActiveTabIndex();

        for (size_t i = 0; i < tabs.size(); ++i) {
            TabInfo info;
            Pane* pane = tabs[i]->getActivePane();
            if (pane) {
                const std::string& paneTitle = pane->getTerminal().getWindowTitle();
                if (!paneTitle.empty()) {
                    std::wstring wtitle(paneTitle.begin(), paneTitle.end());
                    if (wtitle.find(L"\\powershell.exe") == std::wstring::npos &&
                        wtitle.find(L"\\cmd.exe") == std::wstring::npos &&
                        wtitle.find(L"\\pwsh.exe") == std::wstring::npos) {
                        info.title = L"Velocitty - " + wtitle;
                    } else {
                        info.title = L"Velocitty";
                    }
                } else {
                    info.title = L"Velocitty";
                }
            } else {
                info.title = L"Velocitty";
            }
            info.isActive = (i == activeIndex);
            tabInfos.push_back(info);
        }
        titlebar_.setTabs(tabInfos);

        renderer_.renderTitlebar(titlebar_);
    }

    for (const auto& pane : activeTab->getPanes()) {
        const Selection* sel = (pane.get() == activePane) ? currentSelection_ : nullptr;
        renderer_.renderBuffer(pane->getTerminal().getBuffer(), yOffset, sel);
        if (pane.get() == activePane) {
            const auto& buffer = pane->getTerminal().getBuffer();
            float cursorOpacity = 0.0f;

            if (windowActive_ && buffer.isCursorVisible() && !buffer.isScrolledBack()) {
                ULONGLONG now = GetTickCount64();
                ULONGLONG timeSinceInput = now - lastInputTime_;

                if (timeSinceInput < solidAfterInputMs_) {
                    cursorOpacity = 1.0f;
                    cursorBlinkOn_ = true;
                    lastBlinkToggle_ = now;
                } else {
                    if (now - lastBlinkToggle_ >= blinkIntervalMs_) {
                        cursorBlinkOn_ = !cursorBlinkOn_;
                        lastBlinkToggle_ = now;
                    }
                    cursorOpacity = cursorBlinkOn_ ? 1.0f : 0.0f;
                }
            }

            renderer_.drawCursor(
                buffer.getCursorCol(),
                buffer.getCursorRow(),
                yOffset,
                cursorOpacity
            );

            ULONGLONG now = GetTickCount64();
            ULONGLONG timeSinceScroll = now - lastScrollTime_;
            float scrollbarOpacity = 0.0f;

            if (buffer.getScrollbackSize() > 0) {
                if (buffer.isScrolledBack()) {
                    scrollbarOpacity = 1.0f;
                } else if (timeSinceScroll < scrollbarVisibleMs_) {
                    scrollbarOpacity = 1.0f;
                } else if (timeSinceScroll < scrollbarVisibleMs_ + scrollbarFadeMs_) {
                    float fadeProgress = static_cast<float>(timeSinceScroll - scrollbarVisibleMs_) / scrollbarFadeMs_;
                    scrollbarOpacity = 1.0f - fadeProgress;
                }

                if (scrollbarOpacity > 0.0f) {
                    renderer_.renderScrollbar(buffer, yOffset, scrollbarOpacity);
                }
            }
        }
    }

    if (useCustomTitlebar) {
        renderer_.renderBorder(titlebar_.getColors().divider);
    }

    if (fileSearchOverlay_ && fileSearchOverlay_->isVisible()) {
        if (fileSearchService_) {
            fileSearchOverlay_->setIndexProgress(fileSearchService_->getIndexProgress());
        }
        renderer_.renderFileSearchOverlay(*fileSearchOverlay_);
    }

    renderer_.endFrame();
    renderer_.present(Config::instance().getRender().vsync);
}

void Application::onMouseDown(int x, int y, bool rightButton) {
    if (rightButton) {
        paste();
        return;
    }

    float titlebarHeight = Config::instance().getTitlebar().customTitlebar
        ? titlebar_.getHeight()
        : 0.0f;

    int adjustedY = y - static_cast<int>(titlebarHeight) - static_cast<int>(renderer_.getTopPadding());
    if (adjustedY < 0) adjustedY = 0;

    PaneContainer* activeTab = tabManager_.getActiveTab();
    if (!activeTab) return;

    Pane* pane = activeTab->findPaneAt(static_cast<float>(x), static_cast<float>(adjustedY));
    if (pane) {
        activeTab->setActivePane(pane);
        currentSelection_ = &pane->getSelection();
    }

    float cellW = renderer_.getCellWidth();
    float cellH = renderer_.getCellHeight();
    float leftPadding = renderer_.getLeftPadding();
    int adjustedX = x - static_cast<int>(leftPadding);
    if (adjustedX < 0) adjustedX = 0;
    uint16_t col = static_cast<uint16_t>(adjustedX / cellW);
    uint16_t row = static_cast<uint16_t>(adjustedY / cellH);

    if (currentSelection_) {
        currentSelection_->start(col, row);
    }

    mouseDown_ = true;
    lastMouseX_ = x;
    lastMouseY_ = y;
}

void Application::onMouseMove(int x, int y) {
    if (!mouseDown_ || !currentSelection_) return;

    float titlebarHeight = Config::instance().getTitlebar().customTitlebar
        ? titlebar_.getHeight()
        : 0.0f;

    int adjustedY = y - static_cast<int>(titlebarHeight) - static_cast<int>(renderer_.getTopPadding());
    if (adjustedY < 0) adjustedY = 0;

    float cellW = renderer_.getCellWidth();
    float cellH = renderer_.getCellHeight();
    float leftPadding = renderer_.getLeftPadding();
    int adjustedX = x - static_cast<int>(leftPadding);
    if (adjustedX < 0) adjustedX = 0;
    uint16_t col = static_cast<uint16_t>(adjustedX / cellW);
    uint16_t row = static_cast<uint16_t>(adjustedY / cellH);

    currentSelection_->update(col, row);

    lastMouseX_ = x;
    lastMouseY_ = y;
}

void Application::onMouseUp(int x, int y) {
    if (!currentSelection_) return;

    float titlebarHeight = Config::instance().getTitlebar().customTitlebar
        ? titlebar_.getHeight()
        : 0.0f;

    int adjustedY = y - static_cast<int>(titlebarHeight) - static_cast<int>(renderer_.getTopPadding());
    if (adjustedY < 0) adjustedY = 0;

    float cellW = renderer_.getCellWidth();
    float cellH = renderer_.getCellHeight();
    float leftPadding = renderer_.getLeftPadding();
    int adjustedX = x - static_cast<int>(leftPadding);
    if (adjustedX < 0) adjustedX = 0;
    uint16_t col = static_cast<uint16_t>(adjustedX / cellW);
    uint16_t row = static_cast<uint16_t>(adjustedY / cellH);

    currentSelection_->update(col, row);
    currentSelection_->end();

    mouseDown_ = false;
}

void Application::onMouseDoubleClick(int x, int y) {
    PaneContainer* activeTab = tabManager_.getActiveTab();
    if (!activeTab) return;

    Pane* pane = activeTab->getActivePane();
    if (!pane) return;

    float titlebarHeight = Config::instance().getTitlebar().customTitlebar
        ? titlebar_.getHeight()
        : 0.0f;

    int adjustedY = y - static_cast<int>(titlebarHeight) - static_cast<int>(renderer_.getTopPadding());
    if (adjustedY < 0) adjustedY = 0;

    float cellW = renderer_.getCellWidth();
    float cellH = renderer_.getCellHeight();
    float leftPadding = renderer_.getLeftPadding();
    int adjustedX = x - static_cast<int>(leftPadding);
    if (adjustedX < 0) adjustedX = 0;
    uint16_t col = static_cast<uint16_t>(adjustedX / cellW);
    uint16_t row = static_cast<uint16_t>(adjustedY / cellH);

    pane->getSelection().selectWord(col, row, pane->getTerminal().getBuffer());
}

void Application::handleKeyBinding(const std::string& action) {
    if (action == "copy") copy();
    else if (action == "paste") paste();
    else if (action == "newTab") newTab();
    else if (action == "closeTab") closeTab();
    else if (action == "nextTab") tabManager_.nextTab();
    else if (action == "prevTab") tabManager_.prevTab();
    else if (action == "splitHorizontal") splitHorizontal();
    else if (action == "splitVertical") splitVertical();
    else if (action == "closePane") closePane();
    else if (action == "zoomIn") zoomIn();
    else if (action == "zoomOut") zoomOut();
    else if (action == "resetZoom") resetZoom();
    else if (action == "toggleFullscreen") toggleFullscreen();
}

ScrollbarMetrics Application::getScrollbarMetrics(const ScreenBuffer& buffer, float yOffset) {
    ScrollbarMetrics m = {};
    m.hasScrollback = buffer.getScrollbackSize() > 0;

    if (!m.hasScrollback) return m;

    uint32_t scrollbackSize = buffer.getScrollbackSize();
    uint32_t totalLines = buffer.getTotalLines();
    uint32_t visibleLines = buffer.getRows();
    uint32_t viewportOffset = buffer.getViewportOffset();

    float cellH = renderer_.getCellHeight();
    float viewportHeight = visibleLines * cellH + renderer_.getBottomPadding();

    float scrollbarWidth = 6.0f;
    float scrollbarPadding = 2.0f;
    float minThumbHeight = 20.0f;

    m.trackX = static_cast<float>(windowWidth_) - scrollbarWidth - scrollbarPadding;
    m.trackY = yOffset + renderer_.getTopPadding();
    m.trackWidth = scrollbarWidth;
    m.trackHeight = viewportHeight;

    float thumbRatio = static_cast<float>(visibleLines) / static_cast<float>(totalLines);
    m.thumbHeight = std::max(minThumbHeight, m.trackHeight * thumbRatio);

    float scrollableRange = m.trackHeight - m.thumbHeight;
    float maxOffset = static_cast<float>(scrollbackSize);
    float scrollPosition = maxOffset > 0 ? (1.0f - static_cast<float>(viewportOffset) / maxOffset) : 1.0f;
    m.thumbY = m.trackY + scrollPosition * scrollableRange;

    return m;
}

bool Application::isPointOnScrollbar(int x, int y) {
    PaneContainer* activeTab = tabManager_.getActiveTab();
    if (!activeTab) return false;

    Pane* pane = activeTab->getActivePane();
    if (!pane) return false;

    const auto& buffer = pane->getTerminal().getBuffer();
    if (buffer.getScrollbackSize() == 0) return false;

    const bool useCustomTitlebar = Config::instance().getTitlebar().customTitlebar && !fullscreen_;
    float yOffset = useCustomTitlebar ? titlebar_.getHeight() + 1.0f : 0.0f;

    ScrollbarMetrics m = getScrollbarMetrics(buffer, yOffset);

    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);

    float hitPaddingLeft = 8.0f;
    float hitPaddingRight = 12.0f;
    return fx >= m.trackX - hitPaddingLeft &&
           fx <= m.trackX + m.trackWidth + hitPaddingRight &&
           fy >= m.trackY &&
           fy <= m.trackY + m.trackHeight;
}

void Application::copy() {
    if (!currentSelection_ || !currentSelection_->hasSelection()) return;

    PaneContainer* activeTab = tabManager_.getActiveTab();
    if (!activeTab) return;

    Pane* pane = activeTab->getActivePane();
    if (!pane) return;

    std::wstring text = currentSelection_->getSelectedText(pane->getTerminal().getBuffer());
    Selection::copyToClipboard(text);
    currentSelection_->clear();
}

void Application::paste() {
    std::wstring text = Selection::pasteFromClipboard();
    if (text.empty()) return;

    PaneContainer* activeTab = tabManager_.getActiveTab();
    if (!activeTab) return;

    Pane* pane = activeTab->getActivePane();
    if (!pane) return;

    std::string utf8;
    utf8.reserve(text.size() * 2);

    for (wchar_t ch : text) {
        if (ch == L'\r') continue;
        if (ch == L'\n') {
            utf8 += '\r';
        } else {
            char buf[4];
            int len = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, buf, sizeof(buf), nullptr, nullptr);
            if (len > 0) {
                utf8.append(buf, len);
            }
        }
    }

    if (!utf8.empty()) {
        pane->getTerminal().sendInput(utf8.data(), utf8.size());
    }
}

void Application::newTab() {
    PaneContainer* tab = tabManager_.createTab();
    if (!tab) return;

    calculateGridSize();
    const auto& termConfig = Config::instance().getTerminal();
    const wchar_t* shell = termConfig.shell.empty() ? nullptr : termConfig.shell.c_str();

    Pane* pane = tab->createPane(cols_, rows_, shell);
    if (pane) {
        setupPaneImageCallback(pane);
        tabManager_.setActiveTab(tabManager_.getTabCount() - 1);
        currentSelection_ = &pane->getSelection();
    }
}

void Application::closeTab() {
    if (tabManager_.getTabCount() <= 1) {
        running_ = false;
        return;
    }

    PaneContainer* activeTab = tabManager_.getActiveTab();
    if (activeTab) {
        tabManager_.closeTab(activeTab);
    }
}

void Application::splitHorizontal() {
    PaneContainer* activeTab = tabManager_.getActiveTab();
    if (!activeTab) return;

    Pane* pane = activeTab->getActivePane();
    if (!pane) return;

    float titlebarHeight = Config::instance().getTitlebar().customTitlebar
        ? titlebar_.getHeight() + 1.0f
        : 0.0f;

    const auto& termConfig = Config::instance().getTerminal();
    const wchar_t* shell = termConfig.shell.empty() ? nullptr : termConfig.shell.c_str();

    Pane* newPane = activeTab->split(pane, SplitDirection::Horizontal, shell);
    if (newPane) {
        setupPaneImageCallback(newPane);
        activeTab->updateLayout(
            static_cast<float>(windowWidth_) - renderer_.getLeftPadding(),
            static_cast<float>(windowHeight_) - titlebarHeight - renderer_.getTopPadding() - renderer_.getBottomPadding(),
            renderer_.getCellWidth(),
            renderer_.getCellHeight()
        );
    }
}

void Application::splitVertical() {
    PaneContainer* activeTab = tabManager_.getActiveTab();
    if (!activeTab) return;

    Pane* pane = activeTab->getActivePane();
    if (!pane) return;

    float titlebarHeight = Config::instance().getTitlebar().customTitlebar
        ? titlebar_.getHeight() + 1.0f
        : 0.0f;

    const auto& termConfig = Config::instance().getTerminal();
    const wchar_t* shell = termConfig.shell.empty() ? nullptr : termConfig.shell.c_str();

    Pane* newPane = activeTab->split(pane, SplitDirection::Vertical, shell);
    if (newPane) {
        setupPaneImageCallback(newPane);
        activeTab->updateLayout(
            static_cast<float>(windowWidth_) - renderer_.getLeftPadding(),
            static_cast<float>(windowHeight_) - titlebarHeight - renderer_.getTopPadding() - renderer_.getBottomPadding(),
            renderer_.getCellWidth(),
            renderer_.getCellHeight()
        );
    }
}

void Application::closePane() {
    PaneContainer* activeTab = tabManager_.getActiveTab();
    if (!activeTab) return;

    if (activeTab->getPanes().size() <= 1) {
        closeTab();
        return;
    }

    float titlebarHeight = Config::instance().getTitlebar().customTitlebar
        ? titlebar_.getHeight() + 1.0f
        : 0.0f;

    Pane* pane = activeTab->getActivePane();
    if (pane) {
        activeTab->closePane(pane);
        activeTab->updateLayout(
            static_cast<float>(windowWidth_) - renderer_.getLeftPadding(),
            static_cast<float>(windowHeight_) - titlebarHeight - renderer_.getTopPadding() - renderer_.getBottomPadding(),
            renderer_.getCellWidth(),
            renderer_.getCellHeight()
        );
    }
}

void Application::zoomIn() {
	// stubbed this out for now - will implement later probably with DPI scaling
}

void Application::zoomOut() {
    // stubbed this out for now - will implement later probably with DPI scaling
}

void Application::resetZoom() {
    // stubbed this out for now - will implement later probably with DPI scaling
}

void Application::setupPaneImageCallback(Pane* pane) {
    if (!pane) return;
    pane->getTerminal().setImageCallback(
        [this](const uint8_t* rgba, uint32_t w, uint32_t h, uint32_t cellX, uint32_t cellY) {
            renderer_.addImage(rgba, w, h, cellX, cellY);
        }
    );
}

void Application::toggleFullscreen() {
    const bool useCustomTitlebar = Config::instance().getTitlebar().customTitlebar;
    DWORD normalStyle = useCustomTitlebar
        ? (WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU)
        : WS_OVERLAPPEDWINDOW;

    if (!fullscreen_) {
        fullscreen_ = true;
        GetWindowPlacement(hwnd_, &prevWindowPlacement_);
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowPos(hwnd_, HWND_TOP,
                mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

            windowWidth_ = mi.rcMonitor.right - mi.rcMonitor.left;
            windowHeight_ = mi.rcMonitor.bottom - mi.rcMonitor.top;
        }
    } else {
        fullscreen_ = false;
        SetWindowLongPtrW(hwnd_, GWL_STYLE, normalStyle | WS_VISIBLE);
        SetWindowPlacement(hwnd_, &prevWindowPlacement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        RECT clientRect;
        GetClientRect(hwnd_, &clientRect);
        windowWidth_ = clientRect.right;
        windowHeight_ = clientRect.bottom;
    }

    renderer_.resize(windowWidth_, windowHeight_);
    calculateGridSize();
    titlebar_.setWindowSize(windowWidth_, windowHeight_);

    float titlebarHeight = (useCustomTitlebar && !fullscreen_) ? titlebar_.getHeight() + 1.0f : 0.0f;
    PaneContainer* activeTab = tabManager_.getActiveTab();
    if (activeTab) {
        activeTab->updateLayout(
            static_cast<float>(windowWidth_) - renderer_.getLeftPadding(),
            static_cast<float>(windowHeight_) - titlebarHeight - renderer_.getTopPadding() - renderer_.getBottomPadding(),
            renderer_.getCellWidth(),
            renderer_.getCellHeight()
        );
    }
}

void Application::initFileSearch() {
    fileSearchOverlay_ = std::make_unique<FileSearchOverlay>();
    fileSearchService_ = std::make_unique<FileSearchService>();
    fileSearchService_->startIndexing();
}

void Application::toggleFileSearch() {
    if (!fileSearchOverlay_) {
        initFileSearch();
    }

    if (fileSearchOverlay_->isVisible()) {
        fileSearchOverlay_->hide();
    } else {
        fileSearchOverlay_->setWindowSize(
            static_cast<float>(windowWidth_),
            static_cast<float>(windowHeight_)
        );
        fileSearchOverlay_->show();
    }
}

void Application::executeFileAction() {
    if (!fileSearchOverlay_ || !fileSearchOverlay_->hasAction()) return;

    PaneContainer* activeTab = tabManager_.getActiveTab();
    if (!activeTab) return;
    Pane* pane = activeTab->getActivePane();
    if (!pane) return;

    const std::wstring& path = fileSearchOverlay_->getSelectedPath();
    auto action = fileSearchOverlay_->getAction();

    std::string cmd;
    switch (action) {
        case FileSearchOverlay::Action::Cd: {
            std::wstring wpath = path;
            int len = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string utf8path(len - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, utf8path.data(), len, nullptr, nullptr);
            cmd = "cd \"" + utf8path + "\"\r";
            break;
        }
        case FileSearchOverlay::Action::CdParent: {
            size_t pos = path.find_last_of(L'\\');
            std::wstring parent = (pos != std::wstring::npos) ? path.substr(0, pos) : path;
            int len = WideCharToMultiByte(CP_UTF8, 0, parent.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string utf8path(len - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, parent.c_str(), -1, utf8path.data(), len, nullptr, nullptr);
            cmd = "cd \"" + utf8path + "\"\r";
            break;
        }
        case FileSearchOverlay::Action::InsertPath: {
            int len = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string utf8path(len - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8path.data(), len, nullptr, nullptr);
            cmd = "\"" + utf8path + "\"";
            break;
        }
        default:
            break;
    }

    if (!cmd.empty()) {
        pane->getTerminal().sendInput(cmd.c_str(), cmd.length());
    }

    fileSearchOverlay_->clearAction();
}

void Application::toggleContextMenu() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exePathStr = exePath;

    HKEY hKey;
    bool exists = (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Classes\\Directory\\Background\\shell\\Velocitty",
        0, KEY_READ, &hKey) == ERROR_SUCCESS);
    if (exists) {
        RegCloseKey(hKey);
    }

    if (exists) {
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\Background\\shell\\Velocitty");
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\shell\\Velocitty");
    } else {
        if (RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\Classes\\Directory\\Background\\shell\\Velocitty",
            0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                (BYTE*)L"Open in Velocitty", sizeof(L"Open in Velocitty"));
            std::wstring icon = exePathStr + L",0";
            RegSetValueExW(hKey, L"Icon", 0, REG_SZ,
                (BYTE*)icon.c_str(), (DWORD)((icon.length() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }

        if (RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\Classes\\Directory\\Background\\shell\\Velocitty\\command",
            0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
            std::wstring cmd = L"\"" + exePathStr + L"\" \"%V\"";
            RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                (BYTE*)cmd.c_str(), (DWORD)((cmd.length() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }

        if (RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\Classes\\Directory\\shell\\Velocitty",
            0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                (BYTE*)L"Open in Velocitty", sizeof(L"Open in Velocitty"));
            std::wstring icon = exePathStr + L",0";
            RegSetValueExW(hKey, L"Icon", 0, REG_SZ,
                (BYTE*)icon.c_str(), (DWORD)((icon.length() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }

        if (RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\Classes\\Directory\\shell\\Velocitty\\command",
            0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
            std::wstring cmd = L"\"" + exePathStr + L"\" \"%V\"";
            RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                (BYTE*)cmd.c_str(), (DWORD)((cmd.length() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
