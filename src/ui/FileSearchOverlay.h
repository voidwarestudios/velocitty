#pragma once

#include "../../framework.h"
#include "../search/SearchResult.h"
#include <functional>

class FileSearchOverlay {
public:
    enum class Action {
        None,
        Cd,
        CdParent,
        InsertPath
    };

    struct Rect {
        float x, y, w, h;
    };

    FileSearchOverlay() = default;

    void show() {
        visible_ = true;
        query_.clear();
        results_.clear();
        selectedIndex_ = 0;
        scrollOffset_ = 0;
        action_ = Action::None;
        selectedPath_.clear();
    }

    void hide() {
        visible_ = false;
    }

    bool isVisible() const { return visible_; }

    bool onChar(wchar_t ch) {
        if (!visible_) return false;
        if (ch < 32) return false;

        query_ += ch;
        searchTrigger_ = true;
        return true;
    }

    bool onKeyDown(UINT vk, bool ctrl, bool shift) {
        if (!visible_) return false;

        switch (vk) {
            case VK_ESCAPE:
                hide();
                return true;

            case VK_UP:
                if (selectedIndex_ > 0) {
                    selectedIndex_--;
                    ensureVisible();
                }
                return true;

            case VK_DOWN:
                if (selectedIndex_ + 1 < results_.size()) {
                    selectedIndex_++;
                    ensureVisible();
                }
                return true;

            case VK_PRIOR:
                if (selectedIndex_ >= maxVisible_) {
                    selectedIndex_ -= maxVisible_;
                } else {
                    selectedIndex_ = 0;
                }
                ensureVisible();
                return true;

            case VK_NEXT:
                selectedIndex_ = std::min(
                    selectedIndex_ + maxVisible_,
                    static_cast<int>(results_.size()) - 1
                );
                if (selectedIndex_ < 0) selectedIndex_ = 0;
                ensureVisible();
                return true;

            case VK_RETURN:
                if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(results_.size())) {
                    const auto& r = results_[selectedIndex_];
                    selectedPath_ = r.fullPath;

                    if (r.isDirectory) {
                        action_ = Action::Cd;
                    } else if (shift) {
                        action_ = Action::CdParent;
                    } else {
                        action_ = Action::InsertPath;
                    }
                    hide();
                }
                return true;

            case VK_BACK:
                if (!query_.empty()) {
                    query_.pop_back();
                    searchTrigger_ = true;
                }
                return true;

            case 'A':
                if (ctrl) {
                    query_.clear();
                    searchTrigger_ = true;
                    return true;
                }
                break;
        }

        return false;
    }

    bool onMouseDown(int x, int y) {
        if (!visible_) return false;

        auto panel = getOverlayRect(windowWidth_, windowHeight_);
        if (x < panel.x || x > panel.x + panel.w ||
            y < panel.y || y > panel.y + panel.h) {
            hide();
            return true;
        }

        auto resultsRect = getResultsRect(windowWidth_, windowHeight_);
        if (x >= resultsRect.x && x <= resultsRect.x + resultsRect.w &&
            y >= resultsRect.y && y <= resultsRect.y + resultsRect.h) {

            int clickedIndex = scrollOffset_ +
                static_cast<int>((y - resultsRect.y) / resultHeight_);

            if (clickedIndex >= 0 && clickedIndex < static_cast<int>(results_.size())) {
                selectedIndex_ = clickedIndex;

                const auto& r = results_[selectedIndex_];
                selectedPath_ = r.fullPath;
                action_ = r.isDirectory ? Action::Cd : Action::InsertPath;
                hide();
            }
        }

        return true;
    }

    bool onMouseWheel(int delta) {
        if (!visible_) return false;

        int scroll = delta > 0 ? -3 : 3;
        scrollOffset_ = std::max(0, std::min(
            static_cast<int>(results_.size()) - maxVisible_,
            scrollOffset_ + scroll
        ));

        return true;
    }

    void setWindowSize(float w, float h) {
        windowWidth_ = w;
        windowHeight_ = h;
    }

    void setResults(const std::vector<SearchResult>& results, bool complete) {
        results_ = results;
        if (selectedIndex_ >= static_cast<int>(results_.size())) {
            selectedIndex_ = results_.empty() ? 0 : static_cast<int>(results_.size()) - 1;
        }
        ensureVisible();
    }

    void setIndexProgress(float progress) {
        indexProgress_ = progress;
    }

    bool shouldTriggerSearch() {
        bool trigger = searchTrigger_;
        searchTrigger_ = false;
        return trigger;
    }

    const std::wstring& getQuery() const { return query_; }
    const std::vector<SearchResult>& getResults() const { return results_; }
    int getSelectedIndex() const { return selectedIndex_; }
    int getScrollOffset() const { return scrollOffset_; }
    int getMaxVisible() const { return maxVisible_; }
    bool isIndexing() const { return indexProgress_ < 1.0f; }
    float getIndexProgress() const { return indexProgress_; }

    bool hasAction() const { return action_ != Action::None; }
    Action getAction() const { return action_; }
    const std::wstring& getSelectedPath() const { return selectedPath_; }
    void clearAction() { action_ = Action::None; selectedPath_.clear(); }

    Rect getOverlayRect(float winW, float winH) const {
        float w = std::min(overlayWidth_, winW * 0.8f);
        float maxH = std::min(500.0f, winH * 0.7f);

        int visibleResults = std::min(maxVisible_, static_cast<int>(results_.size()));
        float h = searchBoxHeight_ + visibleResults * resultHeight_ + hintBarHeight_ + padding_ * 2;
        h = std::min(h, maxH);

        float x = (winW - w) / 2;
        float y = 80.0f;

        return { x, y, w, h };
    }

    Rect getSearchBoxRect(float winW, float winH) const {
        auto panel = getOverlayRect(winW, winH);
        return {
            panel.x + padding_,
            panel.y + padding_,
            panel.w - padding_ * 2,
            searchBoxHeight_
        };
    }

    Rect getResultsRect(float winW, float winH) const {
        auto panel = getOverlayRect(winW, winH);
        auto search = getSearchBoxRect(winW, winH);
        return {
            panel.x + padding_,
            search.y + search.h + 8,
            panel.w - padding_ * 2,
            panel.h - searchBoxHeight_ - hintBarHeight_ - padding_ * 2 - 16
        };
    }

    Rect getHintBarRect(float winW, float winH) const {
        auto panel = getOverlayRect(winW, winH);
        return {
            panel.x + padding_,
            panel.y + panel.h - hintBarHeight_ - padding_,
            panel.w - padding_ * 2,
            hintBarHeight_
        };
    }

    static constexpr float overlayWidth_ = 700.0f;
    static constexpr float searchBoxHeight_ = 36.0f;
    static constexpr float resultHeight_ = 26.0f;
    static constexpr float hintBarHeight_ = 24.0f;
    static constexpr float padding_ = 8.0f;
    static constexpr int maxVisible_ = 12;

private:
    void ensureVisible() {
        if (selectedIndex_ < scrollOffset_) {
            scrollOffset_ = selectedIndex_;
        } else if (selectedIndex_ >= scrollOffset_ + maxVisible_) {
            scrollOffset_ = selectedIndex_ - maxVisible_ + 1;
        }
    }

    bool visible_ = false;
    std::wstring query_;
    std::vector<SearchResult> results_;
    int selectedIndex_ = 0;
    int scrollOffset_ = 0;
    float indexProgress_ = 0.0f;
    bool searchTrigger_ = false;

    float windowWidth_ = 0;
    float windowHeight_ = 0;

    Action action_ = Action::None;
    std::wstring selectedPath_;
};
