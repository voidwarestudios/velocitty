#include "Titlebar.h"
#include <algorithm>

void Titlebar::setWindowSize(uint32_t width, uint32_t height) {
    windowWidth_ = width;
    windowHeight_ = height;
}

Titlebar::ButtonRect Titlebar::getCloseRect() const {
    return {
        static_cast<float>(windowWidth_) - metrics_.buttonWidth,
        0.0f,
        metrics_.buttonWidth,
        metrics_.height
    };
}

Titlebar::ButtonRect Titlebar::getMaximizeRect() const {
    return {
        static_cast<float>(windowWidth_) - metrics_.buttonWidth * 2.0f,
        0.0f,
        metrics_.buttonWidth,
        metrics_.height
    };
}

Titlebar::ButtonRect Titlebar::getMinimizeRect() const {
    return {
        static_cast<float>(windowWidth_) - metrics_.buttonWidth * 3.0f,
        0.0f,
        metrics_.buttonWidth,
        metrics_.height
    };
}

Titlebar::ButtonRect Titlebar::getIconRect() const {
    float padding = (metrics_.height - metrics_.iconSize) / 2.0f;
    return {
        metrics_.iconPadding,
        padding,
        metrics_.iconSize,
        metrics_.iconSize
    };
}

Titlebar::ButtonRect Titlebar::getTitleRect() const {
    float startX = metrics_.titlePadding;
    float endX = static_cast<float>(windowWidth_) - metrics_.buttonWidth * 3.0f - metrics_.titlePadding;
    return {
        startX,
        0.0f,
        endX - startX,
        metrics_.height
    };
}

Titlebar::ButtonRect Titlebar::getDragRect() const {
    float buttonsWidth = metrics_.buttonWidth * 3.0f;
    auto newTabRect = getNewTabRect();
    float tabsEnd = newTabRect.x + newTabRect.width;
    return {
        tabsEnd,
        0.0f,
        static_cast<float>(windowWidth_) - buttonsWidth - tabsEnd,
        metrics_.height
    };
}

Titlebar::ButtonRect Titlebar::getTabAreaRect() const {
    float windowButtons = metrics_.buttonWidth * 3.0f;
    float availableWidth = static_cast<float>(windowWidth_) - windowButtons - metrics_.newTabButtonWidth;
    return {
        0.0f,
        0.0f,
        availableWidth,
        metrics_.height
    };
}

float Titlebar::getTabWidth() const {
    if (tabs_.empty()) return 0.0f;

    auto tabArea = getTabAreaRect();
    float totalWidth = tabArea.width;
    float tabWidth = totalWidth / static_cast<float>(tabs_.size());

    tabWidth = std::max(tabWidth, metrics_.tabMinWidth);
    tabWidth = std::min(tabWidth, metrics_.tabMaxWidth);

    return tabWidth;
}

Titlebar::ButtonRect Titlebar::getTabRect(size_t index) const {
    if (index >= tabs_.size()) return {0, 0, 0, 0};

    float tabWidth = getTabWidth();
    return {
        static_cast<float>(index) * tabWidth,
        0.0f,
        tabWidth,
        metrics_.height
    };
}

Titlebar::ButtonRect Titlebar::getNewTabRect() const {
    float tabWidth = getTabWidth();
    float tabsWidth = tabs_.empty() ? 0.0f : tabWidth * static_cast<float>(tabs_.size());
    return {
        tabsWidth,
        0.0f,
        metrics_.newTabButtonWidth,
        metrics_.height
    };
}

Titlebar::ButtonRect Titlebar::getTabCloseRect(size_t index) const {
    if (index >= tabs_.size()) return {0, 0, 0, 0};

    auto tabRect = getTabRect(index);
    float closeX = tabRect.x + tabRect.width - metrics_.tabClosePadding - metrics_.tabCloseSize;
    float closeY = (metrics_.height - metrics_.tabCloseSize) / 2.0f;
    return {
        closeX,
        closeY,
        metrics_.tabCloseSize,
        metrics_.tabCloseSize
    };
}

bool Titlebar::pointInRect(int x, int y, const ButtonRect& rect) const {
    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);
    return fx >= rect.x && fx < rect.x + rect.width &&
           fy >= rect.y && fy < rect.y + rect.height;
}

TitlebarButton Titlebar::hitTest(int x, int y) const {
    if (y < 0 || y >= static_cast<int>(metrics_.height)) {
        return TitlebarButton::None;
    }

    if (pointInRect(x, y, getCloseRect())) return TitlebarButton::Close;
    if (pointInRect(x, y, getMaximizeRect())) return TitlebarButton::Maximize;
    if (pointInRect(x, y, getMinimizeRect())) return TitlebarButton::Minimize;
    if (pointInRect(x, y, getNewTabRect())) return TitlebarButton::NewTab;

    return TitlebarButton::None;
}

int Titlebar::hitTestTab(int x, int y) const {
    if (y < 0 || y >= static_cast<int>(metrics_.height)) {
        return -1;
    }

    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (pointInRect(x, y, getTabCloseRect(i))) {
            continue;
        }
        if (pointInRect(x, y, getTabRect(i))) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

int Titlebar::hitTestTabClose(int x, int y) const {
    if (y < 0 || y >= static_cast<int>(metrics_.height)) {
        return -1;
    }

    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (pointInRect(x, y, getTabCloseRect(i))) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

LRESULT Titlebar::handleNcHitTest(int x, int y, HWND hwnd) const {
    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);

    int localX = x - windowRect.left;
    int localY = y - windowRect.top;

    const int resizeBorder = 8;

    bool onLeft = localX < resizeBorder;
    bool onRight = localX >= static_cast<int>(windowWidth_) - resizeBorder;
    bool onTop = localY < resizeBorder;
    bool onBottom = localY >= static_cast<int>(windowHeight_) - resizeBorder;

    if (onTop && onLeft) return HTTOPLEFT;
    if (onTop && onRight) return HTTOPRIGHT;
    if (onBottom && onLeft) return HTBOTTOMLEFT;
    if (onBottom && onRight) return HTBOTTOMRIGHT;
    if (onLeft) return HTLEFT;
    if (onRight) return HTRIGHT;
    if (onTop) return HTTOP;
    if (onBottom) return HTBOTTOM;

    if (localY < static_cast<int>(metrics_.height)) {
        TitlebarButton btn = hitTest(localX, localY);
        switch (btn) {
            case TitlebarButton::Close: return HTCLOSE;
            case TitlebarButton::Maximize: return HTMAXBUTTON;
            case TitlebarButton::Minimize: return HTMINBUTTON;
            case TitlebarButton::NewTab: return HTCLIENT;
            default: break;
        }

        if (hitTestTab(localX, localY) >= 0 || hitTestTabClose(localX, localY) >= 0) {
            return HTCLIENT;
        }

        if (pointInRect(localX, localY, getDragRect())) {
            return HTCAPTION;
        }
    }

    return HTCLIENT;
}

void Titlebar::onMouseMove(int x, int y) {
    TitlebarButton newHover = hitTest(x, y);
    if (newHover != hoveredButton_) {
        hoveredButton_ = newHover;
    }

    int newHoveredTab = hitTestTab(x, y);
    if (newHoveredTab != hoveredTab_) {
        hoveredTab_ = newHoveredTab;
    }

    int newHoveredTabClose = hitTestTabClose(x, y);
    if (newHoveredTabClose != hoveredTabClose_) {
        hoveredTabClose_ = newHoveredTabClose;
    }
}

void Titlebar::onMouseLeave() {
    hoveredButton_ = TitlebarButton::None;
    pressedButton_ = TitlebarButton::None;
    hoveredTab_ = -1;
    pressedTab_ = -1;
    hoveredTabClose_ = -1;
    pressedTabClose_ = -1;
}

void Titlebar::onMouseDown(int x, int y) {
    TitlebarButton btn = hitTest(x, y);
    if (btn != TitlebarButton::None) {
        pressedButton_ = btn;
    }

    int tabClose = hitTestTabClose(x, y);
    if (tabClose >= 0) {
        pressedTabClose_ = tabClose;
        return;
    }

    int tab = hitTestTab(x, y);
    if (tab >= 0) {
        pressedTab_ = tab;
    }
}

void Titlebar::onMouseUp(int x, int y) {
    pressedButton_ = TitlebarButton::None;
    pressedTab_ = -1;
    pressedTabClose_ = -1;
}
