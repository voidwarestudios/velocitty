#include "Config.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <ShlObj.h>

Config& Config::instance() {
    static Config instance;
    return instance;
}

std::wstring Config::getConfigPath() const {
    if (!configPath_.empty()) {
        return configPath_;
    }

    try {
        wchar_t* appDataPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appDataPath))) {
            std::wstring path = appDataPath;
            CoTaskMemFree(appDataPath);
            path += L"\\Velocitty";
            std::error_code ec;
            std::filesystem::create_directories(path, ec);
            return path + L"\\config.json";
        }
    } catch (...) {
    }

    return L"config.json";
}

bool Config::load(const std::wstring& path) {
    initDefaults();

    std::wstring configPath = path.empty() ? getConfigPath() : path;
    configPath_ = configPath;

    std::ifstream file(configPath);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    auto findValue = [&content](const std::string& key) -> std::string {
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";

        pos = content.find(":", pos);
        if (pos == std::string::npos) return "";
        pos++;

        while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t')) pos++;

        if (content[pos] == '"') {
            pos++;
            size_t end = content.find("\"", pos);
            if (end != std::string::npos) {
                return content.substr(pos, end - pos);
            }
        } else {
            size_t end = pos;
            while (end < content.size() && content[end] != ',' && content[end] != '}' && content[end] != '\n') {
                end++;
            }
            std::string value = content.substr(pos, end - pos);
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
                value.pop_back();
            }
            return value;
        }
        return "";
    };

    auto parseColor = [](const std::string& value) -> uint32_t {
        if (value.empty()) return 0;
        if (value[0] == '#') {
            return 0xFF000000 | static_cast<uint32_t>(std::stoul(value.substr(1), nullptr, 16));
        }
        return static_cast<uint32_t>(std::stoul(value));
    };

    std::string fontFamily = findValue("fontFamily");
    if (!fontFamily.empty()) {
        font_.family = std::wstring(fontFamily.begin(), fontFamily.end());
    }

    std::string fontSize = findValue("fontSize");
    if (!fontSize.empty()) {
        font_.size = std::stof(fontSize);
    }

    std::string ligatures = findValue("ligatures");
    if (!ligatures.empty()) {
        font_.ligatures = (ligatures == "true");
    }

    std::string fg = findValue("foreground");
    if (!fg.empty()) colorScheme_.foreground = parseColor(fg);

    std::string bg = findValue("background");
    if (!bg.empty()) colorScheme_.background = parseColor(bg);

    std::string cursor = findValue("cursorColor");
    if (!cursor.empty()) colorScheme_.cursor = parseColor(cursor);

    std::string vsync = findValue("vsync");
    if (!vsync.empty()) render_.vsync = (vsync == "true");

    std::string dirtyRect = findValue("dirtyRectOptimization");
    if (!dirtyRect.empty()) render_.dirtyRectOptimization = (dirtyRect == "true");

    std::string opacity = findValue("opacity");
    if (!opacity.empty()) render_.opacity = std::stof(opacity);

    std::string shell = findValue("shell");
    if (!shell.empty()) {
        terminal_.shell = std::wstring(shell.begin(), shell.end());
    }

    std::string scrollback = findValue("scrollbackLines");
    if (!scrollback.empty()) {
        terminal_.scrollbackLines = static_cast<uint16_t>(std::stoi(scrollback));
    }

    std::string cursorBlink = findValue("cursorBlink");
    if (!cursorBlink.empty()) terminal_.cursorBlink = (cursorBlink == "true");

    std::string cursorStyle = findValue("cursorStyle");
    if (!cursorStyle.empty()) terminal_.cursorStyle = cursorStyle;

    std::string width = findValue("windowWidth");
    if (!width.empty()) window_.width = static_cast<uint32_t>(std::stoi(width));

    std::string height = findValue("windowHeight");
    if (!height.empty()) window_.height = static_cast<uint32_t>(std::stoi(height));

    return true;
}

bool Config::save(const std::wstring& path) {
    std::wstring configPath = path.empty() ? getConfigPath() : path;

    auto colorToHex = [](uint32_t color) -> std::string {
        char buf[16];
        snprintf(buf, sizeof(buf), "#%06X", color & 0xFFFFFF);
        return buf;
    };

    std::ofstream file(configPath);
    if (!file.is_open()) {
        return false;
    }

    file << "{\n";
    file << "  \"font\": {\n";
    file << "    \"fontFamily\": \"" << std::string(font_.family.begin(), font_.family.end()) << "\",\n";
    file << "    \"fontSize\": " << font_.size << ",\n";
    file << "    \"ligatures\": " << (font_.ligatures ? "true" : "false") << "\n";
    file << "  },\n";

    file << "  \"colors\": {\n";
    file << "    \"foreground\": \"" << colorToHex(colorScheme_.foreground) << "\",\n";
    file << "    \"background\": \"" << colorToHex(colorScheme_.background) << "\",\n";
    file << "    \"cursorColor\": \"" << colorToHex(colorScheme_.cursor) << "\",\n";
    file << "    \"selection\": \"" << colorToHex(colorScheme_.selection) << "\"\n";
    file << "  },\n";

    file << "  \"terminal\": {\n";
    file << "    \"shell\": \"" << std::string(terminal_.shell.begin(), terminal_.shell.end()) << "\",\n";
    file << "    \"scrollbackLines\": " << terminal_.scrollbackLines << ",\n";
    file << "    \"cursorBlink\": " << (terminal_.cursorBlink ? "true" : "false") << ",\n";
    file << "    \"cursorStyle\": \"" << terminal_.cursorStyle << "\"\n";
    file << "  },\n";

    file << "  \"render\": {\n";
    file << "    \"vsync\": " << (render_.vsync ? "true" : "false") << ",\n";
    file << "    \"dirtyRectOptimization\": " << (render_.dirtyRectOptimization ? "true" : "false") << ",\n";
    file << "    \"opacity\": " << render_.opacity << "\n";
    file << "  },\n";

    file << "  \"window\": {\n";
    file << "    \"windowWidth\": " << window_.width << ",\n";
    file << "    \"windowHeight\": " << window_.height << "\n";
    file << "  },\n";

    file << "  \"keyBindings\": [\n";
    for (size_t i = 0; i < keyBindings_.size(); ++i) {
        const auto& kb = keyBindings_[i];
        file << "    {\"action\": \"" << kb.action << "\", \"key\": \"" << kb.key << "\"";
        if (kb.ctrl) file << ", \"ctrl\": true";
        if (kb.alt) file << ", \"alt\": true";
        if (kb.shift) file << ", \"shift\": true";
        file << "}";
        if (i < keyBindings_.size() - 1) file << ",";
        file << "\n";
    }
    file << "  ]\n";

    file << "}\n";

    return true;
}

void Config::initDefaults() {
    initDefaultColorSchemes();
    initDefaultKeyBindings();

    colorScheme_ = availableSchemes_[0];

    font_.family = L"Cascadia Mono";
    font_.size = 14.0f;
    font_.ligatures = true;

    window_.width = 1024;
    window_.height = 768;

    terminal_.scrollbackLines = 10000;
    terminal_.cursorBlink = true;
    terminal_.cursorStyle = "block";

    render_.vsync = true;
    render_.dirtyRectOptimization = true;
    render_.opacity = 1.0f;
}

void Config::initDefaultKeyBindings() {
    keyBindings_.clear();

    keyBindings_.push_back({"copy", "C", true, false, false});
    keyBindings_.push_back({"paste", "V", true, false, false});
    keyBindings_.push_back({"newTab", "T", true, false, false});
    keyBindings_.push_back({"closeTab", "W", true, false, false});
    keyBindings_.push_back({"nextTab", "Tab", true, false, false});
    keyBindings_.push_back({"prevTab", "Tab", true, false, true});
    keyBindings_.push_back({"splitHorizontal", "D", true, true, false});
    keyBindings_.push_back({"splitVertical", "D", true, false, true});
    keyBindings_.push_back({"closePane", "W", true, true, false});
    keyBindings_.push_back({"zoomIn", "=", true, false, false});
    keyBindings_.push_back({"zoomOut", "-", true, false, false});
    keyBindings_.push_back({"resetZoom", "0", true, false, false});
    keyBindings_.push_back({"scrollUp", "Up", false, false, true});
    keyBindings_.push_back({"scrollDown", "Down", false, false, true});
    keyBindings_.push_back({"scrollPageUp", "PageUp", false, false, true});
    keyBindings_.push_back({"scrollPageDown", "PageDown", false, false, true});
    keyBindings_.push_back({"find", "F", true, false, false});
    keyBindings_.push_back({"toggleFullscreen", "F11", false, false, false});
}

void Config::initDefaultColorSchemes() {
    availableSchemes_.clear();

    ColorScheme dark;
    dark.name = "Velocitty Dark";
    dark.foreground = 0xFFCCCCCC;
    dark.background = 0xFF1E1E1E;
    dark.cursor = 0xFFFFFFFF;
    dark.selection = 0x40FFFFFF;
    availableSchemes_.push_back(dark);

    ColorScheme campbell;
    campbell.name = "Campbell";
    campbell.foreground = 0xFFCCCCCC;
    campbell.background = 0xFF0C0C0C;
    campbell.cursor = 0xFFFFFFFF;
    campbell.selection = 0x40FFFFFF;
    availableSchemes_.push_back(campbell);

    ColorScheme oneDark;
    oneDark.name = "One Dark";
    oneDark.foreground = 0xFFABB2BF;
    oneDark.background = 0xFF282C34;
    oneDark.cursor = 0xFF528BFF;
    oneDark.selection = 0x403E4451;
    oneDark.ansiColors = {
        0xFF282C34, 0xFFE06C75, 0xFF98C379, 0xFFE5C07B,
        0xFF61AFEF, 0xFFC678DD, 0xFF56B6C2, 0xFFABB2BF,
        0xFF5C6370, 0xFFE06C75, 0xFF98C379, 0xFFE5C07B,
        0xFF61AFEF, 0xFFC678DD, 0xFF56B6C2, 0xFFFFFFFF
    };
    availableSchemes_.push_back(oneDark);

    ColorScheme dracula;
    dracula.name = "Dracula";
    dracula.foreground = 0xFFF8F8F2;
    dracula.background = 0xFF282A36;
    dracula.cursor = 0xFFF8F8F2;
    dracula.selection = 0x4044475A;
    dracula.ansiColors = {
        0xFF21222C, 0xFFFF5555, 0xFF50FA7B, 0xFFF1FA8C,
        0xFFBD93F9, 0xFFFF79C6, 0xFF8BE9FD, 0xFFF8F8F2,
        0xFF6272A4, 0xFFFF6E6E, 0xFF69FF94, 0xFFFFFFA5,
        0xFFD6ACFF, 0xFFFF92DF, 0xFFA4FFFF, 0xFFFFFFFF
    };
    availableSchemes_.push_back(dracula);

    ColorScheme solarizedDark;
    solarizedDark.name = "Solarized Dark";
    solarizedDark.foreground = 0xFF839496;
    solarizedDark.background = 0xFF002B36;
    solarizedDark.cursor = 0xFF839496;
    solarizedDark.selection = 0x40073642;
    solarizedDark.ansiColors = {
        0xFF073642, 0xFFDC322F, 0xFF859900, 0xFFB58900,
        0xFF268BD2, 0xFFD33682, 0xFF2AA198, 0xFFEEE8D5,
        0xFF002B36, 0xFFCB4B16, 0xFF586E75, 0xFF657B83,
        0xFF839496, 0xFF6C71C4, 0xFF93A1A1, 0xFFFDF6E3
    };
    availableSchemes_.push_back(solarizedDark);
}

void Config::setColorScheme(const std::string& name) {
    for (const auto& scheme : availableSchemes_) {
        if (scheme.name == name) {
            colorScheme_ = scheme;
            return;
        }
    }
}
