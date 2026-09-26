#include "logforge/Queue.h"
namespace logforge {
Json RunQueue(const FFmpegInstallation& tools, const std::vector<fs::path>& inputs, const fs::path& directory,
              Logger& logger, const std::atomic_bool& cancel, const TranscodeOptions& options,
              const std::string& explicitChroma, const QueueCallback& progress) {
    Json results = Json::array();
    size_t successful = 0;
    for (size_t i = 0; i < inputs.size() && !cancel; ++i) {
        const auto output = directory / (inputs[i].stem().wstring() + L"_AppleLog.mov");
        Json entry{{"input", PathText(inputs[i])}, {"output", PathText(output)}, {"passed", false}};
        try {
            auto media = Probe(tools.ffprobe, inputs[i], &cancel);
            media.inputChromaOverride = explicitChroma;
            progress(i, inputs.size(), media, {});
            auto report = TranscodeJob::Run(
                tools, media, output, logger, cancel,
                [&](const auto& p) { progress(i, inputs.size(), media, p); }, options);
            entry["passed"] = report.passed;
            entry["validation"] = report.ToJson();
            ++successful;
        } catch (const std::exception& error) {
            entry["error"] = error.what();
            entry["cancelled"] = cancel.load();
            // Even inputs rejected before encoding get their own report. A
            // report-write failure is an item failure and never skips the rest.
            try {
                const auto path =
                    DataDirectory() / L"logs" /
                    (L"LogForge-queue-failure-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                     std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(i) + L".validation.json");
                std::ofstream f(path);
                f << entry.dump(2);
                f.close();
                if (!f)
                    throw AppError(TextId::ReportSave);
                entry["failure_report"] = PathText(path);
            } catch (const std::exception& e) {
                entry["report_error"] = e.what();
            }
        }
        results.push_back(entry);
    }
    return {{"items", results},
            {"successful", successful},
            {"failed", results.size() - successful},
            {"remaining", inputs.size() - results.size()},
            {"cancelled", cancel.load()}};
}
} // namespace logforge
