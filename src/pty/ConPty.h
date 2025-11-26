#pragma once

#include "../../framework.h"
#include <cstdint>
#include <string>

enum class ShellType {
    Unknown,
    Cmd,
    PowerShell,
    Pwsh
};

class ConPty {
public:
    ConPty() = default;
    ~ConPty();

    ConPty(const ConPty&) = delete;
    ConPty& operator=(const ConPty&) = delete;

    bool create(uint16_t cols, uint16_t rows, const wchar_t* shell = nullptr);
    void resize(uint16_t cols, uint16_t rows);
    void close();

    HANDLE getReadHandle() const { return pipeOut_; }
    HANDLE getWriteHandle() const { return pipeIn_; }
    bool isAlive() const;

    ShellType getShellType() const { return shellType_; }
    const std::wstring& getShellName() const { return shellName_; }

private:
    HPCON hPC_ = nullptr;
    HANDLE pipeIn_ = INVALID_HANDLE_VALUE;
    HANDLE pipeOut_ = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION childProc_{};
    COORD size_{80, 30};
    ShellType shellType_ = ShellType::Unknown;
    std::wstring shellName_;
};
