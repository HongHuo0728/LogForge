#include "logforge/Media.h"
#include <cmath>
#include <iostream>
#include <limits>

using namespace logforge;
namespace {
void Require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class Clock> std::vector<VideoPacketTiming> Packets(size_t count, Clock clock) {
    std::vector<VideoPacketTiming> result;
    for (size_t i = 0; i < count; ++i)
        result.push_back({clock(i), clock(i + 1) - clock(i)});
    return result;
}
void Invalid(const std::vector<VideoPacketTiming>& p, const MediaInfo& m, const char* what) {
    const auto r = AnalyzeCadence(p, m);
    Require(!r.verified && r.classification == "invalid" && r.errorPacket >= 0, what);
}
} // namespace
int main() {
    try {
        MediaInfo m;
        m.timeBase = {1, 1200};
        m.nominalFps = {60000, 1001};
        m.averageFps = {309 * 1200, 6183};
        // Reconstruct the reported numbers exactly: 309 packets / 6183 ticks,
        // avg=59.970887918..., nominal=59.94005994..., packet53 error=1.06.
        auto p = Packets(309, [](size_t i) { return static_cast<int64_t>(i * 2061 / 103); });
        Require(std::abs(p[53].pts - 53 * 20.02L) > 1.05L, "Regression does not trigger the old gate");
        Require(std::abs(std::abs(p[53].pts - 53 * 20.02L) - 1.06L) < 1e-9,
                "Reported packet53 phase was not reproduced");
        auto r = AnalyzeCadence(p, m);
        Require(r.verified && r.classification == "quantized-fixed", "Reported 59.94 clip rejected");
        Require(std::abs(r.rate.Value() - m.averageFps.Value()) < 1e-10, "Inferred capture clock incorrect");
        Require(r.quantizationSpanTicks <= 1 && r.maxNominalPhaseError > 3,
                "Inference hid the nominal clock disagreement");
        const auto json = r.ToJson();
        Require(json.at("time_base") == "1/1200" &&
                    json.at("candidate_source") == "packet_quantization_envelope" &&
                    json.contains("max_phase_error_seconds") &&
                    json.contains("max_phase_error_frame_fraction"),
                "Cadence report lacks quantization diagnostics");
        std::cout << "Reported 59.94 regression: " << json.dump() << '\n';

        // Floor AND ceil, arbitrary quantizer phase, truncated final cycle, large
        // negative initial PTS. None of these uses avg metadata as an authority.
        m.averageFps = {30, 1};
        for (int64_t denominator : {7, 29, 103, 1001}) {
            for (int64_t phase : {0, 1, 3}) {
                const int64_t numerator = 20 * denominator + denominator / 3;
                const auto offset = std::numeric_limits<int64_t>::min() + 100;
                p = Packets(317, [&](size_t i) {
                    return offset + (static_cast<int64_t>(i) * numerator + phase) / denominator;
                });
                Require(AnalyzeCadence(p, m).verified, "Phase-shifted floor quantization rejected");
                p = Packets(317, [&](size_t i) {
                    return offset + (static_cast<int64_t>(i) * numerator + denominator - 1) / denominator;
                });
                Require(AnalyzeCadence(p, m).verified, "Ceil quantization rejected");
            }
        }
        // Duration histogram and average are insufficient: all of these are
        // contiguous positive20/21-tick sequences, but have no fixed clock strip.
        p = Packets(4000,
                    [](size_t i) { return static_cast<int64_t>(std::floor(20.01L * i + .000002L * i * i)); });
        Invalid(p, m, "Accelerating clock drift fitted away");
        p = Packets(4000, [](size_t i) {
            return static_cast<int64_t>(std::floor(i < 2000 ? 20.005L * i : 40010.L + 20.015L * (i - 2000)));
        });
        Invalid(p, m, "Piecewise-rate VFR fitted away");
        p = Packets(420, [](size_t i) {
            return static_cast<int64_t>(20 * i + std::min<size_t>(i > 200 ? i - 200 : 0, 10));
        });
        Invalid(p, m, "Clustered corrections with plausible average accepted");
        p = Packets(309, [](size_t i) { return static_cast<int64_t>(i * 2061 / 103); });
        p.back().duration += 5;
        Invalid(p, m, "Corrupt final duration accepted");
        p.back().duration = 0;
        Invalid(p, m, "Missing duration accepted");
        p.back().duration = -1;
        Invalid(p, m, "Negative duration accepted");
        p = Packets(309, [](size_t i) { return static_cast<int64_t>(i * 2061 / 103); });
        p[53].pts += 1;
        Invalid(p, m, "Gap/overlap accepted");
        p[53].pts = p[52].pts;
        Invalid(p, m, "Duplicate timestamp accepted");
        p[53].pts = p[52].pts - 1;
        Invalid(p, m, "Backwards timestamp accepted");

        // Integer nominal tags cannot veto fixed fractional clocks either.
        m.timeBase = {1, 30000};
        m.nominalFps = {30, 1};
        for (int64_t rate : {2999, 2998, 299701}) {
            const int64_t scale = rate == 299701 ? 10000 : 100;
            p = Packets(10000, [&](size_t i) {
                return static_cast<int64_t>(i) * 30000 * scale / rate;
            });
            Require(AnalyzeCadence(p, m).verified, "Integer nominal vetoed fractional fixed cadence");
        }
        m.timeBase = {1, 1200};
        m.nominalFps = {60000, 1001};
        // O(n log n) envelope implementation also checks long recordings.
        p = Packets(120000, [](size_t i) { return static_cast<int64_t>(i * 2061 / 103); });
        Require(AnalyzeCadence(p, m).verified, "Long quantized-fixed recording rejected");
        // A large integer period must not expand the one-tick cell through
        // floating-point cancellation or a span-scaled precision allowance.
        m.timeBase = {1, 600000000000000};
        constexpr int64_t hugePeriod = 10000000000000;
        p = Packets(3000, [](size_t i) {
            return static_cast<int64_t>(i) * hugePeriod + static_cast<int64_t>(i / 103);
        });
        Require(AnalyzeCadence(p, m).verified, "Fine time-base integer cancellation lost precision");
        p = Packets(3000, [](size_t i) {
            return static_cast<int64_t>(i) * hugePeriod +
                   static_cast<int64_t>(std::min<size_t>(i > 1200 ? i - 1200 : 0, 10));
        });
        Invalid(p, m, "Huge clockspan widened quantization acceptance");
        std::cout
            << "PASS: quantized fixed cadence, full-sequence drift/VFR negatives, endpoint and PTS checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
