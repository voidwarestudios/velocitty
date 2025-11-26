#pragma once

#include "../../framework.h"
#include <unordered_map>
#include <string_view>
#include <iterator>

class TrigramIndex {
public:
    TrigramIndex() {
        postings_.reserve(100000);
    }

    void addFile(uint32_t fileIndex, std::wstring_view name) {
        if (name.size() < 3) {
            shortNames_.push_back(fileIndex);
            return;
        }

        for (size_t i = 0; i + 2 < name.size(); ++i) {
            uint32_t tri = makeTrigram(name[i], name[i + 1], name[i + 2]);
            postings_[tri].push_back(fileIndex);
        }
    }

    std::vector<uint32_t> search(std::wstring_view query) const {
        if (query.empty()) return {};

        if (query.size() < 3) {
            return shortNames_;
        }

        std::vector<uint32_t> result;
        bool first = true;

        for (size_t i = 0; i + 2 < query.size(); ++i) {
            uint32_t tri = makeTrigram(query[i], query[i + 1], query[i + 2]);
            auto it = postings_.find(tri);
            if (it == postings_.end()) return {};

            if (first) {
                result = it->second;
                std::sort(result.begin(), result.end());
                first = false;
            } else {
                std::vector<uint32_t> sorted = it->second;
                std::sort(sorted.begin(), sorted.end());

                std::vector<uint32_t> intersection;
                intersection.reserve(std::min(result.size(), sorted.size()));
                std::set_intersection(
                    result.begin(), result.end(),
                    sorted.begin(), sorted.end(),
                    std::back_inserter(intersection)
                );
                result = std::move(intersection);
            }

            if (result.empty()) return {};
        }

        return result;
    }

    void clear() {
        postings_.clear();
        shortNames_.clear();
    }

    void sortPostings() {
        for (auto& [tri, list] : postings_) {
            std::sort(list.begin(), list.end());
        }
    }

    size_t memoryUsage() const {
        size_t total = postings_.size() * sizeof(uint32_t);
        for (const auto& [tri, list] : postings_) {
            total += list.capacity() * sizeof(uint32_t);
        }
        total += shortNames_.capacity() * sizeof(uint32_t);
        return total;
    }

private:
    static uint32_t makeTrigram(wchar_t a, wchar_t b, wchar_t c) {
        return (static_cast<uint32_t>(towlower(a)) & 0x3FF) |
               ((static_cast<uint32_t>(towlower(b)) & 0x3FF) << 10) |
               ((static_cast<uint32_t>(towlower(c)) & 0x3FF) << 20);
    }

    std::unordered_map<uint32_t, std::vector<uint32_t>> postings_;
    std::vector<uint32_t> shortNames_;
};
