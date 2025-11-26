#pragma once

#include "../../framework.h"

struct SearchResult {
    std::wstring fullPath;
    std::wstring displayName;
    bool isDirectory = false;
    int score = 0;
    size_t matchStart = 0;
    size_t matchLen = 0;

    bool operator<(const SearchResult& other) const {
        return score > other.score;
    }
};
