#include "logforge/Platform.h"
#include <array>
#include <bcrypt.h>
#include <chrono>
#include <iomanip>
#include <shlobj.h>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace logforge {
std::wstring Wide(const std::string& s) {
    if (s.empty())
        return {};
    const int n =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n)
        throw std::runtime_error("Invalid UTF-8 text.");
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
std::string Utf8(const std::wstring& s) {
    if (s.empty())
        return {};
    const int n =
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::string PathText(const fs::path& p) {
    return Utf8(p.wstring());
}
std::wstring WinError(DWORD code) {
    wchar_t* p = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&p), 0, nullptr);
    std::wstring text = p ? p : L"Windows operation failed";
    if (p)
        LocalFree(p);
    return text;
}
fs::path DataDirectory() {
    wchar_t overrideDir[32768]{};
    if (GetEnvironmentVariableW(L"LOGFORGE_DATA_DIR", overrideDir, 32768))
        return fs::path(overrideDir);
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)))
        throw std::runtime_error("Cannot locate LocalAppData.");
    fs::path dir(p);
    CoTaskMemFree(p);
    return dir / L"LogForge";
}
fs::path ExecutableDirectory() {
    std::wstring s(32768, 0);
    s.resize(GetModuleFileNameW(nullptr, s.data(), 32768));
    return fs::path(s).parent_path();
}
fs::path SystemExecutable(const wchar_t* name) {
    wchar_t s[MAX_PATH]{};
    GetSystemDirectoryW(s, MAX_PATH);
    return fs::path(s) / name;
}
std::wstring QuoteArgument(const std::wstring& s) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : s) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'\"')
            out.append(slashes * 2 + 1, L'\\');
        else
            out.append(slashes, L'\\');
        out += c;
        slashes = 0;
    }
    out.append(slashes * 2, L'\\');
    out += L'\"';
    return out;
}
std::wstring CommandLine(const fs::path& exe, const std::vector<std::wstring>& args) {
    std::wstring cmd = QuoteArgument(exe.wstring());
    for (const auto& a : args)
        cmd += L" " + QuoteArgument(a);
    return cmd;
}
Logger::Logger() {
    auto dir = DataDirectory() / L"logs";
    fs::create_directories(dir);
    SYSTEMTIME t{};
    GetLocalTime(&t);
    wchar_t name[100]{};
    swprintf_s(name, L"LogForge-%04u%02u%02u-%02u%02u%02u-%lu.log", t.wYear, t.wMonth, t.wDay, t.wHour,
               t.wMinute, t.wSecond, GetCurrentProcessId());
    path_ = dir / name;
    file_.open(path_, std::ios::app);
    if (!file_)
        throw std::runtime_error("Cannot create local log file.");
    Write(std::string("LogForge ") + Version + "; Windows x64");
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    auto fn =
        reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof(v);
    if (fn && fn(&v) == 0)
        Write("Windows " + std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion) +
              " build " + std::to_string(v.dwBuildNumber));
}
void Logger::Write(const std::string& text) {
    std::lock_guard lock(mutex_);
    SYSTEMTIME t{};
    GetLocalTime(&t);
    file_ << std::setfill('0') << std::setw(2) << t.wHour << ':' << std::setw(2) << t.wMinute << ':'
          << std::setw(2) << t.wSecond << ' ' << text << '\n';
    file_.flush();
}
FFmpegProcess::FFmpegProcess(const fs::path& exe, const std::vector<std::wstring>& args, bool pipeIn) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE a{}, b{};
    auto make = [&](Handle& parent, Handle& child, bool readParent) {
        if (!CreatePipe(&a, &b, &sa, 1 << 20))
            throw std::runtime_error("Cannot create process pipe: " + Utf8(WinError()));
        parent.reset(readParent ? a : b);
        child.reset(readParent ? b : a);
        if (!SetHandleInformation(parent.get(), HANDLE_FLAG_INHERIT, 0))
            throw std::runtime_error("Cannot protect process pipe.");
    };
    Handle childIn, childOut, childErr;
    make(output_, childOut, true);
    make(error_, childErr, true);
    if (pipeIn)
        make(input_, childIn, false);
    else
        childIn.reset(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                  OPEN_EXISTING, 0, nullptr));
    if (!childIn)
        throw std::runtime_error("Cannot open process input.");
    job_.reset(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job_ ||
        !SetInformationJobObject(job_.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        throw std::runtime_error("Cannot create child process job.");
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<unsigned char> storage(size);
    auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &size))
        throw std::runtime_error("Cannot initialize process attributes.");
    struct Cleanup {
        LPPROC_THREAD_ATTRIBUTE_LIST p;
        ~Cleanup() {
            DeleteProcThreadAttributeList(p);
        }
    } cleanup{attributes};
    HANDLE inherited[]{childIn.get(), childOut.get(), childErr.get()};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                   sizeof(inherited), nullptr, nullptr))
        throw std::runtime_error("Cannot restrict inherited handles.");
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = childIn.get();
    si.StartupInfo.hStdOutput = childOut.get();
    si.StartupInfo.hStdError = childErr.get();
    si.lpAttributeList = attributes;
    PROCESS_INFORMATION pi{};
    auto cmd = CommandLine(exe, args);
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                        &si.StartupInfo, &pi))
        throw std::runtime_error("Cannot start " + PathText(exe) + ": " + Utf8(WinError()));
    process_.reset(pi.hProcess);
    Handle thread(pi.hThread);
    if (!AssignProcessToJobObject(job_.get(), process_.get())) {
        TerminateProcess(process_.get(), 1);
        throw std::runtime_error("Cannot supervise child process.");
    }
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        Terminate();
        throw std::runtime_error("Cannot resume child process.");
    }
}
FFmpegProcess::~FFmpegProcess() {
    if (Running())
        Terminate();
    if (process_)
        WaitForSingleObject(process_.get(), 5000);
}
size_t FFmpegProcess::Read(void* data, size_t bytes) {
    DWORD got = 0;
    if (!ReadFile(output_.get(), data, static_cast<DWORD>(std::min<size_t>(bytes, 1 << 20)), &got, nullptr)) {
        if (GetLastError() != ERROR_BROKEN_PIPE)
            throw std::runtime_error("Cannot read child process output.");
    }
    return got;
}
void FFmpegProcess::Write(const void* data, size_t bytes) {
    auto p = static_cast<const unsigned char*>(data);
    while (bytes) {
        DWORD n{};
        if (!WriteFile(input_.get(), p, static_cast<DWORD>(std::min<size_t>(bytes, 1 << 20)), &n, nullptr) ||
            !n)
            throw std::runtime_error("Encoder closed its input pipe. See the detailed log.");
        p += n;
        bytes -= n;
    }
}
void FFmpegProcess::CloseInput() {
    input_.reset();
}
void FFmpegProcess::Terminate() noexcept {
    if (job_)
        TerminateJobObject(job_.get(), 130);
}
bool FFmpegProcess::Running() const {
    return process_ && WaitForSingleObject(process_.get(), 0) == WAIT_TIMEOUT;
}
int FFmpegProcess::Wait() {
    WaitForSingleObject(process_.get(), INFINITE);
    DWORD c{};
    GetExitCodeProcess(process_.get(), &c);
    return static_cast<int>(c);
}
void ReadLines(HANDLE pipe, const LineCallback& fn) {
    std::string line;
    std::array<char, 8192> buffer{};
    DWORD n{};
    while (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &n, nullptr) && n) {
        for (DWORD i = 0; i < n; ++i) {
            char c = buffer[i];
            if (c == '\n' || c == '\r') {
                if (!line.empty()) {
                    fn(line);
                    line.clear();
                }
            } else if (line.size() < 65536)
                line += c;
        }
    }
    if (!line.empty())
        fn(line);
}
ProcessResult RunProcess(const fs::path& exe, const std::vector<std::wstring>& args,
                         const std::atomic_bool* cancel, int timeout, const LineCallback& fn) {
    FFmpegProcess p(exe, args);
    ProcessResult result;
    std::exception_ptr readError;
    std::jthread out([&] {
        try {
            std::array<char, 16384> b{};
            std::string pending;
            size_t n;
            while ((n = p.Read(b.data(), b.size()))) {
                if (result.output.size() + n <= 32 * 1024 * 1024)
                    result.output.append(b.data(), n);
                else if (!fn)
                    throw std::runtime_error("Process output exceeded 32 MB safety limit.");
                if (fn) {
                    pending.append(b.data(), n);
                    size_t pos;
                    while ((pos = pending.find('\n')) != std::string::npos) {
                        fn(pending.substr(0, pos));
                        pending.erase(0, pos + 1);
                    }
                }
            }
            if (fn && !pending.empty())
                fn(pending);
        } catch (...) {
            readError = std::current_exception();
            p.Terminate();
        }
    });
    std::jthread err([&] {
        ReadLines(p.ErrorPipe(), [&](const auto& line) {
            if (result.error.size() < 2 * 1024 * 1024)
                result.error += line + '\n';
        });
    });
    const auto start = std::chrono::steady_clock::now();
    bool timedOut = false;
    while (p.Running()) {
        if (cancel && cancel->load()) {
            p.Terminate();
            break;
        }
        if (timeout > 0 && std::chrono::steady_clock::now() - start > std::chrono::seconds(timeout)) {
            timedOut = true;
            p.Terminate();
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    result.exitCode = p.Wait();
    out.join();
    err.join();
    if (readError)
        std::rethrow_exception(readError);
    if (cancel && cancel->load())
        throw std::runtime_error("用户取消。");
    if (timedOut)
        throw std::runtime_error("External process timed out: " + PathText(exe));
    return result;
}
std::string SHA256(const fs::path& path) {
    BCRYPT_ALG_HANDLE alg{};
    BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA-256 initialization failed.");
    struct Cleanup {
        BCRYPT_ALG_HANDLE& a;
        BCRYPT_HASH_HANDLE& h;
        ~Cleanup() {
            if (h)
                BCryptDestroyHash(h);
            if (a)
                BCryptCloseAlgorithmProvider(a, 0);
        }
    } cleanup{alg, hash};
    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) < 0)
        throw std::runtime_error("SHA-256 initialization failed.");
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot read file for SHA-256 verification.");
    std::array<unsigned char, 65536> b{};
    while (file) {
        file.read(reinterpret_cast<char*>(b.data()), b.size());
        if (BCryptHashData(hash, b.data(), static_cast<ULONG>(file.gcount()), 0) < 0)
            throw std::runtime_error("SHA-256 failed.");
    }
    if (!file.eof())
        throw std::runtime_error("Cannot finish reading checksum input.");
    std::array<unsigned char, 32> digest{};
    if (BCryptFinishHash(hash, digest.data(), 32, 0) < 0)
        throw std::runtime_error("SHA-256 failed.");
    std::ostringstream s;
    for (auto v : digest)
        s << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(v);
    return s.str();
}
} // namespace logforge
