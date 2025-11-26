#pragma once

#include "../../framework.h"
#include "DiskIndex.h"
#include "MftEnumerator.h"
#include <map>
#include <set>

#pragma pack(push, 1)
struct DriveMetadata {
    wchar_t driveLetter;
    uint8_t padding[2];
    uint32_t volumeSerial;
    uint64_t lastUsn;
    uint64_t journalId;
};
#pragma pack(pop)

class IndexBuilder {
public:
    using ProgressCallback = std::function<void(float progress, const std::wstring& status)>;

    struct BuildStats {
        uint32_t filesIndexed = 0;
        uint32_t filesAdded = 0;
        uint32_t filesRemoved = 0;
        uint32_t trigramsCreated = 0;
        bool wasIncremental = false;
    };

    IndexBuilder() = default;

    BuildStats build(const std::wstring& outputPath, std::atomic<bool>& cancel,
                     ProgressCallback progress = nullptr) {
        BuildStats stats;

        entries_.clear();
        stringPool_.clear();
        trigramPostings_.clear();
        shortNames_.clear();
        driveMetadata_.clear();

        stringPool_.reserve(2 * 1024 * 1024);
        entries_.reserve(1000000);

        if (progress) progress(0.0f, L"Scanning drives...");

        DWORD drives = GetLogicalDrives();
        std::vector<int> fixedDrives;

        for (int i = 0; i < 26; ++i) {
            if (!(drives & (1 << i))) continue;
            wchar_t root[4] = { static_cast<wchar_t>(L'A' + i), L':', L'\\', 0 };
            if (GetDriveTypeW(root) == DRIVE_FIXED) {
                fixedDrives.push_back(i);
            }
        }

        MftEnumerator enumerator;
        int currentDrive = 0;

        for (int driveIdx : fixedDrives) {
            if (cancel) break;

            wchar_t drive = L'A' + static_cast<wchar_t>(driveIdx);
            uint8_t driveIndex = static_cast<uint8_t>(driveIdx);

            float driveBaseProgress = static_cast<float>(currentDrive) / static_cast<float>(fixedDrives.size()) * 0.8f;
            float driveProgressRange = 0.8f / static_cast<float>(fixedDrives.size());
            uint32_t driveFileCount = 0;

            if (progress) {
                progress(driveBaseProgress, std::wstring(L"Indexing ") + drive + L":\\ ...");
            }

            DriveMetadata meta{};
            meta.driveLetter = drive;
            meta.volumeSerial = getVolumeSerial(drive);

            enumerator.enumerateDrive(drive,
                [&](const std::wstring& name, uint64_t ref, uint64_t parent, uint32_t attrs) {
                    if (cancel) return;
                    addEntry(ref, parent, name, static_cast<uint8_t>(attrs), driveIndex);
                    stats.filesIndexed++;
                    driveFileCount++;

                    if (progress && (driveFileCount % 5000) == 0) {
                        float withinDrive = std::min(0.95f, driveFileCount / 500000.0f);
                        float p = driveBaseProgress + withinDrive * driveProgressRange;
                        progress(p, std::wstring(L"Indexing ") + drive + L":\\ - " +
                                    std::to_wstring(driveFileCount) + L" files...");
                    }
                },
                cancel
            );

            // need this for incremental updates
            captureJournalPosition(drive, meta);
            driveMetadata_.push_back(meta);

            currentDrive++;
        }

        if (cancel) return stats;

        if (progress) progress(0.85f, L"Building trigram index...");

        for (uint32_t idx = 0; idx < entries_.size(); ++idx) {
            if (cancel) break;
            auto name = getName(idx);
            addTrigrams(idx, name);
        }

        stats.trigramsCreated = static_cast<uint32_t>(trigramPostings_.size());

        if (cancel) return stats;

        if (progress) progress(0.90f, L"Writing index file...");

        writeToFile(outputPath);

        if (progress) progress(1.0f, L"Complete");

        stats.filesAdded = stats.filesIndexed;
        return stats;
    }

    BuildStats incrementalUpdate(const std::wstring& indexPath, std::atomic<bool>& cancel,
                                  ProgressCallback progress = nullptr) {
        BuildStats stats;
        stats.wasIncremental = true;

        if (!loadExistingMetadata(indexPath)) {
            return build(indexPath, cancel, progress);
        }

        if (progress) progress(0.0f, L"Checking for changes...");

        std::vector<uint64_t> deletedRefs;
        std::vector<FileChange> addedFiles;

        int driveIdx = 0;
        for (auto& meta : driveMetadata_) {
            if (cancel) break;

            if (progress) {
                float p = static_cast<float>(driveIdx) / static_cast<float>(driveMetadata_.size());
                progress(p * 0.5f, std::wstring(L"Scanning changes on ") + meta.driveLetter + L":\\");
            }

            queryJournalChanges(meta, deletedRefs, addedFiles, cancel,
                                static_cast<uint8_t>(meta.driveLetter - L'A'));
            driveIdx++;
        }

        if (cancel) return stats;

        size_t totalChanges = deletedRefs.size() + addedFiles.size();
        if (totalChanges > entries_.size() / 4) {
            if (progress) progress(0.0f, L"Many changes detected, rebuilding...");
            return build(indexPath, cancel, progress);
        }

        if (totalChanges == 0) {
            if (progress) progress(1.0f, L"Index is up to date");
            return stats;
        }

        if (progress) progress(0.6f, L"Applying changes...");

        std::set<uint64_t> deletedSet(deletedRefs.begin(), deletedRefs.end());
        for (auto it = refToIndex_.begin(); it != refToIndex_.end(); ) {
            if (deletedSet.count(it->first)) {
                uint32_t idx = it->second;
                removeTrigrams(idx);
                entries_[idx].fileRef = 0;  // mark as deleted
                stats.filesRemoved++;
                it = refToIndex_.erase(it);
            } else {
                ++it;
            }
        }

        for (const auto& change : addedFiles) {
            if (cancel) break;

            uint8_t driveIndex = static_cast<uint8_t>(change.driveLetter - L'A');
            uint32_t idx = addEntry(change.fileRef, change.parentRef, change.name,
                                    static_cast<uint8_t>(change.attributes), driveIndex);
            addTrigrams(idx, change.name);
            stats.filesAdded++;
        }

        stats.filesIndexed = static_cast<uint32_t>(entries_.size());
        stats.trigramsCreated = static_cast<uint32_t>(trigramPostings_.size());

        if (cancel) return stats;

        if (progress) progress(0.9f, L"Writing updated index...");

        for (auto& meta : driveMetadata_) {
            captureJournalPosition(meta.driveLetter, meta);
        }

        writeToFile(indexPath);

        if (progress) progress(1.0f, L"Update complete");

        return stats;
    }

    static bool needsRebuild(const std::wstring& indexPath) {
        DiskIndex existing;
        if (!existing.open(indexPath)) return true;

        uint64_t age = GetTickCount64() - existing.buildTimestamp();
        constexpr uint64_t MAX_AGE_MS = 7 * 24 * 60 * 60 * 1000;  // 7 days

        return age > MAX_AGE_MS;
    }

private:
    struct FileChange {
        wchar_t driveLetter;
        uint64_t fileRef;
        uint64_t parentRef;
        std::wstring name;
        uint32_t attributes;
    };

    uint32_t addEntry(uint64_t fileRef, uint64_t parentRef, const std::wstring& name,
                      uint8_t attributes, uint8_t driveIndex) {
        uint32_t idx = static_cast<uint32_t>(entries_.size());
        uint32_t nameOffset = static_cast<uint32_t>(stringPool_.size());
        uint16_t nameLen = static_cast<uint16_t>(std::min(name.length(), size_t(UINT16_MAX)));

        stringPool_.insert(stringPool_.end(), name.begin(), name.begin() + nameLen);

        DiskFileEntry entry{};
        entry.fileRef = fileRef;
        entry.parentRef = parentRef;
        entry.nameOffset = nameOffset;
        entry.nameLength = nameLen;
        entry.attributes = attributes;
        entry.driveIndex = driveIndex;

        entries_.push_back(entry);

        uint64_t key = makeRefKey(driveIndex, fileRef);
        refToIndex_[key] = idx;

        return idx;
    }

    static uint64_t makeRefKey(uint8_t driveIndex, uint64_t fileRef) {
        return (static_cast<uint64_t>(driveIndex) << 56) | (fileRef & 0x00FFFFFFFFFFFFFFULL);
    }

    std::wstring_view getName(uint32_t idx) const {
        const auto& e = entries_[idx];
        return { stringPool_.data() + e.nameOffset, e.nameLength };
    }

    void addTrigrams(uint32_t fileIndex, std::wstring_view name) {
        if (name.size() < 3) {
            shortNames_.push_back(fileIndex);
            return;
        }

        for (size_t i = 0; i + 2 < name.size(); ++i) {
            uint32_t tri = DiskIndex::makeTrigram(name[i], name[i + 1], name[i + 2]);
            trigramPostings_[tri].push_back(fileIndex);
        }
    }

    void removeTrigrams(uint32_t fileIndex) {
        auto name = getName(fileIndex);
        if (name.size() < 3) {
            auto it = std::find(shortNames_.begin(), shortNames_.end(), fileIndex);
            if (it != shortNames_.end()) shortNames_.erase(it);
            return;
        }

        for (size_t i = 0; i + 2 < name.size(); ++i) {
            uint32_t tri = DiskIndex::makeTrigram(name[i], name[i + 1], name[i + 2]);
            auto& list = trigramPostings_[tri];
            auto it = std::find(list.begin(), list.end(), fileIndex);
            if (it != list.end()) list.erase(it);
        }
    }

    void writeToFile(const std::wstring& path) {
        std::vector<std::pair<uint32_t, std::vector<uint32_t>>> sortedTrigrams;
        sortedTrigrams.reserve(trigramPostings_.size() + 1);

        if (!shortNames_.empty()) {
            std::sort(shortNames_.begin(), shortNames_.end());
            sortedTrigrams.emplace_back(0, shortNames_);
        }

        for (auto& [tri, list] : trigramPostings_) {
            if (!list.empty()) {
                std::sort(list.begin(), list.end());
                sortedTrigrams.emplace_back(tri, std::move(list));
            }
        }

        std::sort(sortedTrigrams.begin(), sortedTrigrams.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        std::vector<DiskTrigramEntry> trigramEntries;
        std::vector<uint32_t> allPostings;
        trigramEntries.reserve(sortedTrigrams.size());

        for (const auto& [tri, list] : sortedTrigrams) {
            DiskTrigramEntry te{};
            te.trigram = tri;
            te.postingOffset = static_cast<uint32_t>(allPostings.size());
            te.postingCount = static_cast<uint32_t>(list.size());
            trigramEntries.push_back(te);
            allPostings.insert(allPostings.end(), list.begin(), list.end());
        }

        DiskIndexHeader header{};
        header.magic = DiskIndexHeader::MAGIC;
        header.version = DiskIndexHeader::VERSION;
        header.entryCount = static_cast<uint32_t>(entries_.size());
        header.stringPoolSize = static_cast<uint32_t>(stringPool_.size());
        header.trigramCount = static_cast<uint32_t>(trigramEntries.size());
        header.postingDataSize = static_cast<uint32_t>(allPostings.size());
        header.buildTimestamp = GetTickCount64();

        std::wstring tempPath = path + L".tmp";

        HANDLE hFile = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0,
                                   nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return;

        DWORD written;
        WriteFile(hFile, &header, sizeof(header), &written, nullptr);
        WriteFile(hFile, entries_.data(), entries_.size() * sizeof(DiskFileEntry), &written, nullptr);
        WriteFile(hFile, stringPool_.data(), stringPool_.size() * sizeof(wchar_t), &written, nullptr);
        WriteFile(hFile, trigramEntries.data(), trigramEntries.size() * sizeof(DiskTrigramEntry), &written, nullptr);
        WriteFile(hFile, allPostings.data(), allPostings.size() * sizeof(uint32_t), &written, nullptr);

        uint32_t metaCount = static_cast<uint32_t>(driveMetadata_.size());
        WriteFile(hFile, &metaCount, sizeof(metaCount), &written, nullptr);
        WriteFile(hFile, driveMetadata_.data(), driveMetadata_.size() * sizeof(DriveMetadata), &written, nullptr);

        CloseHandle(hFile);

        DeleteFileW(path.c_str());
        MoveFileW(tempPath.c_str(), path.c_str());
    }

    bool loadExistingMetadata(const std::wstring& path) {
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return false;

        LARGE_INTEGER fileSize;
        GetFileSizeEx(hFile, &fileSize);

        DiskIndexHeader header{};
        DWORD read;
        ReadFile(hFile, &header, sizeof(header), &read, nullptr);

        if (header.magic != DiskIndexHeader::MAGIC) {
            CloseHandle(hFile);
            return false;
        }

        entries_.resize(header.entryCount);
        ReadFile(hFile, entries_.data(), header.entryCount * sizeof(DiskFileEntry), &read, nullptr);

        stringPool_.resize(header.stringPoolSize);
        ReadFile(hFile, stringPool_.data(), header.stringPoolSize * sizeof(wchar_t), &read, nullptr);

        LARGE_INTEGER skip;
        skip.QuadPart = header.trigramCount * sizeof(DiskTrigramEntry) +
                        header.postingDataSize * sizeof(uint32_t);
        SetFilePointerEx(hFile, skip, nullptr, FILE_CURRENT);

        uint32_t metaCount = 0;
        ReadFile(hFile, &metaCount, sizeof(metaCount), &read, nullptr);
        driveMetadata_.resize(metaCount);
        ReadFile(hFile, driveMetadata_.data(), metaCount * sizeof(DriveMetadata), &read, nullptr);

        CloseHandle(hFile);

        refToIndex_.clear();
        refToIndex_.reserve(entries_.size());
        for (uint32_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].fileRef != 0) {
                uint64_t key = makeRefKey(entries_[i].driveIndex, entries_[i].fileRef);
                refToIndex_[key] = i;
            }
        }

        trigramPostings_.clear();
        shortNames_.clear();
        for (uint32_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].fileRef != 0) {
                addTrigrams(i, getName(i));
            }
        }

        return true;
    }

    uint32_t getVolumeSerial(wchar_t drive) {
        wchar_t root[4] = { drive, L':', L'\\', 0 };
        DWORD serial = 0;
        GetVolumeInformationW(root, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0);
        return serial;
    }

    void captureJournalPosition(wchar_t drive, DriveMetadata& meta) {
        wchar_t volumePath[8] = { L'\\', L'\\', L'.', L'\\', drive, L':', 0 };

        HANDLE hVolume = CreateFileW(volumePath, GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr, OPEN_EXISTING, 0, nullptr);
        if (hVolume == INVALID_HANDLE_VALUE) return;

        USN_JOURNAL_DATA_V0 journalData{};
        DWORD bytesReturned;

        if (DeviceIoControl(hVolume, FSCTL_QUERY_USN_JOURNAL,
                            nullptr, 0, &journalData, sizeof(journalData),
                            &bytesReturned, nullptr)) {
            meta.lastUsn = journalData.NextUsn;
            meta.journalId = journalData.UsnJournalID;
        }

        CloseHandle(hVolume);
    }

    void queryJournalChanges(DriveMetadata& meta, std::vector<uint64_t>& deleted,
                             std::vector<FileChange>& added, std::atomic<bool>& cancel,
                             uint8_t driveIndex) {
        wchar_t volumePath[8] = { L'\\', L'\\', L'.', L'\\', meta.driveLetter, L':', 0 };

        HANDLE hVolume = CreateFileW(volumePath, GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr, OPEN_EXISTING, 0, nullptr);
        if (hVolume == INVALID_HANDLE_VALUE) return;

        // verify journal ID hasn't changed
        USN_JOURNAL_DATA_V0 journalData{};
        DWORD bytesReturned;

        if (!DeviceIoControl(hVolume, FSCTL_QUERY_USN_JOURNAL,
                             nullptr, 0, &journalData, sizeof(journalData),
                             &bytesReturned, nullptr)) {
            CloseHandle(hVolume);
            return;
        }

        if (journalData.UsnJournalID != meta.journalId) {
            // journal was recreated, can't do incremental - signal by adding many fake changes (idk theres prob a better way to do that)
            CloseHandle(hVolume);
            for (uint32_t i = 0; i < 1000000; ++i) {
                deleted.push_back(makeRefKey(driveIndex, i));
            }
            return;
        }

        READ_USN_JOURNAL_DATA_V0 readData{};
        readData.StartUsn = meta.lastUsn;
        readData.ReasonMask = USN_REASON_FILE_CREATE | USN_REASON_FILE_DELETE |
                              USN_REASON_RENAME_NEW_NAME | USN_REASON_RENAME_OLD_NAME;
        readData.ReturnOnlyOnClose = FALSE;
        readData.Timeout = 0;
        readData.BytesToWaitFor = 0;
        readData.UsnJournalID = meta.journalId;

        std::vector<uint8_t> buffer(64 * 1024);

        while (!cancel) {
            if (!DeviceIoControl(hVolume, FSCTL_READ_USN_JOURNAL,
                                 &readData, sizeof(readData),
                                 buffer.data(), static_cast<DWORD>(buffer.size()),
                                 &bytesReturned, nullptr)) {
                break;
            }

            if (bytesReturned <= sizeof(USN)) break;

            USN nextUsn = *reinterpret_cast<USN*>(buffer.data());
            auto* record = reinterpret_cast<USN_RECORD*>(buffer.data() + sizeof(USN));
            DWORD remaining = bytesReturned - sizeof(USN);

            while (remaining > 0 && !cancel) {
                if (record->RecordLength == 0) break;

                if (record->Reason & (USN_REASON_FILE_DELETE | USN_REASON_RENAME_OLD_NAME)) {
                    deleted.push_back(makeRefKey(driveIndex, record->FileReferenceNumber));
                }
                if (record->Reason & (USN_REASON_FILE_CREATE | USN_REASON_RENAME_NEW_NAME)) {
                    std::wstring name(record->FileName, record->FileNameLength / sizeof(wchar_t));
                    added.push_back({
                        meta.driveLetter,
                        record->FileReferenceNumber,
                        record->ParentFileReferenceNumber,
                        std::move(name),
                        record->FileAttributes
                    });
                }

                remaining -= record->RecordLength;
                record = reinterpret_cast<USN_RECORD*>(
                    reinterpret_cast<uint8_t*>(record) + record->RecordLength);
            }

            readData.StartUsn = nextUsn;
            if (nextUsn >= journalData.NextUsn) break;
        }

        meta.lastUsn = journalData.NextUsn;
        CloseHandle(hVolume);
    }

    std::vector<DiskFileEntry> entries_;
    std::vector<wchar_t> stringPool_;
    std::unordered_map<uint32_t, std::vector<uint32_t>> trigramPostings_;
    std::vector<uint32_t> shortNames_;
    std::unordered_map<uint64_t, uint32_t> refToIndex_;
    std::vector<DriveMetadata> driveMetadata_;
};
