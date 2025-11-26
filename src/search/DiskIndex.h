#pragma once

#include "../../framework.h"
#include <span>
#include <string_view>
#include <ShlObj.h>

#pragma pack(push, 1)
struct DiskIndexHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t entryCount;
    uint32_t stringPoolSize;
    uint32_t trigramCount;
    uint32_t postingDataSize;
    uint64_t buildTimestamp;
    uint32_t reserved[4];

    static constexpr uint32_t MAGIC = 0x56454C49;  // "VELI"
    static constexpr uint32_t VERSION = 2;  // v2: multi-drive composite keys
};

struct DiskFileEntry {
    uint64_t fileRef;
    uint64_t parentRef;
    uint32_t nameOffset;
    uint16_t nameLength;
    uint8_t attributes;
    uint8_t driveIndex;
};

struct DiskTrigramEntry {
    uint32_t trigram;
    uint32_t postingOffset;
    uint32_t postingCount;
};
#pragma pack(pop)

static_assert(sizeof(DiskIndexHeader) == 48, "DiskIndexHeader size mismatch");
static_assert(sizeof(DiskFileEntry) == 24, "DiskFileEntry size mismatch");
static_assert(sizeof(DiskTrigramEntry) == 12, "DiskTrigramEntry size mismatch");

class DiskIndex {
public:
    DiskIndex() = default;
    ~DiskIndex() { close(); }

    DiskIndex(const DiskIndex&) = delete;
    DiskIndex& operator=(const DiskIndex&) = delete;

    DiskIndex(DiskIndex&& other) noexcept { swap(other); }
    DiskIndex& operator=(DiskIndex&& other) noexcept {
        if (this != &other) {
            close();
            swap(other);
        }
        return *this;
    }

    bool open(const std::wstring& path) {
        close();

        hFile_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile_ == INVALID_HANDLE_VALUE) return false;

        LARGE_INTEGER size;
        if (!GetFileSizeEx(hFile_, &size)) {
            close();
            return false;
        }
        fileSize_ = static_cast<size_t>(size.QuadPart);

        if (fileSize_ < sizeof(DiskIndexHeader)) {
            close();
            return false;
        }

        hMapping_ = CreateFileMappingW(hFile_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!hMapping_) {
            close();
            return false;
        }

        base_ = MapViewOfFile(hMapping_, FILE_MAP_READ, 0, 0, 0);
        if (!base_) {
            close();
            return false;
        }

        header_ = static_cast<const DiskIndexHeader*>(base_);
        if (header_->magic != DiskIndexHeader::MAGIC ||
            header_->version != DiskIndexHeader::VERSION) {
            close();
            return false;
        }

        auto ptr = static_cast<const uint8_t*>(base_) + sizeof(DiskIndexHeader);

        entries_ = reinterpret_cast<const DiskFileEntry*>(ptr);
        ptr += header_->entryCount * sizeof(DiskFileEntry);

        stringPool_ = reinterpret_cast<const wchar_t*>(ptr);
        ptr += header_->stringPoolSize * sizeof(wchar_t);

        trigrams_ = reinterpret_cast<const DiskTrigramEntry*>(ptr);
        ptr += header_->trigramCount * sizeof(DiskTrigramEntry);

        postingData_ = reinterpret_cast<const uint32_t*>(ptr);

        refToIndex_.reserve(header_->entryCount);
        for (uint32_t i = 0; i < header_->entryCount; ++i) {
            uint64_t key = makeRefKey(entries_[i].driveIndex, entries_[i].fileRef);
            refToIndex_[key] = i;
        }

        return true;
    }

    void close() {
        refToIndex_.clear();
        header_ = nullptr;
        entries_ = nullptr;
        stringPool_ = nullptr;
        trigrams_ = nullptr;
        postingData_ = nullptr;

        if (base_) {
            UnmapViewOfFile(base_);
            base_ = nullptr;
        }
        if (hMapping_) {
            CloseHandle(hMapping_);
            hMapping_ = nullptr;
        }
        if (hFile_ != INVALID_HANDLE_VALUE) {
            CloseHandle(hFile_);
            hFile_ = INVALID_HANDLE_VALUE;
        }
        fileSize_ = 0;
    }

    bool isOpen() const { return base_ != nullptr; }

    uint32_t entryCount() const {
        return header_ ? header_->entryCount : 0;
    }

    const DiskFileEntry& entry(uint32_t idx) const {
        return entries_[idx];
    }

    std::wstring_view getName(uint32_t idx) const {
        const auto& e = entries_[idx];
        return { stringPool_ + e.nameOffset, e.nameLength };
    }

    std::span<const uint32_t> getPostings(uint32_t trigram) const {
        if (!header_ || header_->trigramCount == 0) return {};

        int lo = 0;
        int hi = static_cast<int>(header_->trigramCount) - 1;

        while (lo <= hi) {
            int mid = lo + (hi - lo) / 2;
            uint32_t midTri = trigrams_[mid].trigram;

            if (midTri == trigram) {
                return { postingData_ + trigrams_[mid].postingOffset,
                         trigrams_[mid].postingCount };
            }
            if (midTri < trigram) {
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
        return {};
    }

    std::vector<uint32_t> getShortNameIndices() const {
        // trigram 0 is reserved for short names (< 3 chars)
        auto span = getPostings(0);
        return { span.begin(), span.end() };
    }

    std::wstring buildFullPath(uint32_t entryIndex) const {
        if (!header_ || entryIndex >= header_->entryCount) return {};

        std::vector<std::wstring_view> parts;
        parts.reserve(32);

        uint8_t driveIndex = entries_[entryIndex].driveIndex;
        uint32_t current = entryIndex;

        while (current < header_->entryCount) {
            const auto& e = entries_[current];
            auto name = getName(current);
            if (name.empty()) break;

            parts.push_back(name);

            uint64_t parentKey = makeRefKey(driveIndex, e.parentRef);
            auto it = refToIndex_.find(parentKey);
            if (it == refToIndex_.end()) break;
            current = it->second;
        }

        if (parts.empty()) return {};

        wchar_t drive = L'A' + driveIndex;

        std::wstring path;
        path.reserve(256);
        path += drive;
        path += L':';

        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            path += L'\\';
            path += *it;
        }

        return path;
    }

    uint64_t buildTimestamp() const {
        return header_ ? header_->buildTimestamp : 0;
    }

    static std::wstring getIndexPath() {
        wchar_t path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
            std::wstring indexPath = path;
            indexPath += L"\\Velocitty";
            CreateDirectoryW(indexPath.c_str(), nullptr);
            indexPath += L"\\search.idx";
            return indexPath;
        }
        return L"search.idx";
    }

    static uint32_t makeTrigram(wchar_t a, wchar_t b, wchar_t c) {
        return (static_cast<uint32_t>(towlower(a)) & 0x3FF) |
               ((static_cast<uint32_t>(towlower(b)) & 0x3FF) << 10) |
               ((static_cast<uint32_t>(towlower(c)) & 0x3FF) << 20);
    }

    static uint64_t makeRefKey(uint8_t driveIndex, uint64_t fileRef) {
        return (static_cast<uint64_t>(driveIndex) << 56) | (fileRef & 0x00FFFFFFFFFFFFFFULL);
    }

private:
    void swap(DiskIndex& other) noexcept {
        std::swap(hFile_, other.hFile_);
        std::swap(hMapping_, other.hMapping_);
        std::swap(base_, other.base_);
        std::swap(fileSize_, other.fileSize_);
        std::swap(header_, other.header_);
        std::swap(entries_, other.entries_);
        std::swap(stringPool_, other.stringPool_);
        std::swap(trigrams_, other.trigrams_);
        std::swap(postingData_, other.postingData_);
        std::swap(refToIndex_, other.refToIndex_);
    }

    HANDLE hFile_ = INVALID_HANDLE_VALUE;
    HANDLE hMapping_ = nullptr;
    void* base_ = nullptr;
    size_t fileSize_ = 0;

    const DiskIndexHeader* header_ = nullptr;
    const DiskFileEntry* entries_ = nullptr;
    const wchar_t* stringPool_ = nullptr;
    const DiskTrigramEntry* trigrams_ = nullptr;
    const uint32_t* postingData_ = nullptr;

    std::unordered_map<uint64_t, uint32_t> refToIndex_;
};
