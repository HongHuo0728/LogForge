#pragma once
#include "Color.h"
#include "Localization.h"
#include "Platform.h"
#include <nlohmann/json.hpp>

namespace logforge {
using Json = nlohmann::json;
struct Rational {
    int64_t numerator = 0, denominator = 1;
    double Value() const {
        return denominator ? static_cast<double>(numerator) / static_cast<double>(denominator) : 0;
    }
    std::wstring Text() const {
        return std::to_wstring(numerator) + L"/" + std::to_wstring(denominator);
    }
    static Rational Parse(const std::string& value);
};
struct AudioInfo {
    std::string codec, layout;
    int channels = 0, sampleRate = 0;
    double start = 0, duration = 0;
    Json tags = Json::object();
};
struct VideoPacketTiming {
    int64_t pts = 0, duration = 0;
};
struct CadenceReport {
    bool verified = false;
    Rational rate, timeBase, nominalRate, averageRate;
    int64_t packets = 0, errorPacket = -1;
    double maxIntervalError = 0, maxPhaseError = 0, maxDurationError = 0;
    double maxNominalPhaseError = 0, quantizationSpanTicks = 0, errorTicks = 0;
    std::string error, candidateSource, classification = "invalid", reasonCode;
    Json ToJson() const;
};
struct MediaInfo {
    fs::path path;
    std::string container, codec, profile, pixelFormat, primaries, transfer, matrix, range, fieldOrder,
        chromaLocation, sampleAspect;
    int width = 0, height = 0, bitDepth = 0, videoStreams = 0, mainVideoStreams = 0;
    int64_t frames = 0;
    Rational fps, averageFps, nominalFps, timeBase;
    double duration = 0, videoDuration = 0, startTime = 0, rotation = 0;
    std::string timecode;
    Json tags = Json::object(), videoTags = Json::object(), raw;
    std::vector<AudioInfo> audio;
    CadenceReport cadence;
    std::string inputChromaOverride;
    bool outputChromaVerified = false;
    bool displayMatrixSupported = true;
    bool firstVideoAttachedPicture = false;
    bool forceBT2020Interpretation = false;
    std::string EffectiveChromaLocation() const;
    std::string EffectiveRange() const;
    Json InputInterpretation() const;
    std::vector<Message> InputWarnings() const;
    static MediaInfo Parse(const Json& json, const fs::path& path = {});
    std::vector<Message> UnsupportedReasons() const;
    std::wstring Summary(Language language = Language::English) const;
};
bool SameMetadataValue(const std::string& key, const Json& a, const Json& b);
bool SupportedDisplayMatrix(const std::string& matrix, int width = 0, int height = 0);
Json PreserveMovCreationTimes(const Json& sourceAtoms, const fs::path& encodedPartial,
                              const std::atomic_bool& cancel, bool orientationBaked = false);
Json InspectMovTimeline(const MediaInfo& input, const Json& atoms, const fs::path& ffprobe = {},
                        const std::atomic_bool* cancel = nullptr);
MediaInfo Probe(const fs::path& ffprobe, const fs::path& path, const std::atomic_bool* cancel = nullptr);
CadenceReport AnalyzeCadence(std::span<const VideoPacketTiming> packets, const MediaInfo& media);
AppError CadenceFailure(const CadenceReport& report, const MediaInfo& media);
CadenceReport VerifyConstantFrameRate(const fs::path& ffprobe, const MediaInfo& media,
                                      const std::atomic_bool& cancel, bool rejectInvalid = true);
struct ValidationReport {
    bool passed = true;
    bool signalWarning = false;
    std::vector<Message> errors, warnings;
    Json signal = Json::object();
    Json timing = Json::object(), metadata = Json::object(), ffmpeg = Json::object();
    Json publication{{"attempted", false}, {"published", false}, {"final_path", ""},
                     {"status", "not_attempted"}, {"error", ""}};
    bool Completed() const { return passed && publication.value("published", false); }
    Json ToJson() const;
};
ValidationReport ValidateOutput(const MediaInfo& input, const MediaInfo& output, int64_t processedFrames);
struct AppleLogMetadataWriter {
    // No standardized Apple Log H.273 transfer ID is established here.
    // 2 means unspecified. Never use 9 (generic logarithmic 100:1) or 18 (HLG).
    static std::vector<std::wstring> Arguments(const MediaInfo& input, double exposureStops = 0,
                                               const ToneAdjustments& tone = {});
    static Json CopyPlan(const MediaInfo& input);
    static bool IsConflict(const std::string& key);
};
struct ReferenceMovAnalyzer {
    static Json Analyze(const fs::path& file);
    static Json SemanticDiff(const Json& first, const Json& second);
};
} // namespace logforge
