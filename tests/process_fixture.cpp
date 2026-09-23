#include <filesystem>
#include <string>
#include <windows.h>

void Write(HANDLE handle, const std::string& text) {
    DWORD done{};
    WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &done, nullptr);
}
int wmain(int argc, wchar_t** argv) {
    const std::wstring mode = argc > 1 ? argv[1] : L"";
    if (mode == L"hold" || mode == L"wait") {
        Sleep(30000);
        return 0;
    }
    if (mode == L"heldpipe") {
        wchar_t exe[32768]{};
        GetModuleFileNameW(nullptr, exe, 32768);
        std::wstring command = L"\"" + std::wstring(exe) + L"\" hold";
        STARTUPINFOW si{sizeof(si)};
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(exe, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                            &si, &pi))
            return 2;
        Write(si.hStdOutput, std::to_string(pi.dwProcessId) + "\n");
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 0;
    }
    if (mode == L"stdout-long" || mode == L"stderr-long") {
        const auto stream = GetStdHandle(mode == L"stdout-long" ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
        for (int i = 0; i < 1024; ++i)
            Write(stream, std::string(8192, 'x'));
        return 0;
    }
    if (mode == L"--internal-discover") {
        Write(GetStdHandle(STD_OUTPUT_HANDLE),
              "{\"type\":\"candidate\",\"candidate\":{\"path\":\"D:/test/ffmpeg.exe\",\"ffprobe_path\":\"D:/"
              "test/ffprobe.exe\",\"source\":\"test\",\"paired\":false}}\n");
        Sleep(30000);
        return 0;
    }
    if (mode == L"line") {
        Write(GetStdHandle(STD_OUTPUT_HANDLE), "line\n");
        return 0;
    }
    return 77;
}
