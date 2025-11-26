#pragma once

#include "../../framework.h"
#include "DiskIndex.h"
#include "IndexBuilder.h"
#include "SearchResult.h"
#include <shared_mutex>
#include <functional>
#include <iterator>

class FileSearchService {
public:
    using ResultCallback = std::function<void(const std::vector<SearchResult>&, bool complete)>;
    using ProgressCallback = std::function<void(float progress, const std::wstring& status)>;

    FileSearchService() = default;
    ~FileSearchService() {
        stopIndexing();
    }

    FileSearchService(const FileSearchService&) = delete;
    FileSearchService& operator=(const FileSearchService&) = delete;

    void startIndexing(ProgressCallback progressCallback = nullptr) {
        if (indexing_) return;

        progressCallback_ = progressCallback;
        cancelIndex_ = false;
        indexThread_ = std::thread([this]() { indexThreadFunc(); });
    }

    void stopIndexing() {
        cancelIndex_ = true;
        cancelSearch_ = true;

        if (indexThread_.joinable()) {
            indexThread_.join();
        }
        if (searchThread_.joinable()) {
            searchThread_.join();
        }
    }

    void search(const std::wstring& query, ResultCallback callback) {
        if (query.empty()) {
            callback({}, true);
            return;
        }

        cancelSearch_ = true;
        if (searchThread_.joinable()) {
            searchThread_.join();
        }

        cancelSearch_ = false;
        uint64_t id = ++searchId_;

        searchThread_ = std::thread([this, query, callback, id]() {
            searchThreadFunc(query, callback, id);
        });
    }

    void cancelSearch() {
        cancelSearch_ = true;
    }

    bool isIndexing() const { return indexing_; }
    bool isIndexReady() const { return indexReady_; }

    size_t getIndexedCount() const {
        std::shared_lock lock(indexMutex_);
        return index_.entryCount();
    }

    float getIndexProgress() const {
        return indexProgress_.load();
    }

    std::wstring getIndexStatus() const {
        std::shared_lock lock(statusMutex_);
        return indexStatus_;
    }

private:
    void indexThreadFunc() {
        indexing_ = true;
        indexProgress_ = 0.0f;

        std::wstring indexPath = DiskIndex::getIndexPath();

        // try to load existing index first (instant availability)
        {
            std::unique_lock lock(indexMutex_);
            if (index_.open(indexPath)) {
                indexReady_ = true;
                setStatus(L"Index loaded, checking for updates...");
            }
        }

        IndexBuilder builder;

        auto progressCb = [this](float progress, const std::wstring& status) {
            indexProgress_ = progress;
            setStatus(status);
            if (progressCallback_) {
                progressCallback_(progress, status);
            }
        };

        IndexBuilder::BuildStats stats;

        if (indexReady_) {
            // do incremental update
            stats = builder.incrementalUpdate(indexPath, cancelIndex_, progressCb);
        } else {
            // full build
            stats = builder.build(indexPath, cancelIndex_, progressCb);
        }

        if (!cancelIndex_) {
            // reload the updated index
            std::unique_lock lock(indexMutex_);
            index_.close();
            if (index_.open(indexPath)) {
                indexReady_ = true;

                std::wstring statusMsg = L"Ready - " + std::to_wstring(index_.entryCount()) + L" files";
                if (stats.wasIncremental) {
                    if (stats.filesAdded > 0 || stats.filesRemoved > 0) {
                        statusMsg += L" (+" + std::to_wstring(stats.filesAdded) +
                                     L"/-" + std::to_wstring(stats.filesRemoved) + L")";
                    }
                }
                setStatus(statusMsg);
            }
        }

        indexProgress_ = 1.0f;
        indexing_ = false;
    }

    void searchThreadFunc(std::wstring query, ResultCallback callback, uint64_t searchId) {
        std::shared_lock lock(indexMutex_);

        if (!index_.isOpen() || query.empty()) {
            callback({}, true);
            return;
        }

        std::vector<SearchResult> results;
        results.reserve(200);

        if (query.length() >= 3) {
            // trigram search
            std::vector<uint32_t> candidates = trigramSearch(query);

            for (uint32_t idx : candidates) {
                if (cancelSearch_ || searchId != searchId_) return;
                if (idx >= index_.entryCount()) continue;

                const auto& entry = index_.entry(idx);
                auto name = index_.getName(idx);

                size_t matchPos = findMatchPosition(name, query);
                if (matchPos == std::wstring::npos) continue;

                SearchResult r;
                r.displayName = std::wstring(name);
                r.fullPath = index_.buildFullPath(idx);
                r.isDirectory = (entry.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                r.score = calculateScore(name, query, matchPos);
                r.matchStart = matchPos;
                r.matchLen = query.length();

                results.push_back(std::move(r));
                if (results.size() >= 200) break;
            }
        } else {
            // short query - check short names first, then linear scan
            auto shortIndices = index_.getShortNameIndices();
            for (uint32_t idx : shortIndices) {
                if (cancelSearch_ || searchId != searchId_) return;
                if (idx >= index_.entryCount()) continue;

                const auto& entry = index_.entry(idx);
                auto name = index_.getName(idx);

                size_t matchPos = findMatchPosition(name, query);
                if (matchPos == std::wstring::npos) continue;

                SearchResult r;
                r.displayName = std::wstring(name);
                r.fullPath = index_.buildFullPath(idx);
                r.isDirectory = (entry.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                r.score = calculateScore(name, query, matchPos);
                r.matchStart = matchPos;
                r.matchLen = query.length();

                results.push_back(std::move(r));
                if (results.size() >= 200) break;
            }

            // linear scan for remaining
            if (results.size() < 200) {
                for (uint32_t idx = 0; idx < index_.entryCount() && results.size() < 200; ++idx) {
                    if (cancelSearch_ || searchId != searchId_) return;

                    const auto& entry = index_.entry(idx);
                    if (entry.fileRef == 0) continue;  // deleted entry

                    auto name = index_.getName(idx);
                    if (name.size() < 3) continue;  // already checked

                    size_t matchPos = findMatchPosition(name, query);
                    if (matchPos == std::wstring::npos) continue;

                    SearchResult r;
                    r.displayName = std::wstring(name);
                    r.fullPath = index_.buildFullPath(idx);
                    r.isDirectory = (entry.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    r.score = calculateScore(name, query, matchPos);
                    r.matchStart = matchPos;
                    r.matchLen = query.length();

                    results.push_back(std::move(r));
                }
            }
        }

        if (cancelSearch_ || searchId != searchId_) return;

        std::sort(results.begin(), results.end());
        if (results.size() > 100) results.resize(100);

        callback(results, true);
    }

    std::vector<uint32_t> trigramSearch(std::wstring_view query) const {
        if (query.size() < 3) return {};

        std::vector<uint32_t> result;
        bool first = true;

        for (size_t i = 0; i + 2 < query.size(); ++i) {
            uint32_t tri = DiskIndex::makeTrigram(query[i], query[i + 1], query[i + 2]);
            auto postings = index_.getPostings(tri);

            if (postings.empty()) return {};

            if (first) {
                result.assign(postings.begin(), postings.end());
                std::sort(result.begin(), result.end());
                first = false;
            } else {
                std::vector<uint32_t> sorted(postings.begin(), postings.end());
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

    void setStatus(const std::wstring& status) {
        std::unique_lock lock(statusMutex_);
        indexStatus_ = status;
    }

    static size_t findMatchPosition(std::wstring_view haystack, std::wstring_view needle) {
        if (needle.empty()) return 0;
        if (haystack.size() < needle.size()) return std::wstring::npos;

        for (size_t i = 0; i <= haystack.size() - needle.size(); ++i) {
            bool match = true;
            for (size_t j = 0; j < needle.size(); ++j) {
                if (towlower(haystack[i + j]) != towlower(needle[j])) {
                    match = false;
                    break;
                }
            }
            if (match) return i;
        }
        return std::wstring::npos;
    }

    static int calculateScore(std::wstring_view name, std::wstring_view query, size_t matchPos) {
        int score = 100;

        if (name.length() == query.length()) {
            score += 50;
        }

        if (matchPos == 0) {
            score += 30;
        }

        int lenDiff = static_cast<int>(name.length()) - static_cast<int>(query.length());
        score -= std::min(lenDiff, 20);

        return score;
    }

    DiskIndex index_;
    mutable std::shared_mutex indexMutex_;
    mutable std::shared_mutex statusMutex_;

    std::thread indexThread_;
    std::thread searchThread_;

    ProgressCallback progressCallback_;
    std::wstring indexStatus_;

    std::atomic<bool> indexing_{false};
    std::atomic<bool> indexReady_{false};
    std::atomic<bool> cancelIndex_{false};
    std::atomic<bool> cancelSearch_{false};
    std::atomic<uint64_t> searchId_{0};
    std::atomic<float> indexProgress_{0.0f};
};
