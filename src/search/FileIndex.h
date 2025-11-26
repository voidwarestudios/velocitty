#pragma once

#include "../../framework.h"
#include <unordered_map>
#include <shared_mutex>
#include <string_view>

#pragma pack(push, 1)
struct FileEntry {
    uint64_t fileRef;
    uint64_t parentRef;
    uint32_t nameOffset;
    uint16_t nameLength;
    uint8_t  attributes;
    uint8_t  driveIndex;
};
#pragma pack(pop)

static_assert(sizeof(FileEntry) == 24, "FileEntry should be 24 bytes");

class StringPool {
public:
    StringPool() {
        data_.reserve(1024 * 1024);
    }

    uint32_t intern(const wchar_t* str, uint16_t len) {
        uint32_t offset = static_cast<uint32_t>(data_.size());
        data_.resize(data_.size() + len);
        memcpy(data_.data() + offset, str, len * sizeof(wchar_t));
        return offset;
    }

    std::wstring_view get(uint32_t offset, uint16_t len) const {
        if (offset + len > data_.size()) return {};
        return { data_.data() + offset, len };
    }

    void clear() {
        data_.clear();
        data_.shrink_to_fit();
    }

    size_t memoryUsage() const {
        return data_.capacity() * sizeof(wchar_t);
    }

private:
    std::vector<wchar_t> data_;
};

class FileIndex {
public:
    FileIndex() {
        entries_.reserve(1000000);
        refToIndex_.reserve(1000000);
    }

    uint32_t addEntry(uint64_t fileRef, uint64_t parentRef, const wchar_t* name,
                      uint16_t nameLen, uint8_t attributes, uint8_t driveIndex) {
        uint32_t idx = static_cast<uint32_t>(entries_.size());
        uint32_t nameOffset = stringPool_.intern(name, nameLen);

        FileEntry entry{};
        entry.fileRef = fileRef;
        entry.parentRef = parentRef;
        entry.nameOffset = nameOffset;
        entry.nameLength = nameLen;
        entry.attributes = attributes;
        entry.driveIndex = driveIndex;

        entries_.push_back(entry);
        refToIndex_[fileRef] = idx;

        return idx;
    }

    const FileEntry& getEntry(uint32_t index) const {
        return entries_[index];
    }

    std::wstring_view getName(uint32_t index) const {
        const auto& entry = entries_[index];
        return stringPool_.get(entry.nameOffset, entry.nameLength);
    }

    std::wstring buildFullPath(uint32_t entryIndex) const {
        std::vector<std::wstring_view> parts;
        parts.reserve(32);

        uint32_t current = entryIndex;
        while (current < entries_.size()) {
            const auto& entry = entries_[current];
            auto name = stringPool_.get(entry.nameOffset, entry.nameLength);
            if (name.empty()) break;

            parts.push_back(name);

            auto it = refToIndex_.find(entry.parentRef);
            if (it == refToIndex_.end()) break;
            current = it->second;
        }

        if (parts.empty()) return {};

        const auto& rootEntry = entries_[entryIndex];
        wchar_t drive = L'A' + rootEntry.driveIndex;

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

    size_t size() const {
        return entries_.size();
    }

    void clear() {
        entries_.clear();
        entries_.shrink_to_fit();
        stringPool_.clear();
        refToIndex_.clear();
    }

    size_t memoryUsage() const {
        return entries_.capacity() * sizeof(FileEntry) +
               stringPool_.memoryUsage() +
               refToIndex_.size() * (sizeof(uint64_t) + sizeof(uint32_t));
    }

    const std::vector<FileEntry>& entries() const { return entries_; }

private:
    std::vector<FileEntry> entries_;
    StringPool stringPool_;
    std::unordered_map<uint64_t, uint32_t> refToIndex_;
};
