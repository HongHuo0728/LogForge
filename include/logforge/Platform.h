#pragma once
#include "logforge/Version.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

namespace logforge {
namespace fs = std::filesystem;
std::wstring Wide(const std::string& text);
std::string Utf8(const std::wstring& text);
std::string PathText(const fs::path& path);
std::wstring WinError(DWORD code = GetLastError());
fs::path DataDirectory();
fs::path ExecutableDirectory();
fs::path SystemExecutable(const wchar_t* filename);
std::wstring QuoteArgument(const std::wstring& arg);
std::wstring CommandLine(const fs::path& exe, const std::vector<std::wstring>& args);
class Handle {
  public:
    Handle() = default;
    explicit Handle(HANDLE h) : value_(h) {}
    ~Handle() {
        reset();
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(other.release()) {}
    Handle& operator=(Handle&& other) noexcept {
        reset(other.release());
        return *this;
    }
    HANDLE get() const noexcept {
        return value_;
    }
    explicit operator bool() const noexcept {
        return value_ && value_ != INVALID_HANDLE_VALUE;
    }
    HANDLE release() noexcept {
        HANDLE h = value_;
        value_ = nullptr;
        return h;
    }
    void reset(HANDLE h = nullptr) noexcept {
        if (*this)
            CloseHandle(value_);
        value_ = h;
    }

  private:
    HANDLE value_ = nullptr;
};
class Logger {
  public:
    Logger();
    void Write(const std::string& text);
    const fs::path& Path() const {
        return path_;
    }

  private:
    fs::path path_;
    std::ofstream file_;
    std::mutex mutex_;
};
class FFmpegProcess {
  public:
    FFmpegProcess(const fs::path& exe, const std::vector<std::wstring>& args, bool inputPipe = false);
    ~FFmpegProcess();
    FFmpegProcess(const FFmpegProcess&) = delete;
    FFmpegProcess& operator=(const FFmpegProcess&) = delete;
    size_t Read(void* data, size_t bytes);
    void Write(const void* data, size_t bytes);
    void CloseInput();
    void Terminate() noexcept;
    bool Running() const;
    int Wait();
    HANDLE ErrorPipe() const {
        return error_.get();
    }

  private:
    Handle process_, job_, input_, output_, error_;
};
using LineCallback = std::function<void(const std::string&)>;
void ReadLines(HANDLE pipe, const LineCallback& callback);
struct ProcessResult {
    int exitCode = -1;
    std::string output, error;
};
ProcessResult RunProcess(const fs::path& exe, const std::vector<std::wstring>& args,
                         const std::atomic_bool* cancel = nullptr, int timeoutSeconds = 30,
                         const LineCallback& onLine = {});
std::string SHA256(const fs::path& file);
} // namespace logforge
