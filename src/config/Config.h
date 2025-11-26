#pragma once

#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <cstdint>

struct ColorScheme {
    std::string name;
    uint32_t foreground = 0xFFCCCCCC;
    uint32_t background = 0xFF1E1E1E;
    uint32_t cursor = 0xFFFFFFFF;
    uint32_t selection = 0x40FFFFFF;
    std::array<uint32_t, 16> ansiColors = {
        0xFF000000, 0xFFCD0000, 0xFF00CD00, 0xFFCDCD00,
        0xFF0000EE, 0xFFCD00CD, 0xFF00CDCD, 0xFFE5E5E5,
        0xFF7F7F7F, 0xFFFF0000, 0xFF00FF00, 0xFFFFFF00,
        0xFF5C5CFF, 0xFFFF00FF, 0xFF00FFFF, 0xFFFFFFFF
    };
};

struct KeyBinding {
    std::string action;
    std::string key;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
};

struct FontConfig {
    std::wstring family = L"Cascadia Mono";
    float size = 14.0f;
    bool ligatures = true;
    bool bold = true;
    bool italic = true;
};

struct WindowConfig {
    uint32_t width = 1024;
    uint32_t height = 768;
    bool maximized = false;
    int x = -1;
    int y = -1;
};

struct TerminalConfig {
    std::wstring shell = L"";
    std::wstring startingDirectory = L"";
    uint16_t scrollbackLines = 10000;
    bool cursorBlink = true;
    std::string cursorStyle = "block";
    float cursorBlinkRate = 500.0f;
};

struct RenderConfig {
    bool vsync = true;
    int targetFps = 60;
    bool dirtyRectOptimization = true;
    float opacity = 1.0f;
};

struct TitlebarConfig {
    bool customTitlebar = true;
    float height = 32.0f;
    float buttonWidth = 46.0f;
    uint32_t background = 0xFF1E1E1E;
    uint32_t backgroundInactive = 0xFF2D2D2D;
    uint32_t text = 0xFFCCCCCC;
    uint32_t textInactive = 0xFF808080;
    uint32_t buttonHover = 0xFF2A2A2A;
    uint32_t buttonPressed = 0xFF252525;
    uint32_t closeHover = 0xFFE81123;
    bool showIcon = true;
};

class Config {
public:
    static Config& instance();

    bool load(const std::wstring& path = L"");
    bool save(const std::wstring& path = L"");

    std::wstring getConfigPath() const;

    ColorScheme& getColorScheme() { return colorScheme_; }
    const ColorScheme& getColorScheme() const { return colorScheme_; }

    FontConfig& getFont() { return font_; }
    const FontConfig& getFont() const { return font_; }

    WindowConfig& getWindow() { return window_; }
    const WindowConfig& getWindow() const { return window_; }

    TerminalConfig& getTerminal() { return terminal_; }
    const TerminalConfig& getTerminal() const { return terminal_; }

    RenderConfig& getRender() { return render_; }
    const RenderConfig& getRender() const { return render_; }

    TitlebarConfig& getTitlebar() { return titlebar_; }
    const TitlebarConfig& getTitlebar() const { return titlebar_; }

    std::vector<KeyBinding>& getKeyBindings() { return keyBindings_; }
    const std::vector<KeyBinding>& getKeyBindings() const { return keyBindings_; }

    void setColorScheme(const std::string& name);
    const std::vector<ColorScheme>& getAvailableColorSchemes() const { return availableSchemes_; }

private:
    Config() = default;
    void initDefaults();
    void initDefaultKeyBindings();
    void initDefaultColorSchemes();

    ColorScheme colorScheme_;
    FontConfig font_;
    WindowConfig window_;
    TerminalConfig terminal_;
    RenderConfig render_;
    TitlebarConfig titlebar_;
    std::vector<KeyBinding> keyBindings_;
    std::vector<ColorScheme> availableSchemes_;

    std::wstring configPath_;
};
