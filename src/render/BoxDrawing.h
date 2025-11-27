#pragma once

#include <cstdint>
#include <vector>

class BoxDrawing {
public:
    static bool isBoxDrawing(char32_t codepoint);
    static bool isPowerline(char32_t codepoint);
    static bool isBlockElement(char32_t codepoint);

    static std::vector<uint8_t> renderGlyph(char32_t codepoint, uint32_t width, uint32_t height);

private:
    static void drawLine(uint8_t* data, uint32_t width, uint32_t height,
                         int x1, int y1, int x2, int y2, uint8_t alpha = 255);
    static void fillRect(uint8_t* data, uint32_t width, uint32_t height,
                         int x, int y, int w, int h, uint8_t alpha = 255);
    static void drawArc(uint8_t* data, uint32_t width, uint32_t height,
                        int cx, int cy, int radius, int startAngle, int endAngle, uint8_t alpha = 255);

    static void renderBoxDrawing(uint8_t* data, uint32_t width, uint32_t height, char32_t cp);
    static void renderBlockElement(uint8_t* data, uint32_t width, uint32_t height, char32_t cp);
    static void renderPowerline(uint8_t* data, uint32_t width, uint32_t height, char32_t cp);
    static void renderBraille(uint8_t* data, uint32_t width, uint32_t height, char32_t cp);
    static void renderRoundedCorner(uint8_t* data, uint32_t width, uint32_t height, char32_t cp);
};
