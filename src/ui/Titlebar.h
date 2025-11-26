#pragma once

#include "../../framework.h"
#include <string>
#include <functional>
#include <vector>

enum class TitlebarButton {
    None,
    Minimize,
    Maximize,
    Close,
    NewTab
};

struct TitlebarMetrics {
    float height = 32.0f;
    float buttonWidth = 46.0f;
    float iconSize = 16.0f;
    float iconPadding = 12.0f;
    float titlePadding = 8.0f;
    float tabMinWidth = 100.0f;
    float tabMaxWidth = 200.0f;
    float tabPadding = 8.0f;
    float tabCloseSize = 16.0f;
    float tabClosePadding = 8.0f;
    float newTabButtonWidth = 32.0f;
};

struct TitlebarColors {
    uint32_t background = 0xFF1E1E1E;
    uint32_t backgroundInactive = 0xFF2D2D2D;
    uint32_t text = 0xFFCCCCCC;
    uint32_t textInactive = 0xFF808080;
    uint32_t buttonHover = 0xFF2A2A2A;
    uint32_t buttonPressed = 0xFF252525;
    uint32_t closeHover = 0xFFE81123;
    uint32_t closePressed = 0xFFF1707A;
    uint32_t divider = 0xFF333333;
    uint32_t tabActive = 0xFF2D2D2D;
    uint32_t tabInactive = 0xFF1E1E1E;
    uint32_t tabHover = 0xFF383838;
    uint32_t tabCloseHover = 0xFF383838;
};

struct TabInfo {
    std::wstring title;
    bool isActive = false;
};

class Titlebar {
public:
    Titlebar() = default;
    ~Titlebar() = default;

    void setMetrics(const TitlebarMetrics& metrics) { metrics_ = metrics; }
    void setColors(const TitlebarColors& colors) { colors_ = colors; }

    void setWindowSize(uint32_t width, uint32_t height);
    void setTitle(const std::wstring& title) { title_ = title; }
    void setActive(bool active) { active_ = active; }
    void setMaximized(bool maximized) { maximized_ = maximized; }

    void setTabs(const std::vector<TabInfo>& tabs) { tabs_ = tabs; }
    const std::vector<TabInfo>& getTabs() const { return tabs_; }

    const TitlebarMetrics& getMetrics() const { return metrics_; }
    const TitlebarColors& getColors() const { return colors_; }
    float getHeight() const { return metrics_.height; }
    const std::wstring& getTitle() const { return title_; }
    bool isActive() const { return active_; }
    bool isMaximized() const { return maximized_; }

    TitlebarButton hitTest(int x, int y) const;
    int hitTestTab(int x, int y) const;
    int hitTestTabClose(int x, int y) const;
    LRESULT handleNcHitTest(int x, int y, HWND hwnd) const;

    void onMouseMove(int x, int y);
    void onMouseLeave();
    void onMouseDown(int x, int y);
    void onMouseUp(int x, int y);

    TitlebarButton getHoveredButton() const { return hoveredButton_; }
    TitlebarButton getPressedButton() const { return pressedButton_; }
    int getHoveredTab() const { return hoveredTab_; }
    int getPressedTab() const { return pressedTab_; }
    int getHoveredTabClose() const { return hoveredTabClose_; }
    int getPressedTabClose() const { return pressedTabClose_; }

    struct ButtonRect {
        float x, y, width, height;
    };

    ButtonRect getMinimizeRect() const;
    ButtonRect getMaximizeRect() const;
    ButtonRect getCloseRect() const;
    ButtonRect getIconRect() const;
    ButtonRect getTitleRect() const;
    ButtonRect getDragRect() const;
    ButtonRect getNewTabRect() const;
    ButtonRect getTabRect(size_t index) const;
    ButtonRect getTabCloseRect(size_t index) const;
    ButtonRect getTabAreaRect() const;
    float getTabWidth() const;

private:
    TitlebarMetrics metrics_;
    TitlebarColors colors_;

    uint32_t windowWidth_ = 0;
    uint32_t windowHeight_ = 0;
    std::wstring title_;
    bool active_ = true;
    bool maximized_ = false;

    std::vector<TabInfo> tabs_;
    int hoveredTab_ = -1;
    int pressedTab_ = -1;
    int hoveredTabClose_ = -1;
    int pressedTabClose_ = -1;

    TitlebarButton hoveredButton_ = TitlebarButton::None;
    TitlebarButton pressedButton_ = TitlebarButton::None;

    bool pointInRect(int x, int y, const ButtonRect& rect) const;
};
