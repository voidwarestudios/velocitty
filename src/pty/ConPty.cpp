#include "ConPty.h"
#include <string>
#include <vector>

ConPty::~ConPty() {
    close();
}

bool ConPty::create(uint16_t cols, uint16_t rows, const wchar_t* shell) {
    size_ = { static_cast<SHORT>(cols), static_cast<SHORT>(rows) };

    HANDLE pipeInRead = INVALID_HANDLE_VALUE;
    HANDLE pipeOutWrite = INVALID_HANDLE_VALUE;

    if (!CreatePipe(&pipeInRead, &pipeIn_, nullptr, 0)) {
        return false;
    }

    if (!CreatePipe(&pipeOut_, &pipeOutWrite, nullptr, 0)) {
        CloseHandle(pipeInRead);
        CloseHandle(pipeIn_);
        pipeIn_ = INVALID_HANDLE_VALUE;
        return false;
    }

    HRESULT hr = CreatePseudoConsole(size_, pipeInRead, pipeOutWrite, 0, &hPC_);

    CloseHandle(pipeInRead);
    CloseHandle(pipeOutWrite);

    if (FAILED(hr)) {
        CloseHandle(pipeIn_);
        CloseHandle(pipeOut_);
        pipeIn_ = INVALID_HANDLE_VALUE;
        pipeOut_ = INVALID_HANDLE_VALUE;
        return false;
    }

    SIZE_T attrListSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);

    auto attrList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(
        HeapAlloc(GetProcessHeap(), 0, attrListSize));

    if (!attrList) {
        close();
        return false;
    }

    if (!InitializeProcThreadAttributeList(attrList, 1, 0, &attrListSize)) {
        HeapFree(GetProcessHeap(), 0, attrList);
        close();
        return false;
    }

    if (!UpdateProcThreadAttribute(attrList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   hPC_, sizeof(hPC_), nullptr, nullptr)) {
        DeleteProcThreadAttributeList(attrList);
        HeapFree(GetProcessHeap(), 0, attrList);
        close();
        return false;
    }

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.lpAttributeList = attrList;

    std::wstring shellPath;
    if (shell && shell[0] != L'\0') {
        shellPath = shell;
    } else {
        wchar_t pwshPath[MAX_PATH];
        if (SearchPathW(nullptr, L"pwsh.exe", nullptr, MAX_PATH, pwshPath, nullptr)) {
            shellPath = L"pwsh.exe";
        } else {
            wchar_t psPath[MAX_PATH];
            if (SearchPathW(nullptr, L"powershell.exe", nullptr, MAX_PATH, psPath, nullptr)) {
                shellPath = L"powershell.exe";
            } else {
                shellPath = L"cmd.exe";
            }
        }
    }

    std::vector<wchar_t> cmdLine(shellPath.begin(), shellPath.end());
    cmdLine.push_back(L'\0');

    BOOL success = CreateProcessW(
        nullptr,
        cmdLine.data(),
        nullptr,
        nullptr,
        FALSE,
        EXTENDED_STARTUPINFO_PRESENT,
        nullptr,
        nullptr,
        &si.StartupInfo,
        &childProc_
    );

    DeleteProcThreadAttributeList(attrList);
    HeapFree(GetProcessHeap(), 0, attrList);

    if (!success) {
        close();
        return false;
    }

    return true;
}

void ConPty::resize(uint16_t cols, uint16_t rows) {
    if (hPC_) {
        size_ = { static_cast<SHORT>(cols), static_cast<SHORT>(rows) };
        ResizePseudoConsole(hPC_, size_);
    }
}

void ConPty::close() {
    if (childProc_.hProcess != nullptr) {
        TerminateProcess(childProc_.hProcess, 0);
        CloseHandle(childProc_.hProcess);
        CloseHandle(childProc_.hThread);
        childProc_ = {};
    }

    if (hPC_) {
        ClosePseudoConsole(hPC_);
        hPC_ = nullptr;
    }

    if (pipeIn_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipeIn_);
        pipeIn_ = INVALID_HANDLE_VALUE;
    }

    if (pipeOut_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipeOut_);
        pipeOut_ = INVALID_HANDLE_VALUE;
    }
}

bool ConPty::isAlive() const {
    if (childProc_.hProcess == nullptr) return false;

    DWORD exitCode;
    if (GetExitCodeProcess(childProc_.hProcess, &exitCode)) {
        return exitCode == STILL_ACTIVE;
    }
    return false;
}
