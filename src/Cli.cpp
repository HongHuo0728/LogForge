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
    Language language = Language::English;
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(Ctrl, TRUE);
    try {
        if (argc > 1 && std::wstring(argv[1]) == L"--internal-discover")
            return RunDiscoveryHelper(std::vector<std::wstring>(argv + 1, argv + argc));
        Logger log;
        std::vector<std::wstring> a;
        fs::path explicitFFmpeg;
        std::string inputChroma;
        int64_t cancelAfterFrames = 0;
        TranscodeOptions options;
        bool toneParameter = false;
        bool deepSearch = false;
        for (int i = 1; i < argc; ++i) {
            if (std::wstring(argv[i]) == L"--deep-search")
                deepSearch = true;
            else if (std::wstring(argv[i]) == L"--language" && i + 1 < argc) {
                const std::wstring value = argv[++i];
                if (value == L"zh-CN")
                    language = Language::SimplifiedChinese;
                else if (value == L"en")
                    language = Language::English;
                else
                    throw AppError(TextId::CLILanguage);
            } else if (std::wstring(argv[i]) == L"--ffmpeg" && i + 1 < argc)
                explicitFFmpeg = argv[++i];
            else if (std::wstring(argv[i]) == L"--input-chroma-location" && i + 1 < argc) {
                inputChroma = Utf8(argv[++i]);
                if (inputChroma != "left" && inputChroma != "center")
                    throw AppError(TextId::InputChroma);
            } else if (std::wstring(argv[i]) == L"--cancel-after-frames" && i + 1 < argc)
                cancelAfterFrames = std::stoll(argv[++i]);
            else if (std::wstring(argv[i]) == L"--exposure-ev" && i + 1 < argc) {
                std::wstring value = argv[++i];
                size_t consumed = 0;
                options.exposureStops = std::stod(value, &consumed);
                if (consumed != value.size())
                    throw AppError(TextId::CLIExposure);
            } else if (std::wstring(argv[i]) == L"--tone") {
                options.tone.enabled = true;
            } else if ((std::wstring(argv[i]) == L"--shadow-lift-ev" ||
                        std::wstring(argv[i]) == L"--highlight-compression-ev" ||
                        std::wstring(argv[i]) == L"--saturation-percent") &&
                       i + 1 < argc) {
                const std::wstring key = argv[i], value = argv[++i];
                size_t consumed = 0;
                const double number = std::stod(value, &consumed);
                if (consumed != value.size())
                    throw AppError(TextId::CLITone);
                if (key == L"--shadow-lift-ev")
                    options.tone.shadowStops = number;
                else if (key == L"--highlight-compression-ev")
                    options.tone.highlightStops = number;
                else
                    options.tone.saturation = number / 100;
                toneParameter = true;
            } else
                a.emplace_back(argv[i]);
        }
        if (toneParameter && !options.tone.enabled)
            throw AppError(TextId::CLIEnableTone);
        ValidateToneAdjustments(options.tone);
        if (a.empty() || a[0] == L"--help" || a[0] == L"--version") {
            std::cout << "LogForge " << DisplayVersion << "\n";
            if (!a.empty() && a[0] == L"--version")
                return 0;
            std::cout << "--detect [--deep-search] | --check-compatible --ffmpeg PATH | --install-ffmpeg | "
                         "--approve-ffmpeg "
                         "--ffmpeg PATH | --probe INPUT | --convert INPUT "
                         "OUTPUT | --analyze INPUT [REFERENCE]\n"
                      << Translate(TextId::CLIOptional, language)
                      << ": --ffmpeg PATH_TO_FFMPEG_EXE "
                         "--language en|zh-CN --exposure-ev STOPS --input-chroma-location left|center\n"
                      << Translate(TextId::CLICreative, language)
                      << ": --tone [--shadow-lift-ev 3] "
                         "[--highlight-compression-ev 1] [--saturation-percent 85]\n";
            return 0;
        }
        // Reject malformed commands before discovering or executing any tools.
        const bool single = a.size() == 1 && (a[0] == L"--detect" || a[0] == L"--check-compatible" ||
                                              a[0] == L"--install-ffmpeg" || a[0] == L"--approve-ffmpeg");
        const bool command = single || (a.size() == 2 && a[0] == L"--probe") ||
                             (a.size() == 3 && a[0] == L"--convert") ||
                             ((a.size() == 2 || a.size() == 3) && a[0] == L"--analyze");
        if (!command || (deepSearch && (a[0] != L"--detect" || !explicitFFmpeg.empty())) ||
            (a[0] == L"--check-compatible" && explicitFFmpeg.empty()))
            throw AppError(TextId::CLICommand);
        FFmpegManager manager(log);
        std::optional<FFmpegInstallation> tools;
        if (a[0] == L"--approve-ffmpeg") {
            if (explicitFFmpeg.empty())
                throw AppError(TextId::CLICommand);
            const auto identity = ToolTrust::Inspect(explicitFFmpeg);
            ToolTrust::ApproveManual(identity);
            manager.SaveManual(identity.ffmpeg);
            std::cout << "Explicit execution approval recorded:\n" << identity.ToJson().dump(2) << '\n';
            return 0;
        }
        if (a[0] == L"--install-ffmpeg") {
            GyanReleaseProvider provider;
            tools = FFmpegDownloader::Install(
                provider, log, cancelled, [&](auto n, auto total, const auto& phase) {
                    static uint64_t last = 0;
                    if (!total || n == total || n - last > 4 * 1024 * 1024) {
                        std::cout << Translate(phase, language) << " " << n << "/" << total << '\n';
                        last = n;
                    }
                });
            std::cout << tools->version << '\n';
            return 0;
        }
        tools = explicitFFmpeg.empty()
                    ? manager.Detect(cancelled, {}, {deepSearch ? DiscoveryMode::Deep : DiscoveryMode::Quick})
                    : std::optional(manager.Check(explicitFFmpeg, cancelled, a[0] != L"--check-compatible"));
        if (!tools) {
            if (a[0] == L"--detect")
                std::cout << manager.Discovery().ToJson().dump(2) << '\n';
            if (cancelled)
                throw AppError(TextId::Cancelled);
            for (const auto& path : manager.UnapprovedCandidates())
                std::cerr << "Discovered, not executed: " << PathText(path) << '\n';
            throw AppError(TextId::FFmpegNotFoundCLI);
        }
        if (a[0] == L"--detect" || a[0] == L"--check-compatible") {
            auto result = tools->ToJson();
            if (explicitFFmpeg.empty())
                result["discovery"] = manager.Discovery().ToJson();
            std::cout << result.dump(2) << '\n';
            return 0;
        }
        if (a[0] == L"--probe" && a.size() == 2) {
            auto m = Probe(tools->ffprobe, a[1], &cancelled);
            m.inputChromaOverride = inputChroma;
            std::cout << m.raw.dump(2) << '\n';
            auto errors = m.UnsupportedReasons();
            for (const auto& e : errors)
                std::cerr << Translate(e, language) << '\n';
            return errors.empty() ? 0 : 2;
        }
        if (a[0] == L"--convert" && a.size() == 3) {
            auto m = Probe(tools->ffprobe, a[1], &cancelled);
            m.inputChromaOverride = inputChroma;
            auto report = TranscodeJob::Run(
                *tools, m, a[2], log, cancelled,
                [&](const auto& p) {
                    std::cout << Translate(p.stage, language) << " frame=" << p.frame << " time=" << p.seconds
                              << " percent=" << p.fraction * 100 << '\n';
                    if (cancelAfterFrames > 0 && p.frame >= cancelAfterFrames)
                        cancelled = true;
                },
                options);
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
                                  {"diff", ReferenceMovAnalyzer::SemanticDiff(first, second)}}
                                 .dump(2)
                          << '\n';
            } else
                std::cout << first.dump(2) << '\n';
            return 0;
        }
        throw AppError(TextId::CLICommand);
    } catch (const logforge::AppError& e) {
        std::cerr << "ERROR[" << logforge::MessageKey(e.message.id)
                  << "]: " << logforge::Describe(e.message, e.details, language) << '\n';
        return cancelled ? 130 : 1;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return cancelled ? 130 : 1;
    }
}
