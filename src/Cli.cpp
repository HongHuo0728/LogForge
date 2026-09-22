#include "logforge/Transcode.h"
#include <fcntl.h>
#include <io.h>
#include <iostream>

namespace {
std::atomic_bool cancelled = false;
BOOL WINAPI Ctrl(DWORD) {
    cancelled = true;
    return TRUE;
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    using namespace logforge;
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(Ctrl, TRUE);
    try {
        Logger log;
        std::vector<std::wstring> a;
        fs::path explicitFFmpeg;
        int64_t cancelAfterFrames = 0;
        for (int i = 1; i < argc; ++i) {
            if (std::wstring(argv[i]) == L"--ffmpeg" && i + 1 < argc)
                explicitFFmpeg = argv[++i];
            else if (std::wstring(argv[i]) == L"--cancel-after-frames" && i + 1 < argc)
                cancelAfterFrames = std::stoll(argv[++i]);
            else
                a.emplace_back(argv[i]);
        }
        if (a.empty()) {
            std::cout << "LogForge 0.1.0\n--detect | --install-ffmpeg | --probe INPUT | --convert INPUT "
                         "OUTPUT | --analyze INPUT [REFERENCE]\nOptional: --ffmpeg PATH_TO_FFMPEG_EXE\n";
            return 0;
        }
        FFmpegManager manager(log);
        std::optional<FFmpegInstallation> tools;
        if (a[0] == L"--install-ffmpeg") {
            GyanReleaseProvider provider;
            tools = FFmpegDownloader::Install(
                provider, log, cancelled, [](auto n, auto total, const auto& phase) {
                    static uint64_t last = 0;
                    if (!total || n == total || n - last > 4 * 1024 * 1024) {
                        std::cout << Utf8(phase) << " " << n << "/" << total << '\n';
                        last = n;
                    }
                });
            std::cout << tools->version << '\n';
            return 0;
        }
        tools = explicitFFmpeg.empty() ? manager.Detect(cancelled)
                                       : std::optional(manager.Check(explicitFFmpeg, cancelled));
        if (!tools)
            throw std::runtime_error("未检测到 FFmpeg。运行 --install-ffmpeg 或使用 --ffmpeg 指定路径。");
        if (a[0] == L"--detect") {
            std::cout << tools->version << '\n' << PathText(tools->ffmpeg) << '\n';
            return 0;
        }
        if (a[0] == L"--probe" && a.size() == 2) {
            auto m = Probe(tools->ffprobe, a[1], &cancelled);
            std::cout << m.raw.dump(2) << '\n';
            auto errors = m.UnsupportedReasons();
            for (const auto& e : errors)
                std::cerr << e << '\n';
            return errors.empty() ? 0 : 2;
        }
        if (a[0] == L"--convert" && a.size() == 3) {
            auto m = Probe(tools->ffprobe, a[1], &cancelled);
            auto report = TranscodeJob::Run(*tools, m, a[2], log, cancelled, [&](const auto& p) {
                std::cout << Utf8(p.stage) << " frame=" << p.frame << " time=" << p.seconds
                          << " percent=" << p.fraction * 100 << '\n';
                if (cancelAfterFrames > 0 && p.frame >= cancelAfterFrames)
                    cancelled = true;
            });
            std::cout << report.ToJson().dump(2) << '\n';
            return report.passed ? 0 : 3;
        }
        if (a[0] == L"--analyze" && (a.size() == 2 || a.size() == 3)) {
            auto first = ReferenceMovAnalyzer::Analyze(a[1]);
            first["ffprobe"] = Probe(tools->ffprobe, a[1], &cancelled).raw;
            if (a.size() == 3) {
                auto second = ReferenceMovAnalyzer::Analyze(a[2]);
                second["ffprobe"] = Probe(tools->ffprobe, a[2], &cancelled).raw;
                std::cout << Json{{"first", first},
                                  {"reference", second},
                                  {"diff", Json::diff(first, second)}}
                                 .dump(2)
                          << '\n';
            } else
                std::cout << first.dump(2) << '\n';
            return 0;
        }
        throw std::runtime_error("Invalid command. Run without arguments for usage.");
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return cancelled ? 130 : 1;
    }
}
