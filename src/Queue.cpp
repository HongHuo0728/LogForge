#include "logforge/Queue.h"
#include <set>
namespace logforge {
QueuePlan PlanQueue(const std::vector<fs::path>& inputs, const fs::path& directory) {
    const auto dir = fs::absolute(directory).lexically_normal();
    if (!fs::is_directory(dir))
        throw AppError(TextId::OutputDirectory);
    const auto less = [](const std::wstring& a, const std::wstring& b) {
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    };
    std::set<std::wstring, decltype(less)> reserved(less);
    QueuePlan plan;
    for (const auto& input : inputs) {
        const auto stem = input.stem().wstring() + L"_AppleLog";
        for (uint64_t n = 1;; ++n) {
            const auto name = stem + (n == 1 ? L"" : L"_" + std::to_wstring(n)) + L".mov";
            const auto output = dir / name;
            if (!reserved.contains(name) && !fs::exists(output)) {
                reserved.insert(name);
                plan.push_back({fs::absolute(input).lexically_normal(), output});
                break;
            }
        }
    }
    return plan;
}
Json QueuePlanJson(const QueuePlan& plan) {
    Json result = Json::array();
    for (const auto& item : plan)
        result.push_back({{"input", PathText(item.input)}, {"output", PathText(item.output)}});
    return result;
}
Json RunQueue(const FFmpegInstallation& tools, const std::vector<fs::path>& inputs, const fs::path& directory,
              Logger& logger, const std::atomic_bool& cancel, const TranscodeOptions& options,
              const std::string& explicitChroma, const QueueCallback& progress) {
    return RunQueue(tools, PlanQueue(inputs, directory), logger, cancel, options, explicitChroma, progress);
}
Json RunQueue(const FFmpegInstallation& tools, const QueuePlan& plan, Logger& logger,
              const std::atomic_bool& cancel, const TranscodeOptions& options,
              const std::string& explicitChroma, const QueueCallback& progress) {
    Json results = Json::array();
    size_t successful = 0;
    for (size_t i = 0; i < plan.size() && !cancel; ++i) {
        const auto& output = plan[i].output;
        Json entry{{"input", PathText(plan[i].input)}, {"output", PathText(output)}, {"passed", false}};
        try {
            auto media = Probe(tools.ffprobe, plan[i].input, &cancel);
            media.inputChromaOverride = explicitChroma;
            media.forceBT2020Interpretation = options.forceBT2020Interpretation;
            progress(i, plan.size(), media, {});
            auto report = TranscodeJob::Run(
                tools, media, output, logger, cancel,
                [&](const auto& p) { progress(i, plan.size(), media, p); }, options);
            entry["passed"] = report.Completed();
            entry["validation"] = report.ToJson();
            if (report.Completed())
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
    return {{"plan", QueuePlanJson(plan)}, {"items", results},
            {"successful", successful},
            {"failed", results.size() - successful},
            {"remaining", plan.size() - results.size()},
            {"cancelled", cancel.load()}};
}
} // namespace logforge
