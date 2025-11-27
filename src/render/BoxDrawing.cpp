#include "BoxDrawing.h"
#include <cmath>
#include <algorithm>

bool BoxDrawing::isBoxDrawing(char32_t cp) {
    return (cp >= 0x2500 && cp <= 0x257F);
}

bool BoxDrawing::isPowerline(char32_t cp) {
    return (cp >= 0xE0A0 && cp <= 0xE0D4) || (cp >= 0xE0B0 && cp <= 0xE0B7);
}

bool BoxDrawing::isBlockElement(char32_t cp) {
    return (cp >= 0x2580 && cp <= 0x259F);
}

std::vector<uint8_t> BoxDrawing::renderGlyph(char32_t codepoint, uint32_t width, uint32_t height) {
    std::vector<uint8_t> data(width * height, 0);

    if (isBoxDrawing(codepoint)) {
        renderBoxDrawing(data.data(), width, height, codepoint);
    } else if (isBlockElement(codepoint)) {
        renderBlockElement(data.data(), width, height, codepoint);
    } else if (isPowerline(codepoint)) {
        renderPowerline(data.data(), width, height, codepoint);
    } else if (codepoint >= 0x2800 && codepoint <= 0x28FF) {
        renderBraille(data.data(), width, height, codepoint);
    }

    return data;
}

void BoxDrawing::drawLine(uint8_t* data, uint32_t width, uint32_t height,
                           int x1, int y1, int x2, int y2, uint8_t alpha) {
    int dx = std::abs(x2 - x1);
    int dy = std::abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;

    while (true) {
        if (x1 >= 0 && x1 < (int)width && y1 >= 0 && y1 < (int)height) {
            data[y1 * width + x1] = alpha;
        }
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx) { err += dx; y1 += sy; }
    }
}

void BoxDrawing::fillRect(uint8_t* data, uint32_t width, uint32_t height,
                           int x, int y, int w, int h, uint8_t alpha) {
    for (int py = y; py < y + h; ++py) {
        for (int px = x; px < x + w; ++px) {
            if (px >= 0 && px < (int)width && py >= 0 && py < (int)height) {
                data[py * width + px] = alpha;
            }
        }
    }
}

void BoxDrawing::drawArc(uint8_t* data, uint32_t width, uint32_t height,
                          int cx, int cy, int radius, int startAngle, int endAngle, uint8_t alpha) {
    for (int angle = startAngle; angle <= endAngle; ++angle) {
        float rad = angle * 3.14159f / 180.0f;
        int x = cx + static_cast<int>(radius * std::cos(rad));
        int y = cy + static_cast<int>(radius * std::sin(rad));
        if (x >= 0 && x < (int)width && y >= 0 && y < (int)height) {
            data[y * width + x] = alpha;
        }
    }
}

void BoxDrawing::renderRoundedCorner(uint8_t* data, uint32_t width, uint32_t height, char32_t cp) {
    int midX = width / 2;
    int midY = height / 2;
    int lineWidth = std::max(1, (int)(width / 8));

    float rx = (float)midX;
    float ry = (float)midY;

    float cx, cy;
    int startAngle, endAngle;

    switch (cp) {
        case 0x256D:
            cx = (float)width;
            cy = (float)height;
            startAngle = 180;
            endAngle = 270;
            break;
        case 0x256E:
            cx = 0.0f;
            cy = (float)height;
            startAngle = 270;
            endAngle = 360;
            break;
        case 0x256F:
            cx = 0.0f;
            cy = 0.0f;
            startAngle = 0;
            endAngle = 90;
            break;
        case 0x2570:
            cx = (float)width;
            cy = 0.0f;
            startAngle = 90;
            endAngle = 180;
            break;
        default:
            return;
    }

    for (int t = -lineWidth / 2; t <= lineWidth / 2; ++t) {
        float radiusX = rx + t;
        float radiusY = ry + t;
        if (radiusX <= 0 || radiusY <= 0) continue;

        for (int angle = startAngle; angle <= endAngle; ++angle) {
            float rad = angle * 3.14159f / 180.0f;
            int x = (int)(cx + radiusX * std::cos(rad));
            int y = (int)(cy + radiusY * std::sin(rad));
            if (x >= 0 && x < (int)width && y >= 0 && y < (int)height) {
                data[y * width + x] = 255;
            }
        }
    }
}

void BoxDrawing::renderBoxDrawing(uint8_t* data, uint32_t width, uint32_t height, char32_t cp) {
    int midX = width / 2;
    int midY = height / 2;
    int lineWidth = std::max(1, (int)(width / 8));
    int heavyWidth = std::max(2, (int)(width / 4));

    bool left = false, right = false, up = false, down = false;
    bool leftHeavy = false, rightHeavy = false, upHeavy = false, downHeavy = false;
    bool doubleH = false, doubleV = false;

    switch (cp) {
        case 0x2500: left = right = true; break;
        case 0x2501: left = right = leftHeavy = rightHeavy = true; break;
        case 0x2502: up = down = true; break;
        case 0x2503: up = down = upHeavy = downHeavy = true; break;
        case 0x250C: right = down = true; break;
        case 0x250D: right = down = rightHeavy = true; break;
        case 0x250E: right = down = downHeavy = true; break;
        case 0x250F: right = down = rightHeavy = downHeavy = true; break;
        case 0x2510: left = down = true; break;
        case 0x2511: left = down = leftHeavy = true; break;
        case 0x2512: left = down = downHeavy = true; break;
        case 0x2513: left = down = leftHeavy = downHeavy = true; break;
        case 0x2514: right = up = true; break;
        case 0x2515: right = up = rightHeavy = true; break;
        case 0x2516: right = up = upHeavy = true; break;
        case 0x2517: right = up = rightHeavy = upHeavy = true; break;
        case 0x2518: left = up = true; break;
        case 0x2519: left = up = leftHeavy = true; break;
        case 0x251A: left = up = upHeavy = true; break;
        case 0x251B: left = up = leftHeavy = upHeavy = true; break;
        case 0x251C: right = up = down = true; break;
        case 0x2524: left = up = down = true; break;
        case 0x252C: left = right = down = true; break;
        case 0x2534: left = right = up = true; break;
        case 0x253C: left = right = up = down = true; break;
        case 0x2550: doubleH = true; left = right = true; break;
        case 0x2551: doubleV = true; up = down = true; break;
        case 0x2554: doubleH = doubleV = true; right = down = true; break;
        case 0x2557: doubleH = doubleV = true; left = down = true; break;
        case 0x255A: doubleH = doubleV = true; right = up = true; break;
        case 0x255D: doubleH = doubleV = true; left = up = true; break;
        case 0x2560: doubleV = true; right = up = down = true; break;
        case 0x2563: doubleV = true; left = up = down = true; break;
        case 0x2566: doubleH = true; left = right = down = true; break;
        case 0x2569: doubleH = true; left = right = up = true; break;
        case 0x256C: doubleH = doubleV = true; left = right = up = down = true; break;
        case 0x2574: left = true; break;
        case 0x2575: up = true; break;
        case 0x2576: right = true; break;
        case 0x2577: down = true; break;
        case 0x2578: left = leftHeavy = true; break;
        case 0x2579: up = upHeavy = true; break;
        case 0x257A: right = rightHeavy = true; break;
        case 0x257B: down = downHeavy = true; break;
        case 0x257C: left = right = rightHeavy = true; break;
        case 0x257D: up = down = downHeavy = true; break;
        case 0x257E: left = right = leftHeavy = true; break;
        case 0x257F: up = down = upHeavy = true; break;

        // rounded corners - render with arcs and return early
        case 0x256D: // ╭ top-left rounded
        case 0x256E: // ╮ top-right rounded
        case 0x256F: // ╯ bottom-right rounded
        case 0x2570: // ╰ bottom-left rounded
            renderRoundedCorner(data, width, height, cp);
            return;

        default:
            left = right = true;
            break;
    }

    int lw = lineWidth;
    int hw = heavyWidth;

    if (left) {
        int w = leftHeavy ? hw : lw;
        fillRect(data, width, height, 0, midY - w/2, midX + 1, w);
    }
    if (right) {
        int w = rightHeavy ? hw : lw;
        fillRect(data, width, height, midX, midY - w/2, width - midX, w);
    }
    if (up) {
        int w = upHeavy ? hw : lw;
        fillRect(data, width, height, midX - w/2, 0, w, midY + 1);
    }
    if (down) {
        int w = downHeavy ? hw : lw;
        fillRect(data, width, height, midX - w/2, midY, w, height - midY);
    }

    if (doubleH && (left || right)) {
        int gap = lineWidth;
        if (left) {
            fillRect(data, width, height, 0, midY - gap - lw/2, midX + 1, lw);
            fillRect(data, width, height, 0, midY + gap - lw/2, midX + 1, lw);
        }
        if (right) {
            fillRect(data, width, height, midX, midY - gap - lw/2, width - midX, lw);
            fillRect(data, width, height, midX, midY + gap - lw/2, width - midX, lw);
        }
    }
    if (doubleV && (up || down)) {
        int gap = lineWidth;
        if (up) {
            fillRect(data, width, height, midX - gap - lw/2, 0, lw, midY + 1);
            fillRect(data, width, height, midX + gap - lw/2, 0, lw, midY + 1);
        }
        if (down) {
            fillRect(data, width, height, midX - gap - lw/2, midY, lw, height - midY);
            fillRect(data, width, height, midX + gap - lw/2, midY, lw, height - midY);
        }
    }
}

void BoxDrawing::renderBlockElement(uint8_t* data, uint32_t width, uint32_t height, char32_t cp) {
    switch (cp) {
        case 0x2580:
            fillRect(data, width, height, 0, 0, width, height / 2);
            break;
        case 0x2581:
            fillRect(data, width, height, 0, height * 7 / 8, width, height / 8);
            break;
        case 0x2582:
            fillRect(data, width, height, 0, height * 3 / 4, width, height / 4);
            break;
        case 0x2583:
            fillRect(data, width, height, 0, height * 5 / 8, width, height * 3 / 8);
            break;
        case 0x2584:
            fillRect(data, width, height, 0, height / 2, width, height / 2);
            break;
        case 0x2585:
            fillRect(data, width, height, 0, height * 3 / 8, width, height * 5 / 8);
            break;
        case 0x2586:
            fillRect(data, width, height, 0, height / 4, width, height * 3 / 4);
            break;
        case 0x2587:
            fillRect(data, width, height, 0, height / 8, width, height * 7 / 8);
            break;
        case 0x2588:
            fillRect(data, width, height, 0, 0, width, height);
            break;
        case 0x2589:
            fillRect(data, width, height, 0, 0, width * 7 / 8, height);
            break;
        case 0x258A:
            fillRect(data, width, height, 0, 0, width * 3 / 4, height);
            break;
        case 0x258B:
            fillRect(data, width, height, 0, 0, width * 5 / 8, height);
            break;
        case 0x258C:
            fillRect(data, width, height, 0, 0, width / 2, height);
            break;
        case 0x258D:
            fillRect(data, width, height, 0, 0, width * 3 / 8, height);
            break;
        case 0x258E:
            fillRect(data, width, height, 0, 0, width / 4, height);
            break;
        case 0x258F:
            fillRect(data, width, height, 0, 0, width / 8, height);
            break;
        case 0x2590:
            fillRect(data, width, height, width / 2, 0, width / 2, height);
            break;
        case 0x2591:
            for (uint32_t y = 0; y < height; y += 2) {
                for (uint32_t x = (y/2) % 2; x < width; x += 2) {
                    data[y * width + x] = 255;
                }
            }
            break;
        case 0x2592:
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = y % 2; x < width; x += 2) {
                    data[y * width + x] = 255;
                }
            }
            break;
        case 0x2593:
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    if ((x + y) % 2 == 0 || (x + y) % 4 == 0) {
                        data[y * width + x] = 255;
                    }
                }
            }
            break;
        case 0x2594:
            fillRect(data, width, height, 0, 0, width, height / 8);
            break;
        case 0x2595:
            fillRect(data, width, height, width * 7 / 8, 0, width / 8, height);
            break;
        case 0x2596:
            fillRect(data, width, height, 0, height / 2, width / 2, height / 2);
            break;
        case 0x2597:
            fillRect(data, width, height, width / 2, height / 2, width / 2, height / 2);
            break;
        case 0x2598:
            fillRect(data, width, height, 0, 0, width / 2, height / 2);
            break;
        case 0x2599:
            fillRect(data, width, height, 0, 0, width / 2, height);
            fillRect(data, width, height, 0, height / 2, width, height / 2);
            break;
        case 0x259A:
            fillRect(data, width, height, 0, 0, width / 2, height / 2);
            fillRect(data, width, height, width / 2, height / 2, width / 2, height / 2);
            break;
        case 0x259B:
            fillRect(data, width, height, 0, 0, width, height / 2);
            fillRect(data, width, height, 0, 0, width / 2, height);
            break;
        case 0x259C:
            fillRect(data, width, height, 0, 0, width, height / 2);
            fillRect(data, width, height, width / 2, 0, width / 2, height);
            break;
        case 0x259D:
            fillRect(data, width, height, width / 2, 0, width / 2, height / 2);
            break;
        case 0x259E:
            fillRect(data, width, height, width / 2, 0, width / 2, height / 2);
            fillRect(data, width, height, 0, height / 2, width / 2, height / 2);
            break;
        case 0x259F:
            fillRect(data, width, height, width / 2, 0, width / 2, height);
            fillRect(data, width, height, 0, height / 2, width, height / 2);
            break;
    }
}

void BoxDrawing::renderPowerline(uint8_t* data, uint32_t width, uint32_t height, char32_t cp) {
    switch (cp) {
        case 0xE0B0:
            for (uint32_t y = 0; y < height; ++y) {
                float progress = (float)y / height;
                int xLimit = (int)(progress * width);
                for (int x = 0; x < xLimit; ++x) {
                    data[y * width + x] = 255;
                }
            }
            for (uint32_t y = height / 2; y < height; ++y) {
                float progress = (float)(height - y - 1) / (height / 2);
                int xLimit = (int)(progress * width);
                for (int x = 0; x < xLimit; ++x) {
                    data[y * width + x] = 255;
                }
            }
            break;
        case 0xE0B1:
            for (uint32_t y = 0; y < height / 2; ++y) {
                float progress = (float)y / (height / 2);
                int x = (int)(progress * width);
                if (x >= 0 && x < (int)width) {
                    for (int lw = 0; lw < 2; ++lw) {
                        if (x + lw < (int)width) {
                            data[y * width + x + lw] = 255;
                        }
                    }
                }
            }
            for (uint32_t y = height / 2; y < height; ++y) {
                float progress = (float)(height - y - 1) / (height / 2);
                int x = (int)(progress * width);
                if (x >= 0 && x < (int)width) {
                    for (int lw = 0; lw < 2; ++lw) {
                        if (x + lw < (int)width) {
                            data[y * width + x + lw] = 255;
                        }
                    }
                }
            }
            break;
        case 0xE0B2:
            for (uint32_t y = 0; y < height / 2; ++y) {
                float progress = (float)y / (height / 2);
                int xStart = width - (int)(progress * width);
                for (uint32_t x = xStart; x < width; ++x) {
                    data[y * width + x] = 255;
                }
            }
            for (uint32_t y = height / 2; y < height; ++y) {
                float progress = (float)(height - y - 1) / (height / 2);
                int xStart = width - (int)(progress * width);
                for (uint32_t x = xStart; x < width; ++x) {
                    data[y * width + x] = 255;
                }
            }
            break;
        case 0xE0B3:
            for (uint32_t y = 0; y < height / 2; ++y) {
                float progress = (float)y / (height / 2);
                int x = width - 1 - (int)(progress * width);
                if (x >= 0 && x < (int)width) {
                    for (int lw = 0; lw < 2; ++lw) {
                        if (x - lw >= 0) {
                            data[y * width + x - lw] = 255;
                        }
                    }
                }
            }
            for (uint32_t y = height / 2; y < height; ++y) {
                float progress = (float)(height - y - 1) / (height / 2);
                int x = width - 1 - (int)(progress * width);
                if (x >= 0 && x < (int)width) {
                    for (int lw = 0; lw < 2; ++lw) {
                        if (x - lw >= 0) {
                            data[y * width + x - lw] = 255;
                        }
                    }
                }
            }
            break;
        case 0xE0B4:
            for (uint32_t y = 0; y < height; ++y) {
                float progress = (float)y / height;
                if (y < height / 2) {
                    progress = (float)y / (height / 2);
                } else {
                    progress = (float)(height - y - 1) / (height / 2);
                }
                int xLimit = width / 2 + (int)(progress * width / 2);
                for (int x = 0; x < xLimit; ++x) {
                    data[y * width + x] = 255;
                }
            }
            break;
        case 0xE0B6:
            for (uint32_t y = 0; y < height; ++y) {
                float progress;
                if (y < height / 2) {
                    progress = (float)y / (height / 2);
                } else {
                    progress = (float)(height - y - 1) / (height / 2);
                }
                int xStart = width / 2 - (int)(progress * width / 2);
                for (uint32_t x = xStart; x < width; ++x) {
                    data[y * width + x] = 255;
                }
            }
            break;
        case 0xE0A0:
            {
                int midX = width / 2;
                int midY = height / 2;
                for (uint32_t y = 0; y < height; ++y) {
                    for (uint32_t x = 0; x < width; ++x) {
                        int dx = x - midX;
                        int dy = y - midY;
                        if (dy < 0 && std::abs(dx) <= -dy / 2) {
                            data[y * width + x] = 255;
                        }
                        if (x >= width * 3 / 8 && x <= width * 5 / 8 && y >= height / 4) {
                            data[y * width + x] = 255;
                        }
                    }
                }
            }
            break;
        case 0xE0A2:
            {
                int midX = width / 2;
                int midY = height / 3;
                int radius = width / 3;
                for (uint32_t y = 0; y < height; ++y) {
                    for (uint32_t x = 0; x < width; ++x) {
                        int dx = x - midX;
                        int dy = y - midY;
                        if (dx * dx + dy * dy <= radius * radius) {
                            data[y * width + x] = 255;
                        }
                    }
                }
                fillRect(data, width, height, midX - 1, midY, 2, height - midY);
            }
            break;
    }
}

void BoxDrawing::renderBraille(uint8_t* data, uint32_t width, uint32_t height, char32_t cp) {
    uint8_t dots = cp - 0x2800;

    int dotW = width / 3;
    int dotH = height / 5;
    int offsetX = (width - 2 * dotW) / 2;
    int offsetY = (height - 4 * dotH) / 2;

    int positions[8][2] = {
        {0, 0}, {0, 1}, {0, 2}, {0, 3},
        {1, 0}, {1, 1}, {1, 2}, {1, 3}
    };

    int bitOrder[8] = {0, 1, 2, 6, 3, 4, 5, 7};

    for (int i = 0; i < 8; ++i) {
        if (dots & (1 << bitOrder[i])) {
            int col = positions[i][0];
            int row = positions[i][1];
            int cx = offsetX + col * dotW + dotW / 2;
            int cy = offsetY + row * dotH + dotH / 2;
            int r = std::min(dotW, dotH) / 3;

            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (dx * dx + dy * dy <= r * r) {
                        int px = cx + dx;
                        int py = cy + dy;
                        if (px >= 0 && px < (int)width && py >= 0 && py < (int)height) {
                            data[py * width + px] = 255;
                        }
                    }
                }
            }
        }
    }
}
