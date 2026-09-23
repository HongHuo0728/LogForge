#include "logforge/Media.h"
#include "logforge/ToolTrust.h"
#include <charconv>
#include <cmath>
#include <map>
#include <numeric>

namespace logforge {
Json CadenceReport::ToJson() const {
    return {{"verified", verified},
            {"verified_cadence", Utf8(rate.Text())},
            {"fps", rate.Value()},
            {"packets", packets},
            {"error_packet", errorPacket},
            {"error", error},
            {"error_units", "video time-base ticks"},
            {"max_interval_error", maxIntervalError},
            {"max_phase_error", maxPhaseError},
            {"max_duration_error", maxDurationError}};
}
CadenceReport AnalyzeCadence(std::span<const VideoPacketTiming> p, const MediaInfo& m) {
    CadenceReport r;
    r.packets = static_cast<int64_t>(p.size());
    auto fail = [&](size_t index, const std::string& message) {
        if (r.error.empty()) {
            r.errorPacket = static_cast<int64_t>(index);
            r.error = message;
        }
    };
    if (p.empty() || m.timeBase.Value() <= 0) {
        r.error = "No packets or invalid time base";
        return r;
    }
    std::map<int64_t, size_t> durations;
    bool fixed = true;
    for (size_t i = 0; i < p.size(); ++i) {
        if (p[i].duration <= 0)
            fail(i, "Missing or non-positive duration");
        ++durations[p[i].duration];
        if (p[i].duration != p[0].duration)
            fixed = false;
        if (i) {
            if (p[i].pts <= p[i - 1].pts)
                fail(i, "Duplicate or backwards PTS");
            const long double delta = static_cast<long double>(static_cast<uint64_t>(p[i].pts) -
                                                               static_cast<uint64_t>(p[i - 1].pts));
            if (delta != p[0].duration)
                fixed = false;
            if (delta != p[i - 1].duration)
                fail(i, "Timestamp gap/overlap: PTS interval does not equal previous duration");
        }
    }
    if (!r.error.empty())
        return r;
    const auto modal = std::max_element(durations.begin(), durations.end(), [](const auto& a, const auto& b) {
                           return a.second < b.second;
                       })->first;
    // A constant packet clock is authoritative, including 29.99 and 29.98.
    // Nonuniform clocks are checked against the nominal candidate if it fits the
    // observed interval range, otherwise the dominant packet duration. Never fit
    // away sustained clock corrections by regressing the mean/endpoints.
    long double period = static_cast<long double>(modal);
    const auto reduce = std::gcd(m.timeBase.denominator, modal);
    if (modal / reduce > std::numeric_limits<int64_t>::max() / m.timeBase.numerator) {
        r.error = "Packet period cannot be represented safely";
        return r;
    }
    r.rate = {m.timeBase.denominator / reduce, m.timeBase.numerator * (modal / reduce)};
    if (!fixed && m.nominalFps.Value() > 0) {
        const long double candidate = 1.L / m.nominalFps.Value() / m.timeBase.Value();
        if (candidate >= durations.begin()->first && candidate <= durations.rbegin()->first) {
            period = candidate;
            r.rate = m.nominalFps;
        }
    }
    if (r.rate.denominator <= 0 || r.rate.Value() <= 0 || r.rate.Value() > 120) {
        r.error = "Invalid or unsupported verified frame rate";
        return r;
    }
    const auto gcd = std::gcd(r.rate.numerator, r.rate.denominator);
    r.rate.numerator /= gcd;
    r.rate.denominator /= gcd;
    constexpr long double tolerance = 1.05L; // Existing timestamp quantization budget, unchanged.
    for (size_t i = 0; i < p.size(); ++i) {
        const auto duration = std::abs(static_cast<long double>(p[i].duration) - period);
        const auto phase = std::abs(
            static_cast<long double>(static_cast<uint64_t>(p[i].pts) - static_cast<uint64_t>(p[0].pts)) -
            i * period);
        const auto interval = i ? std::abs(static_cast<long double>(static_cast<uint64_t>(p[i].pts) -
                                                                    static_cast<uint64_t>(p[i - 1].pts)) -
                                           period)
                                : 0;
        r.maxDurationError = std::max(r.maxDurationError, static_cast<double>(duration));
        r.maxIntervalError = std::max(r.maxIntervalError, static_cast<double>(interval));
        r.maxPhaseError = std::max(r.maxPhaseError, static_cast<double>(phase));
        if (duration > tolerance)
            fail(i, "Duration error " + std::to_string(static_cast<double>(duration)) + " ticks");
        if (interval > tolerance)
            fail(i, "Interval error " + std::to_string(static_cast<double>(interval)) + " ticks");
        if (phase > tolerance)
            fail(i, "Cumulative phase error " + std::to_string(static_cast<double>(phase)) + " ticks");
    }
    r.verified = r.error.empty();
    return r;
}
CadenceReport VerifyConstantFrameRate(const fs::path& ffprobe, const MediaInfo& m,
                                      const std::atomic_bool& cancel, bool rejectInvalid) {
    auto lease = ToolTrust::Acquire(ffprobe.parent_path() / L"ffmpeg.exe");
    if (fs::weakly_canonical(ffprobe) != lease->identity.ffprobe)
        throw AppError(Message(TextId::FFmpegUntrusted, {PathText(ffprobe)}));
    std::vector<VideoPacketTiming> packets;
    std::string parseError;
    auto result = RunProcess(
        lease->identity.ffprobe,
        {L"-v", L"error", L"-select_streams", L"v:0", L"-show_packets", L"-show_entries",
         L"packet=pts,duration", L"-of", L"compact=p=0:nk=0", m.path.wstring()},
        &cancel, 0, [&](const std::string& line) {
            VideoPacketTiming p;
            bool pts = false, duration = false;
            size_t at = 0;
            while (at < line.size()) {
                const auto end = line.find('|', at);
                auto field = line.substr(at, end == std::string::npos ? end : end - at);
                if (!field.empty() && field.back() == '\r')
                    field.pop_back();
                const auto eq = field.find('=');
                if (eq != std::string::npos) {
                    const auto key = field.substr(0, eq), value = field.substr(eq + 1);
                    if (key == "pts" || key == "duration") {
                        int64_t n = 0;
                        const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), n);
                        if (ec == std::errc() && ptr == value.data() + value.size()) {
                            if (key == "pts") {
                                p.pts = n;
                                pts = true;
                            } else {
                                p.duration = n;
                                duration = true;
                            }
                        }
                    }
                }
                if (end == std::string::npos)
                    break;
                at = end + 1;
            }
            if (!pts || !duration) {
                if (parseError.empty())
                    parseError = "Packet " + std::to_string(packets.size()) + ": missing PTS/duration";
            }
            packets.push_back(p);
        });
    auto r = AnalyzeCadence(packets, m);
    if (result.exitCode || !parseError.empty()) {
        r.verified = false;
        r.error += " " + parseError + result.error;
    }
    if (r.verified && std::abs(static_cast<double>(r.packets) / r.rate.Value() - m.videoDuration) >
                          std::max(.05, 2 / r.rate.Value())) {
        r.verified = false;
        r.error = "Packet count/cadence disagrees with video duration";
    }
    if (rejectInvalid && !r.verified)
        throw AppError(Message(TextId::InputCadenceDetail,
                               {"packet " + std::to_string(r.errorPacket) + ": " + r.error}));
    return r;
}
} // namespace logforge
