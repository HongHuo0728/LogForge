#pragma once
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
struct MediaInfo {
    fs::path path;
    std::string container, codec, profile, pixelFormat, primaries, transfer, matrix, range, fieldOrder,
        chromaLocation, sampleAspect;
    int width = 0, height = 0, bitDepth = 0, videoStreams = 0;
    int64_t frames = 0;
    Rational fps, nominalFps, timeBase;
    double duration = 0, videoDuration = 0, startTime = 0, rotation = 0;
    std::string timecode;
    Json tags = Json::object(), videoTags = Json::object(), raw;
    std::vector<AudioInfo> audio;
    static MediaInfo Parse(const Json& json, const fs::path& path = {});
    std::vector<std::string> UnsupportedReasons() const;
    std::wstring Summary() const;
};
MediaInfo Probe(const fs::path& ffprobe, const fs::path& path, const std::atomic_bool* cancel = nullptr);
int64_t VerifyConstantFrameRate(const fs::path& ffprobe, const MediaInfo& media,
                                const std::atomic_bool& cancel);
struct ValidationReport {
    bool passed = true;
    std::vector<std::string> errors, warnings;
    Json ToJson() const;
};
ValidationReport ValidateOutput(const MediaInfo& input, const MediaInfo& output, int64_t processedFrames);
struct AppleLogMetadataWriter {
    // No standardized Apple Log H.273 transfer ID is established here.
    // 2 means unspecified. Never use 9 (generic logarithmic 100:1) or 18 (HLG).
    static std::vector<std::wstring> Arguments(const MediaInfo& input);
};
struct ReferenceMovAnalyzer {
    static Json Analyze(const fs::path& file);
};
} // namespace logforge
