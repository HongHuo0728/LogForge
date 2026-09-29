#include "logforge/AudioPayload.h"
#include <algorithm>
#include <sstream>
namespace logforge {
Json VerifyAudioPayload(const fs::path& ffmpeg, const fs::path& input, const fs::path& output,
                        size_t count, const std::atomic_bool& cancel) {
    Json report{{"audio_stream_copy_declared", true}, {"audio_payload_verified", false},
                {"passed", true}, {"status", count ? "pending" : "no_audio"},
                {"method", "SHA-256 per stream, concatenated copied packet bytes; no decoding"},
                {"timestamps_hashed", false}, {"streams", Json::array()}};
    if (!count) return report;
    const auto digests = [&](const fs::path& path) {
        const auto result = RunProcess(ffmpeg, {L"-v", L"error", L"-nostdin", L"-guess_layout_max", L"0",
            L"-i", path.wstring(), L"-map", L"0:a", L"-vn", L"-sn", L"-dn", L"-c:a", L"copy",
            L"-f", L"streamhash", L"-hash", L"sha256", L"pipe:1"}, &cancel, 0);
        if (cancel) throw AppError(TextId::Cancelled);
        if (result.exitCode) throw std::runtime_error("Audio streamhash failed: " + result.error);
        std::istringstream lines(result.output);
        std::vector<std::string> hashes;
        std::string line;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            const auto prefix = std::to_string(hashes.size()) + ",a,SHA256=";
            if (!line.starts_with(prefix) || line.size() != prefix.size() + 64 ||
                !std::all_of(line.begin() + static_cast<std::ptrdiff_t>(prefix.size()), line.end(),
                             [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                                                 (c >= 'A' && c <= 'F'); }))
                throw std::runtime_error("Malformed audio streamhash output");
            hashes.push_back(line.substr(prefix.size()));
        }
        if (hashes.size() != count) throw std::runtime_error("Audio payload stream count mismatch");
        return hashes;
    };
    try {
        const auto before = digests(input), after = digests(output);
        bool equal = true;
        for (size_t i = 0; i < count; ++i) {
            const bool same = before[i] == after[i];
            equal = equal && same;
            report["streams"].push_back({{"audio_index", i}, {"input_sha256", before[i]},
                                          {"output_sha256", after[i]}, {"equal", same}});
        }
        report["passed"] = report["audio_payload_verified"] = equal;
        report["status"] = equal ? "verified" : "payload_mismatch";
    } catch (const AppError&) { throw; }
    catch (const std::exception& e) {
        report["passed"] = false;
        report["status"] = "verification_failed";
        report["error"] = e.what();
    }
    return report;
}
}
