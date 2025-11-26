#pragma once

#include "../../framework.h"
#include <functional>
#include <stack>
#include <winioctl.h>

#ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS 0x00400000
#endif
#ifndef FILE_ATTRIBUTE_RECALL_ON_OPEN
#define FILE_ATTRIBUTE_RECALL_ON_OPEN 0x00040000
#endif

class MftEnumerator {
public:
    using Callback = std::function<void(
        const std::wstring& name,
        uint64_t fileRef,
        uint64_t parentRef,
        uint32_t attributes
    )>;

    bool enumerateDrive(wchar_t driveLetter, Callback callback, std::atomic<bool>& cancel) {
        if (tryMftEnumeration(driveLetter, callback, cancel)) {
            return true;
        }
        std::wstring root;
        root += driveLetter;
        root += L":\\";
        fallbackEnumeration(root, callback, cancel);
        return true;
    }

    static bool hasAdminPrivileges() {
        BOOL isAdmin = FALSE;
        PSID adminGroup = nullptr;
        SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;

        if (AllocateAndInitializeSid(&ntAuth, 2,
            SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0, &adminGroup)) {
            CheckTokenMembership(nullptr, adminGroup, &isAdmin);
            FreeSid(adminGroup);
        }
        return isAdmin != FALSE;
    }

private:
    bool tryMftEnumeration(wchar_t drive, Callback callback, std::atomic<bool>& cancel) {
        wchar_t volumePath[8] = L"\\\\.\\X:";
        volumePath[4] = drive;

        HANDLE volume = CreateFileW(
            volumePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );

        if (volume == INVALID_HANDLE_VALUE) {
            volume = CreateFileW(
                volumePath,
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr
            );
        }

        if (volume == INVALID_HANDLE_VALUE) {
            return false;
        }

        USN_JOURNAL_DATA_V1 journalData{};
        DWORD bytesReturned = 0;

        if (!DeviceIoControl(
            volume,
            FSCTL_QUERY_USN_JOURNAL,
            nullptr, 0,
            &journalData, sizeof(journalData),
            &bytesReturned, nullptr)) {
            CloseHandle(volume);
            return false;
        }

        MFT_ENUM_DATA enumData{};
        enumData.StartFileReferenceNumber = 0;
        enumData.LowUsn = 0;
        enumData.HighUsn = journalData.NextUsn;

        constexpr size_t bufferSize = 1024 * 1024;
        auto buffer = std::make_unique<uint8_t[]>(bufferSize);
        uint32_t filesEnumerated = 0;
        DWORDLONG lastStartRef = 0;
        int stuckCount = 0;

        while (!cancel) {
            if (!DeviceIoControl(
                volume,
                FSCTL_ENUM_USN_DATA,
                &enumData, sizeof(enumData),
                buffer.get(), static_cast<DWORD>(bufferSize),
                &bytesReturned, nullptr)) {
                break;
            }

            if (bytesReturned <= sizeof(USN)) break;

            auto* record = reinterpret_cast<USN_RECORD_V2*>(buffer.get() + sizeof(USN));
            uint8_t* bufferEnd = buffer.get() + bytesReturned;

            while (reinterpret_cast<uint8_t*>(record) < bufferEnd && !cancel) {
                if (record->RecordLength == 0) break;

                std::wstring name(
                    record->FileName,
                    record->FileNameLength / sizeof(wchar_t)
                );

                callback(
                    name,
                    static_cast<uint64_t>(record->FileReferenceNumber),
                    static_cast<uint64_t>(record->ParentFileReferenceNumber),
                    record->FileAttributes
                );

                filesEnumerated++;

                record = reinterpret_cast<USN_RECORD_V2*>(
                    reinterpret_cast<uint8_t*>(record) + record->RecordLength
                );
            }

            DWORDLONG newStartRef = *reinterpret_cast<DWORDLONG*>(buffer.get());

            if (newStartRef == lastStartRef) {
                if (++stuckCount > 3) break;
            } else {
                stuckCount = 0;
            }

            lastStartRef = enumData.StartFileReferenceNumber;
            enumData.StartFileReferenceNumber = newStartRef;
        }

        CloseHandle(volume);

        return filesEnumerated > 0;
    }

    void fallbackEnumeration(const std::wstring& root, Callback callback,
                             std::atomic<bool>& cancel) {
        std::stack<std::wstring> dirs;
        dirs.push(root);

        while (!dirs.empty() && !cancel) {
            std::wstring current = std::move(dirs.top());
            dirs.pop();

            if (current.length() > 260) continue;

            WIN32_FIND_DATAW fd;
            std::wstring searchPath = current;
            if (searchPath.back() != L'\\') searchPath += L'\\';
            searchPath += L'*';

            HANDLE find = FindFirstFileExW(
                searchPath.c_str(),
                FindExInfoBasic,
                &fd,
                FindExSearchNameMatch,
                nullptr,
                FIND_FIRST_EX_LARGE_FETCH
            );

            if (find == INVALID_HANDLE_VALUE) continue;

            do {
                if (cancel) break;

                if (fd.cFileName[0] == L'.') {
                    if (fd.cFileName[1] == 0) continue;
                    if (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0) continue;
                }

                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) &&
                    (fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM)) {
                    continue;
                }

                std::wstring fullPath = current;
                if (fullPath.back() != L'\\') fullPath += L'\\';
                fullPath += fd.cFileName;

                uint64_t fileHash = hashPath(fullPath);
                uint64_t dirParentHash = hashPath(current);

                callback(fd.cFileName, fileHash, dirParentHash, fd.dwFileAttributes);

                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) continue;
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) continue;
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_RECALL_ON_OPEN) continue;

                    if (!shouldSkipDirectory(fd.cFileName)) {
                        dirs.push(fullPath);
                    }
                }

            } while (FindNextFileW(find, &fd));

            FindClose(find);
        }
    }

    static uint64_t hashPath(const std::wstring& path) {
        uint64_t hash = 14695981039346656037ULL;
        for (wchar_t c : path) {
            hash ^= static_cast<uint64_t>(towlower(c));
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    static bool shouldSkipDirectory(const wchar_t* name) {
        static const wchar_t* skipDirs[] = {
            L"$Recycle.Bin",
            L"System Volume Information",
            L"$RECYCLE.BIN",
            L"Windows\\WinSxS",
            L"node_modules",
            L".git",
            L"__pycache__",
            L".vs",
            nullptr
        };

        for (int i = 0; skipDirs[i] != nullptr; ++i) {
            if (_wcsicmp(name, skipDirs[i]) == 0) return true;
        }
        return false;
    }
};
