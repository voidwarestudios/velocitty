#include "ConPty.h"
#include <string>
#include <vector>
#include <sstream>

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
    std::wstring shellArgs;
    bool isPowerShell = false;

    if (shell && shell[0] != L'\0') {
        shellPath = shell;
        std::wstring lowerShell = shell;
        for (auto& c : lowerShell) c = towlower(c);
        if (lowerShell.find(L"pwsh") != std::wstring::npos) {
            isPowerShell = true;
            shellType_ = ShellType::Pwsh;
            shellName_ = L"PowerShell 7";
        } else if (lowerShell.find(L"powershell") != std::wstring::npos) {
            isPowerShell = true;
            shellType_ = ShellType::PowerShell;
            shellName_ = L"Windows PowerShell";
        } else if (lowerShell.find(L"cmd") != std::wstring::npos) {
            shellType_ = ShellType::Cmd;
            shellName_ = L"Command Prompt";
        } else {
            shellType_ = ShellType::Unknown;
            shellName_ = shell;
        }
    } else {
        wchar_t pwshPath[MAX_PATH];
        if (SearchPathW(nullptr, L"pwsh.exe", nullptr, MAX_PATH, pwshPath, nullptr)) {
            shellPath = L"pwsh.exe";
            isPowerShell = true;
            shellType_ = ShellType::Pwsh;
            shellName_ = L"PowerShell 7";
        } else {
            wchar_t psPath[MAX_PATH];
            if (SearchPathW(nullptr, L"powershell.exe", nullptr, MAX_PATH, psPath, nullptr)) {
                shellPath = L"powershell.exe";
                isPowerShell = true;
                shellType_ = ShellType::PowerShell;
                shellName_ = L"Windows PowerShell";
            } else {
                shellPath = L"cmd.exe";
                shellType_ = ShellType::Cmd;
                shellName_ = L"Command Prompt";
            }
        }
    }

    if (isPowerShell) {
        shellArgs = L" -NoLogo -NoExit -Command \""
            L"Clear-Host; "
            L"Write-Host ; "
            L"Write-Host '  __      __  _____   _        ____     _____  _  _____  _____  __   __' -ForegroundColor Cyan; "
            L"Write-Host '  \\ \\    / / | ____| | |      / __ \\   / ____|| ||_   _||_   _| \\ \\ / /' -ForegroundColor Cyan; "
            L"Write-Host '   \\ \\  / /  | |__   | |     | |  | | | |     | |  | |    | |    \\ V / ' -ForegroundColor Cyan; "
            L"Write-Host '    \\ \\/ /   |  __|  | |     | |  | | | |     | |  | |    | |     | |  ' -ForegroundColor Cyan; "
            L"Write-Host '     \\  /    | |___  | |___  | |__| | | |____ | |  | |    | |     | |  ' -ForegroundColor Cyan; "
            L"Write-Host '      \\/     |_____| |_____|  \\____/   \\_____||_|  |_|    |_|     |_|  ' -ForegroundColor Cyan; "
            L"Write-Host ; "
            L"Write-Host '  ----------------------------------------------------------------------------' -ForegroundColor DarkGray; "
            L"Write-Host ; "
            L"Write-Host -NoNewline '  Shell: ' -ForegroundColor Cyan; Write-Host -NoNewline $(if($PSVersionTable.PSEdition -eq 'Core'){'PowerShell '+$PSVersionTable.PSVersion.Major}else{'Windows PowerShell'}); "
            L"Write-Host -NoNewline '    User: ' -ForegroundColor Cyan; Write-Host -NoNewline $env:USERNAME; "
            L"Write-Host -NoNewline '    Host: ' -ForegroundColor Cyan; Write-Host $env:COMPUTERNAME; "
            L"Write-Host -NoNewline '  Directory: ' -ForegroundColor Cyan; Write-Host (Get-Location); "
            L"Write-Host ; "
            L"Write-Host -NoNewline '  Ctrl+Shift+T' -ForegroundColor DarkGray; Write-Host -NoNewline ' New Tab  '; "
            L"Write-Host -NoNewline '|  Ctrl+Shift+W' -ForegroundColor DarkGray; Write-Host -NoNewline ' Close  '; "
            L"Write-Host -NoNewline '|  Ctrl+Shift+D' -ForegroundColor DarkGray; Write-Host ' Split'; "
            L"Write-Host ; "
            L"\"";
    } else {
        wchar_t currentDir[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, currentDir);
        wchar_t username[256];
        DWORD usernameLen = 256;
        GetUserNameW(username, &usernameLen);
        wchar_t computerName[256];
        DWORD computerNameLen = 256;
        GetComputerNameW(computerName, &computerNameLen);

        const wchar_t E = L'\x1b';
        std::wstringstream cmdStartup;
        cmdStartup << L" /K \"@echo off & cls & echo. & "
            L"echo   " << E << L"[96m__      __  _____   _        ____     _____  _  _____  _____  __   __" << E << L"[0m & "
            L"echo   " << E << L"[96m\\ \\    / / ^| ____^| ^| ^|      / __ \\   / ____^|^| ^|^|_   _^|^|_   _^| \\ \\ / /" << E << L"[0m & "
            L"echo   " << E << L"[96m \\ \\  / /  ^| ^|__   ^| ^|     ^| ^|  ^| ^| ^| ^|     ^| ^|  ^| ^|    ^| ^|    \\ V /" << E << L"[0m & "
            L"echo   " << E << L"[96m  \\ \\/ /   ^|  __^|  ^| ^|     ^| ^|  ^| ^| ^| ^|     ^| ^|  ^| ^|    ^| ^|     ^| ^|" << E << L"[0m & "
            L"echo   " << E << L"[96m   \\  /    ^| ^|___  ^| ^|___  ^| ^|__^| ^| ^| ^|____ ^| ^|  ^| ^|    ^| ^|     ^| ^|" << E << L"[0m & "
            L"echo   " << E << L"[96m    \\/     ^|_____^| ^|_____^|  \\____/   \\_____^|^|_^|  ^|_^|    ^|_^|     ^|_^|" << E << L"[0m & "
            L"echo. & "
            L"echo   " << E << L"[90m----------------------------------------------------------------------------" << E << L"[0m & "
            L"echo. & "
            L"echo   " << E << L"[96mShell:" << E << L"[0m Command Prompt    " << E << L"[96mUser:" << E << L"[0m " << username << L"    " << E << L"[96mHost:" << E << L"[0m " << computerName << L" & "
            L"echo   " << E << L"[96mDirectory:" << E << L"[0m " << currentDir << L" & "
            L"echo. & "
            L"echo   " << E << L"[90mCtrl+Shift+T" << E << L"[37m New Tab  " << E << L"[90m^|  Ctrl+Shift+W" << E << L"[37m Close  " << E << L"[90m^|  Ctrl+Shift+D" << E << L"[37m Split" << E << L"[0m & "
            L"echo. & "
            L"@echo on\"";
        shellArgs = cmdStartup.str();
    }

    std::wstring cmdLineStr = shellPath + shellArgs;
    std::vector<wchar_t> cmdLine(cmdLineStr.begin(), cmdLineStr.end());
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
