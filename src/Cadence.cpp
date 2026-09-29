#include "logforge/Media.h"
#include "logforge/ToolTrust.h"
#include <charconv>
#include <cmath>
#include <iomanip>
#include <map>
#include <numeric>
#include <sstream>

namespace logforge {
namespace {
struct ClockPoint {
    int64_t frame, ticks;
};
long double Cross(const ClockPoint& a, const ClockPoint& b, const ClockPoint& c) {
    return static_cast<long double>(b.frame - a.frame) * (c.ticks - a.ticks) -
           static_cast<long double>(b.ticks - a.ticks) * (c.frame - a.frame);
}
std::vector<ClockPoint> Hull(const std::vector<ClockPoint>& points, bool upper) {
    std::vector<ClockPoint> result;
    for (const auto& p : points) {
        while (result.size() >= 2) {
            const auto cross = Cross(result[result.size() - 2], result.back(), p);
            if (upper ? cross < 0 : cross > 0)
                break;
            result.pop_back();
        }
        result.push_back(p);
    }
    return result;
}
long double Residual(const ClockPoint& p, long double period) {
    return static_cast<long double>(p.ticks) - p.frame * period;
}
long double Support(const std::vector<ClockPoint>& hull, long double period, bool upper) {
    size_t lo = 0, hi = hull.size() - 1;
    while (lo < hi) {
        const auto mid = lo + (hi - lo) / 2;
        const auto a = Residual(hull[mid], period), b = Residual(hull[mid + 1], period);
        if (upper ? a < b : a > b)
            lo = mid + 1;
        else
            hi = mid;
    }
    return Residual(hull[lo], period);
}
long double Period(const Rational& r) {
    return static_cast<long double>(r.numerator) / r.denominator;
}
Rational Reduced(int64_t numerator, int64_t denominator) {
    const auto common = std::gcd(numerator, denominator);
    return {numerator / common, denominator / common};
}
Rational RateFromPeriod(Rational period, Rational timeBase) {
    // Cross-reduce before multiplication; malformed timestamps must not overflow.
    const auto a = std::gcd(timeBase.denominator, period.numerator);
    timeBase.denominator /= a;
    period.numerator /= a;
    const auto b = std::gcd(period.denominator, timeBase.numerator);
    period.denominator /= b;
    timeBase.numerator /= b;
    if (timeBase.denominator > INT64_MAX / period.denominator ||
        timeBase.numerator > INT64_MAX / period.numerator)
        return {};
    return Reduced(timeBase.denominator * period.denominator, timeBase.numerator * period.numerator);
}
} // namespace
Json CadenceReport::ToJson() const {
    const double tickSeconds = timeBase.Value();
    const double diagnosticRate = rate.Value() > 0 ? rate.Value() : nominalRate.Value();
    return {{"verified", verified},
            {"verified_cadence", Utf8(rate.Text())},
            {"inferred_cadence", Utf8(rate.Text())},
            {"candidate_source", candidateSource},
            {"classification", classification},
            {"reason", reasonCode},
            {"time_base", Utf8(timeBase.Text())},
            {"avg_fps", averageRate.Value()},
            {"nominal_fps", nominalRate.Value()},
            {"fps", rate.Value()},
            {"packets", packets},
            {"error_packet", errorPacket},
            {"error", error},
            {"error_units", "video time-base ticks"},
            {"error_ticks", errorTicks},
            {"error_microseconds", errorTicks * tickSeconds * 1000000},
            {"error_frame_fraction",
             diagnosticRate > 0 ? Json(errorTicks * tickSeconds * diagnosticRate) : Json(nullptr)},
            {"error_frame_fraction_basis", rate.Value() > 0     ? "candidate"
                                           : diagnosticRate > 0 ? "nominal_metadata"
                                                                : "unavailable"},
            {"max_interval_error", maxIntervalError},
            {"max_phase_error", maxPhaseError},
            {"max_phase_error_seconds", maxPhaseError * tickSeconds},
            {"max_phase_error_frame_fraction", maxPhaseError * tickSeconds * rate.Value()},
            {"max_nominal_phase_error", maxNominalPhaseError},
            {"quantization_span_ticks", quantizationSpanTicks},
            {"max_duration_error", maxDurationError}};
}
CadenceReport AnalyzeCadence(std::span<const VideoPacketTiming> p, const MediaInfo& m) {
    CadenceReport r;
    r.packets = static_cast<int64_t>(p.size());
    r.timeBase = m.timeBase;
    r.nominalRate = m.nominalFps;
    r.averageRate = m.averageFps;
    auto fail = [&](size_t index, const std::string& message, const std::string& reason,
                    long double ticks = 0) {
        if (r.error.empty()) {
            r.errorPacket = static_cast<int64_t>(index);
            r.error = message;
            r.reasonCode = reason;
            r.errorTicks = static_cast<double>(ticks);
        }
    };
    if (p.empty() || m.timeBase.numerator <= 0 || m.timeBase.denominator <= 0) {
        fail(0, "No packets or invalid time base", "pts-corruption");
        return r;
    }
    std::map<int64_t, size_t> durations;
    std::vector<ClockPoint> points;
    points.reserve(p.size() + 1);
    bool fixed = true;
    for (size_t i = 0; i < p.size(); ++i) {
        if (p[i].duration <= 0)
            fail(i, "Missing or non-positive duration", "pts-corruption",
                 static_cast<long double>(p[i].duration));
        ++durations[p[i].duration];
        fixed = fixed && p[i].duration == p[0].duration;
        const auto relative = static_cast<uint64_t>(p[i].pts) - static_cast<uint64_t>(p[0].pts);
        if (p[i].pts < p[0].pts || relative > INT64_MAX)
            fail(i, "Backwards PTS or timestamp span overflow", "pts-corruption");
        points.push_back({static_cast<int64_t>(i), static_cast<int64_t>(relative)});
        if (i) {
            if (p[i].pts <= p[i - 1].pts)
                fail(i, "Duplicate or backwards PTS", "pts-corruption",
                     static_cast<long double>(p[i - 1].duration) +
                         static_cast<long double>(static_cast<uint64_t>(p[i - 1].pts) -
                                                  static_cast<uint64_t>(p[i].pts)));
            const auto delta = static_cast<long double>(static_cast<uint64_t>(p[i].pts) -
                                                        static_cast<uint64_t>(p[i - 1].pts));
            fixed = fixed && delta == p[0].duration;
            if (delta != p[i - 1].duration)
                fail(i, "Timestamp gap/overlap: PTS interval does not equal previous duration",
                     "pts-corruption", std::abs(delta - p[i - 1].duration));
        }
    }
    if (!r.error.empty())
        return r;
    if (points.back().ticks > INT64_MAX - p.back().duration) {
        fail(p.size() - 1, "Final packet endpoint overflow", "pts-corruption");
        return r;
    }
    // Include the final duration: it must not be an unchecked trailing sample.
    points.push_back({static_cast<int64_t>(p.size()), points.back().ticks + p.back().duration});
    const auto modal = std::max_element(durations.begin(), durations.end(), [](const auto& a, const auto& b) {
                           return a.second < b.second;
                       })->first;
    const auto basePeriod = durations.begin()->first;
    Rational candidate{modal, 1};
    r.candidateSource = "constant_packet_clock";
    const long double nominalPeriod =
        m.nominalFps.numerator > 0 && m.nominalFps.denominator > 0
            ? static_cast<long double>(m.nominalFps.denominator) * m.timeBase.denominator /
                  (static_cast<long double>(m.nominalFps.numerator) * m.timeBase.numerator)
            : 0;
    // Roundoff only. One tick is the floor/ceil quantization cell width, not an
    // empirically widened phase tolerance. Every boundary shares the SAME cell.
    const auto roundoff =
        16 * std::numeric_limits<long double>::epsilon() * std::max(1.L, static_cast<long double>(p.size()));
    auto fractionalPeriod = [&](const Rational& period) {
        return static_cast<long double>(period.numerator - basePeriod * period.denominator) /
               period.denominator;
    };
    auto residual = [&](const ClockPoint& point, const Rational& period) {
        // All durations >= basePeriod, so this integer product is <= ticks and
        // cannot overflow. Removing it before floating evaluation avoids losing
        // sub-tick precision when a fine time base has very large tick values.
        return static_cast<long double>(point.ticks - point.frame * basePeriod) -
               point.frame * fractionalPeriod(period);
    };
    auto measure = [&](const Rational& period) {
        r.maxIntervalError = r.maxDurationError = r.maxPhaseError = r.maxNominalPhaseError = 0;
        long double minimum = 0, maximum = 0;
        for (size_t i = 0; i < points.size(); ++i) {
            const auto phase = residual(points[i], period);
            minimum = std::min(minimum, phase);
            maximum = std::max(maximum, phase);
            r.maxPhaseError = std::max(r.maxPhaseError, static_cast<double>(std::abs(phase)));
            if (nominalPeriod > 0)
                r.maxNominalPhaseError =
                    std::max(r.maxNominalPhaseError,
                             static_cast<double>(std::abs(Residual(points[i], nominalPeriod))));
            if (i < p.size())
                r.maxDurationError = std::max(
                    r.maxDurationError,
                    static_cast<double>(std::abs((p[i].duration - basePeriod) - fractionalPeriod(period))));
            if (i && i < p.size())
                r.maxIntervalError = std::max(
                    r.maxIntervalError,
                    static_cast<double>(std::abs((points[i].ticks - points[i - 1].ticks - basePeriod) -
                                                 fractionalPeriod(period))));
        }
        r.quantizationSpanTicks = static_cast<double>(maximum - minimum);
    };
    bool nominalFits = false;
    if (!fixed && m.nominalFps.numerator > 0 && m.nominalFps.denominator > 0 &&
        nominalPeriod >= basePeriod && nominalPeriod <= durations.rbegin()->first) {
        // The reciprocal-product operation is symmetric: 1/(FPS*time_base)
        // yields an exact rational period. A nominal tag only proposes a clock.
        const auto nominalCandidate = RateFromPeriod(m.nominalFps, m.timeBase);
        if (nominalCandidate.numerator > 0 &&
            basePeriod <= INT64_MAX / nominalCandidate.denominator) {
            measure(nominalCandidate);
            nominalFits = r.quantizationSpanTicks <= 1 + roundoff;
            if (nominalFits) {
                candidate = nominalCandidate;
                r.candidateSource = "nominal_verified_quantization";
            }
        }
    }
    if (!fixed) {
        if (durations.rbegin()->first - durations.begin()->first > 1) {
            measure(candidate);
            for (size_t i = 0; i < p.size(); ++i)
                if (p[i].duration - durations.begin()->first > 1) {
                    fail(i, "Local packet durations cannot be floor/ceil samples of one fixed period",
                         "variable-cadence", std::abs(p[i].duration - static_cast<long double>(modal)));
                    break;
                }
        } else if (!nominalFits) {
            auto fractionalPoints = points;
            for (auto& point : fractionalPoints)
                point.ticks -= point.frame * basePeriod;
            const auto upper = Hull(fractionalPoints, true), lower = Hull(fractionalPoints, false);
            auto span = [&](long double period) {
                return Support(upper, period, true) - Support(lower, period, false);
            };
            // Endpoints propose a candidate only. Acceptance still requires every
            // timestamp and duration endpoint to share one quantization cell.
            candidate = Reduced(points.back().ticks, points.back().frame);
            r.candidateSource = "packet_quantization_envelope";
            if (span(fractionalPeriod(candidate)) > 1 + roundoff) {
                std::vector<Rational> slopes;
                for (const auto* hull : {&upper, &lower})
                    for (size_t i = 1; i < hull->size(); ++i)
                        slopes.push_back(Reduced((*hull)[i].ticks - (*hull)[i - 1].ticks,
                                                 (*hull)[i].frame - (*hull)[i - 1].frame));
                std::sort(slopes.begin(), slopes.end(),
                          [](const auto& a, const auto& b) { return Period(a) < Period(b); });
                slopes.erase(std::unique(slopes.begin(), slopes.end(),
                                         [](const auto& a, const auto& b) {
                                             return a.numerator == b.numerator &&
                                                    a.denominator == b.denominator;
                                         }),
                             slopes.end());
                // Vertical hull width is convex in slope. A minimum occurs at
                // a hull edge (possibly at either edge of a flat minimum).
                size_t lo = 0, hi = slopes.size() - 1;
                while (lo < hi) {
                    const auto mid = lo + (hi - lo) / 2;
                    if (span(Period(slopes[mid])) <= span(Period(slopes[mid + 1])))
                        hi = mid;
                    else
                        lo = mid + 1;
                }
                candidate = slopes[lo];
                if (basePeriod > (INT64_MAX - candidate.numerator) / candidate.denominator) {
                    fail(0, "Packet period cannot be represented safely", "unsupported-cadence");
                    return r;
                }
                candidate.numerator += basePeriod * candidate.denominator;
            }
            measure(candidate);
            if (r.quantizationSpanTicks > 1 + roundoff) {
                long double minimum = 0, maximum = 0;
                for (size_t i = 0; i < points.size(); ++i) {
                    minimum = std::min(minimum, residual(points[i], candidate));
                    maximum = std::max(maximum, residual(points[i], candidate));
                    if (maximum - minimum > 1 + roundoff) {
                        fail(std::min(i, p.size() - 1),
                             "No fixed rational period fits all packet boundaries within one quantization "
                             "cell",
                             "quantization-inconsistent", maximum - minimum);
                        break;
                    }
                }
            }
        }
    } else
        measure(candidate);
    r.rate = RateFromPeriod(candidate, m.timeBase);
    if (r.error.empty() && (r.rate.Value() <= 0 || r.rate.Value() > 120))
        fail(0, "Invalid or unsupported verified frame rate", "unsupported-cadence");
    r.verified = r.error.empty();
    if (r.verified) {
        r.classification = fixed ? "fixed" : "quantized-fixed";
        r.reasonCode = fixed ? "constant_packet_period" : "all_boundaries_share_one_quantization_cell";
    }
    return r;
}
AppError CadenceFailure(const CadenceReport& r, const MediaInfo& m) {
    std::ostringstream context;
    const double diagnosticRate = r.rate.Value() > 0 ? r.rate.Value() : m.nominalFps.Value();
    context << std::setprecision(10) << "packet " << r.errorPacket << ": " << r.error
            << "; error=" << r.errorTicks << " ticks (" << r.errorTicks * m.timeBase.Value() * 1000000
            << " us; " << r.errorTicks * m.timeBase.Value() * diagnosticRate << " frame"
            << (r.rate.Value() > 0 ? ")" : ", nominal metadata estimate)")
            << "; time_base=" << Utf8(m.timeBase.Text()) << "; candidate/inferred=" << r.rate.Value()
            << " fps; avg=" << m.averageFps.Value() << "; nominal=" << m.nominalFps.Value()
            << "; category=" << r.reasonCode;
    const auto category = r.reasonCode == "pts-corruption" ? TextId::CadencePtsCorrupt
                          : r.reasonCode == "quantization-inconsistent" ? TextId::CadenceAmbiguous
                                                                       : TextId::CadenceVariable;
    return AppError(Message(TextId::InputCadenceDetail, {context.str()}), {Message(category)});
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
        r.classification = "invalid";
        r.reasonCode = "pts-corruption";
    }
    if (r.verified && std::abs(static_cast<double>(r.packets) / r.rate.Value() - m.videoDuration) >
                          std::max(.05, 2 / r.rate.Value())) {
        r.verified = false;
        r.error = "Packet count/cadence disagrees with video duration";
        r.classification = "invalid";
        r.reasonCode = "container-duration-conflict";
    }
    if (rejectInvalid && !r.verified)
        throw CadenceFailure(r, m);
    return r;
}
} // namespace logforge
