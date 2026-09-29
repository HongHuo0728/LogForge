#include "logforge/Media.h"
#include "logforge/ToolTrust.h"
#include <array>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace logforge {
namespace {
std::string str(const Json& j, const char* k, const std::string& fallback = {}) {
    auto it = j.find(k);
    if (it == j.end() || it->is_null())
        return fallback;
    if (it->is_string())
        return it->get<std::string>();
    if (it->is_number())
        return it->dump();
    return fallback;
}
double number(const Json& j, const char* k, double fallback = 0) {
    try {
        const auto s = str(j, k);
        if (s.empty() || s == "N/A")
            return fallback;
        size_t n = 0;
        const double d = std::stod(s, &n);
        return n == s.size() && std::isfinite(d) ? d : fallback;
    } catch (...) {
        return fallback;
    }
}
Json ReadTags(const Json& j) {
    return j.contains("tags") && j["tags"].is_object() ? j["tags"] : Json::object();
}
int integer(const Json& j, const char* key) {
    const auto n = number(j, key);
    if (n < 0 || n > std::numeric_limits<int>::max() || std::floor(n) != n)
        throw AppError(TextId::ProbeStreams);
    return static_cast<int>(n);
}
int64_t frameCount(const Json& j) {
    const auto text = str(j, "nb_frames");
    if (text.empty() || text == "N/A")
        return 0;
    int64_t n = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), n);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || n < 0)
        throw AppError(TextId::ProbeStreams);
    return n;
}
bool Unspecified(const std::string& value) {
    return value.empty() || value == "unknown" || value == "unspecified";
}
bool IsMovDemuxer(const std::string& value) {
    std::istringstream formats(value);
    std::string format;
    while (std::getline(formats, format, ','))
        if (format == "mov")
            return true;
    return false;
}
} // namespace
Rational Rational::Parse(const std::string& value) {
    Rational r;
    const auto slash = value.find('/');
    try {
        size_t n = 0;
        r.numerator = std::stoll(value.substr(0, slash), &n);
        if (n != (slash == std::string::npos ? value.size() : slash))
            return {};
        if (slash != std::string::npos) {
            auto tail = value.substr(slash + 1);
            r.denominator = std::stoll(tail, &n);
            if (n != tail.size())
                return {};
        }
        if (r.denominator <= 0 || r.numerator < 0)
            return {};
        return r;
    } catch (...) {
        return {};
    }
}
MediaInfo MediaInfo::Parse(const Json& j, const fs::path& path) {
    if (!j.is_object() || !j.contains("streams") || !j["streams"].is_array())
        throw AppError(TextId::ProbeStreams);
    MediaInfo m;
    m.path = path;
    m.raw = j;
    const auto f = j.value("format", Json::object());
    m.container = str(f, "format_name");
    m.duration = number(f, "duration");
    m.tags = ReadTags(f);
    for (const auto& s : j["streams"]) {
        const auto type = str(s, "codec_type");
        if (type == "video") {
            const bool attachedPicture = s.value("disposition", Json::object()).value("attached_pic", 0) != 0;
            if (!attachedPicture)
                ++m.mainVideoStreams;
            if (++m.videoStreams > 1)
                continue;
            m.firstVideoAttachedPicture = attachedPicture;
            m.codec = str(s, "codec_name");
            m.profile = str(s, "profile");
            m.pixelFormat = str(s, "pix_fmt");
            m.width = integer(s, "width");
            m.height = integer(s, "height");
            m.bitDepth = integer(s, "bits_per_raw_sample");
            if (!m.bitDepth && m.pixelFormat == "yuv422p10le")
                m.bitDepth = 10;
            m.primaries = str(s, "color_primaries");
            m.transfer = str(s, "color_transfer");
            m.matrix = str(s, "color_space");
            m.range = str(s, "color_range");
            m.fieldOrder = str(s, "field_order");
            m.chromaLocation = str(s, "chroma_location");
            m.sampleAspect = str(s, "sample_aspect_ratio");
            m.averageFps = Rational::Parse(str(s, "avg_frame_rate"));
            m.nominalFps = Rational::Parse(str(s, "r_frame_rate"));
            m.fps = m.averageFps;
            // Camera clock quantization can make the mean differ slightly from the
            // capture rate. This candidate is verified against EVERY packet below.
            if (m.nominalFps.Value() > 0 && m.averageFps.Value() > 0 &&
                std::abs(m.nominalFps.Value() - m.averageFps.Value()) <= m.averageFps.Value() * 0.001)
                m.fps = m.nominalFps;
            m.timeBase = Rational::Parse(str(s, "time_base"));
            m.frames = frameCount(s);
            m.videoDuration = number(s, "duration", m.duration);
            m.startTime = number(s, "start_time");
            m.videoTags = ReadTags(s);
            m.timecode = str(m.videoTags, "timecode");
            m.rotation = number(m.videoTags, "rotate");
            for (const auto& side : s.value("side_data_list", Json::array())) {
                if (side.contains("rotation"))
                    m.rotation = number(side, "rotation");
                if (side.contains("displaymatrix"))
                    m.displayMatrixSupported &= SupportedDisplayMatrix(str(side, "displaymatrix"), m.width, m.height);
            }
        } else if (type == "audio") {
            m.audio.push_back({str(s, "codec_name"), str(s, "channel_layout"), integer(s, "channels"),
                               integer(s, "sample_rate"), number(s, "start_time"), number(s, "duration"),
                               ReadTags(s)});
        } else if (m.timecode.empty() && str(s, "codec_tag_string") == "tmcd")
            m.timecode = str(ReadTags(s), "timecode");
    }
    if (m.timecode.empty())
        m.timecode = str(m.tags, "timecode");
    return m;
}
std::string MediaInfo::EffectiveChromaLocation() const {
    if (Unspecified(chromaLocation))
        return inputChromaOverride.empty() ? "left" : inputChromaOverride;
    return chromaLocation;
}
std::string MediaInfo::EffectiveRange() const {
    return Unspecified(range) ? "tv" : range;
}
std::vector<Message> MediaInfo::InputWarnings() const {
    std::vector<Message> warnings;
    if (Unspecified(primaries))
        warnings.emplace_back(TextId::InputAssumption, std::initializer_list<std::string>{"primaries", "BT.2020"});
    if (Unspecified(matrix))
        warnings.emplace_back(TextId::InputAssumption, std::initializer_list<std::string>{"matrix", "BT.2020 NCL"});
    if (Unspecified(range))
        warnings.emplace_back(TextId::InputAssumption, std::initializer_list<std::string>{"range", "limited"});
    if (Unspecified(chromaLocation) && inputChromaOverride.empty())
        warnings.emplace_back(TextId::InputAssumption, std::initializer_list<std::string>{"chroma location", "left"});
    if (forceBT2020Interpretation && ((!Unspecified(primaries) && primaries != "bt2020") ||
                                      (!Unspecified(matrix) && matrix != "bt2020nc")))
        warnings.emplace_back(TextId::InputColorOverride);
    return warnings;
}
Json MediaInfo::InputInterpretation() const {
    Json result{{"declared", {{"primaries", primaries}, {"matrix", matrix}, {"transfer", transfer},
                              {"range", range}, {"chroma_location", chromaLocation}}},
                {"assumed", Json::object()}, {"overridden", Json::object()},
                {"effective", Json::object()}, {"origin", Json::object()},
                {"conflicting_fields", Json::array()}, {"warnings", Json::array()}};
    const auto field = [&](const char* key, const std::string& declared, const std::string& effective,
                           const char* assumption, bool conflict, bool overridden = false) {
        result["effective"][key] = effective;
        if (overridden) {
            result["overridden"][key] = effective;
            result["origin"][key] = "explicit-override";
        } else if (Unspecified(declared) && assumption) {
            result["assumed"][key] = effective;
            result["origin"][key] = assumption;
        } else {
            result["origin"][key] = "source-declared";
        }
        if (conflict)
            result["conflicting_fields"].push_back(key);
    };
    const bool primariesConflict = !Unspecified(primaries) && primaries != "bt2020";
    const bool matrixConflict = !Unspecified(matrix) && matrix != "bt2020nc";
    field("primaries", primaries, Unspecified(primaries) || forceBT2020Interpretation ? "bt2020" : primaries,
          "assumed-bt2020", primariesConflict, forceBT2020Interpretation && primariesConflict);
    field("matrix", matrix, Unspecified(matrix) || forceBT2020Interpretation ? "bt2020nc" : matrix,
          "assumed-bt2020nc", matrixConflict, forceBT2020Interpretation && matrixConflict);
    field("transfer", transfer, transfer, nullptr, transfer != "arib-std-b67");
    field("range", range, EffectiveRange(), "assumed-limited",
          !Unspecified(range) && range != "tv" && range != "pc");
    if (!Unspecified(range))
        result["origin"]["range"] = range == "pc" ? "declared-full" : range == "tv" ? "declared-limited" : "source-declared";
    field("chroma_location", chromaLocation, EffectiveChromaLocation(), "assumed-left",
          EffectiveChromaLocation() != "left" && EffectiveChromaLocation() != "center",
          Unspecified(chromaLocation) && !inputChromaOverride.empty());
    for (const auto& warning : InputWarnings())
        result["warnings"].push_back(Translate(warning));
    return result;
}
std::vector<Message> MediaInfo::UnsupportedReasons() const {
    std::vector<Message> e;
    // Match admission to the fixed decoder interpretation. Missing optional
    // declarations remain compatible; explicit conflicts must never be ignored.
    // Camera metadata, timecode and edit-list shapes are not prerequisites.
    const auto brand = str(tags, "major_brand");
    if (!IsMovDemuxer(container) || (!brand.empty() && brand != "qt  "))
        e.emplace_back(TextId::InputContainer);
    if (mainVideoStreams != 1 || firstVideoAttachedPicture)
        e.emplace_back(TextId::InputVideoCount);
    if (codec != "prores")
        e.emplace_back(TextId::InputCodec);
    if (profile != "Standard" && profile != "HQ")
        e.emplace_back(TextId::InputProfile);
    if (transfer != "arib-std-b67")
        e.emplace_back(TextId::InputTransfer);
    if (!forceBT2020Interpretation && !Unspecified(primaries) && primaries != "bt2020")
        e.emplace_back(TextId::InputPrimaries);
    if (!forceBT2020Interpretation && !Unspecified(matrix) && matrix != "bt2020nc")
        e.emplace_back(TextId::InputMatrix);
    if (!Unspecified(range) && range != "pc" && range != "tv")
        e.emplace_back(TextId::InputRange);
    if (EffectiveChromaLocation() != "left" && EffectiveChromaLocation() != "center")
        e.emplace_back(TextId::InputChroma);
    return e;
}
std::wstring MediaInfo::Summary(Language language) const {
    std::wostringstream s;
    const auto interpretation = InputInterpretation();
    const auto origin = [&](const char* key) {
        const auto value = interpretation["origin"][key].get<std::string>();
        return TranslateWide(value.starts_with("assumed-") ? TextId::DefaultAssumption :
                             value == "explicit-override" ? TextId::ExplicitSource : TextId::SourceDeclared,
                             language);
    };
    s << (codec == "prores" ? L"ProRes" : Wide(codec)) << L" " << Wide(profile) << L"   ·   " << width
      << L" × " << height << L"   ·   " << std::fixed << std::setprecision(3) << fps.Value() << L" fps\r\n";
    s << (pixelFormat == "yuv422p10le" ? L"10-bit 4:2:2" : Wide(pixelFormat)) << L"   ·   "
      << Wide(interpretation["effective"]["primaries"].get<std::string>()) << L" (" << origin("primaries") << L") / "
      << (transfer == "arib-std-b67" ? L"HLG" : Wide(transfer)) << L" / "
      << Wide(interpretation["effective"]["matrix"].get<std::string>()) << L" (" << origin("matrix") << L")\r\n";
    s << TranslateWide(TextId::RangeLabel, language) << L": " << Wide(EffectiveRange())
      << L" (" << origin("range") << L")   ·   ";
    s << TranslateWide(TextId::ChromaLocation, language) << L": "
      << Wide(EffectiveChromaLocation().empty() ? "unknown" : EffectiveChromaLocation())
      << L" (" << origin("chroma_location") << L")"
      << L"\r\n";
    s << TranslateWide(TextId::Duration, language) << L": " << std::setprecision(2) << videoDuration
      << L" s   ·   " << TranslateWide(TextId::Audio, language) << L": ";
    if (audio.empty())
        s << TranslateWide(TextId::NoAudio, language);
    else
        for (const auto& a : audio)
            s << Wide(a.codec) << L" " << a.channels << L" ch / " << a.sampleRate << L" Hz  ";
    s << L"\r\n"
      << TranslateWide(TextId::Timecode, language) << L": " << (timecode.empty() ? L"—" : Wide(timecode))
      << L"   ·   " << TranslateWide(TextId::Rotation, language) << L": " << rotation << L"°";
    return s.str();
}
MediaInfo Probe(const fs::path& ffprobe, const fs::path& path, const std::atomic_bool* cancel) {
    auto lease = ToolTrust::Acquire(ffprobe.parent_path() / L"ffmpeg.exe");
    if (fs::weakly_canonical(ffprobe) != lease->identity.ffprobe)
        throw AppError(Message(TextId::FFmpegUntrusted, {PathText(ffprobe)}));
    const auto r = RunProcess(lease->identity.ffprobe,
                              {L"-v", L"error", L"-show_format", L"-show_streams", L"-show_chapters", L"-of",
                               L"json", path.wstring()},
                              cancel, 60);
    if (r.exitCode)
        throw AppError(Message(TextId::ProbeFailed, {r.error}));
    try {
        return MediaInfo::Parse(Json::parse(r.output), path);
    } catch (const Json::exception& e) {
        throw AppError(Message(TextId::ProbeJson, {e.what()}));
    }
}
Json ValidationReport::ToJson() const {
    Json e = Json::array(), w = Json::array(), codes = Json::array(), warningCodes = Json::array();
    for (const auto& m : errors) {
        e.push_back(Translate(m));
        codes.push_back(MessageKey(m.id));
    }
    for (const auto& m : warnings) {
        w.push_back(Translate(m));
        warningCodes.push_back(MessageKey(m.id));
    }
    return {{"passed", passed},
            {"validation_passed", passed},
            {"completed", Completed()},
            {"publication", publication},
            {"errors", e},
            {"warnings", w},
            {"error_codes", codes},
            {"warning_codes", warningCodes},
            {"signal_warning", signalWarning},
            {"signal", signal},
            {"timing", timing},
            {"metadata", metadata},
            {"ffmpeg", ffmpeg}};
}
ValidationReport ValidateOutput(const MediaInfo& in, const MediaInfo& out, int64_t frames) {
    ValidationReport r;
    auto require = [&](bool ok, const Message& msg) {
        if (!ok) {
            r.passed = false;
            r.errors.push_back(msg);
        }
    };
    require(out.container.find("mov") != std::string::npos &&
                out.tags.value("major_brand", std::string()) == "qt  ",
            TextId::OutputMov);
    require(out.codec == "prores" && out.profile == "HQ", TextId::OutputCodec);
    require(out.pixelFormat == "yuv422p10le" && out.bitDepth == 10, TextId::OutputDepth);
    require(out.width == in.width && out.height == in.height, TextId::OutputResolution);
    require(std::abs(out.fps.Value() - in.fps.Value()) < 0.00001, TextId::OutputRate);
    require(std::abs(out.averageFps.Value() - in.fps.Value()) < 0.00001, TextId::OutputAverageRate);
    require(out.frames == frames, TextId::OutputFrames);
    require(out.primaries == "bt2020" && out.matrix == "bt2020nc", TextId::OutputGamut);
    require(out.transfer.empty() || out.transfer == "unknown" || out.transfer == "unspecified",
            TextId::OutputTransfer);
    require(out.tags.value("logforge.transfer", std::string()) == "Apple Log", TextId::OutputDeclaration);
    require(out.range == "tv", TextId::OutputRange);
    const bool unspecifiedChroma =
        out.chromaLocation.empty() || out.chromaLocation == "unspecified" || out.chromaLocation == "unknown";
    require(out.chromaLocation == "left" || (unspecifiedChroma && out.outputChromaVerified &&
                                             out.tags.value("logforge.chroma_location", "") == "left"),
            TextId::OutputChroma);
    require(in.cadence.verified && out.cadence.verified && out.cadence.packets == frames &&
                std::abs(out.cadence.rate.Value() - in.cadence.rate.Value()) < 0.00001,
            TextId::OutputCadence);
    if (!out.cadence.verified)
        r.errors.emplace_back(TextId::InputCadenceDetail,
                              std::initializer_list<std::string>{"output packet " +
                                                                 std::to_string(out.cadence.errorPacket) +
                                                                 ": " + out.cadence.error});
    r.timing = {{"input",
                 {{"avg_fps", in.averageFps.Value()},
                  {"nominal_fps", in.nominalFps.Value()},
                  {"cadence", in.cadence.ToJson()}}},
                {"output",
                 {{"avg_fps", out.averageFps.Value()},
                  {"nominal_fps", out.nominalFps.Value()},
                  {"cadence", out.cadence.ToJson()}}}};
    r.metadata = AppleLogMetadataWriter::CopyPlan(in);
    for (auto& entry : r.metadata["preserved"]) {
        const auto scope = entry.value("write_scope", entry["scope"].get<std::string>());
        const auto key = entry["key"].get<std::string>();
        const Json* tags = nullptr;
        if (scope == "format")
            tags = &out.tags;
        else if (scope == "v:0")
            tags = &out.videoTags;
        else if (scope.starts_with("a:")) {
            const auto index = static_cast<size_t>(std::stoul(scope.substr(2)));
            if (index < out.audio.size())
                tags = &out.audio[index].tags;
        }
        const bool preserved =
            tags && tags->contains(key) && SameMetadataValue(key, tags->at(key), entry["value"]);
        entry["verified_preserved"] = preserved;
        const bool defaultLanguage =
            key == "language" && entry["value"] == "und" && tags && !tags->contains(key);
        require(preserved || defaultLanguage, Message(TextId::OutputMetadataPreserve, {scope + ":" + key}));
    }
    for (const auto* tags : {&out.tags, &out.videoTags})
        for (const auto& [key, value] : tags->items())
            require(!AppleLogMetadataWriter::IsConflict(key), Message(TextId::OutputMetadataConflict, {key}));
    for (const auto& stream : out.raw.value("streams", Json::array()))
        if (stream.value("codec_type", "") == "video")
            for (const auto& side : stream.value("side_data_list", Json::array())) {
                const auto type = side.value("side_data_type", "");
                require(!AppleLogMetadataWriter::IsConflict(type),
                        Message(TextId::OutputMetadataConflict, {type}));
            }
    const double tolerance = std::max(0.05, 2.0 / in.fps.Value());
    require(std::abs(out.videoDuration - in.videoDuration) <= tolerance, TextId::OutputVideoDuration);
    require(std::abs(out.duration - in.duration) <= std::max(0.1, tolerance),
            TextId::OutputContainerDuration);
    require(out.audio.size() == in.audio.size(), TextId::OutputAudioCount);
    const auto chaptersIn = in.raw.value("chapters", Json::array()),
               chaptersOut = out.raw.value("chapters", Json::array());
    require(chaptersIn.size() == chaptersOut.size(), TextId::OutputChapters);
    for (size_t i = 0; i < std::min(chaptersIn.size(), chaptersOut.size()); ++i) {
        for (const char* time : {"start_time", "end_time"})
            require(std::abs(number(chaptersIn[i], time) - number(chaptersOut[i], time)) <= 0.001,
                    TextId::OutputChapters);
        require(chaptersIn[i].value("tags", Json::object()).value("title", "") ==
                    chaptersOut[i].value("tags", Json::object()).value("title", ""),
                TextId::OutputChapters);
    }
    for (size_t i = 0; i < std::min(in.audio.size(), out.audio.size()); ++i) {
        const auto& a = in.audio[i];
        const auto& b = out.audio[i];
        const auto index = std::to_string(i + 1);
        require(a.codec == b.codec, {TextId::AudioCodecChanged, {index, a.codec, b.codec}});
        require(a.channels == b.channels, {TextId::AudioChannelsChanged,
                                           {index, std::to_string(a.channels), std::to_string(b.channels)}});
        require(
            a.sampleRate == b.sampleRate,
            {TextId::AudioRateChanged, {index, std::to_string(a.sampleRate), std::to_string(b.sampleRate)}});
        require(a.layout == b.layout, {TextId::AudioLayoutChanged,
                                       {index, a.layout.empty() ? "(unspecified)" : a.layout,
                                        b.layout.empty() ? "(unspecified)" : b.layout}});
        if (a.duration > 0 && b.duration > 0)
            require(std::abs(a.duration - b.duration) < 0.1, TextId::OutputAudioDuration);
        require(std::abs((a.start - in.startTime) - (b.start - out.startTime)) < 0.05,
                TextId::OutputAudioOffset);
    }
    require(std::abs(std::remainder(out.rotation - in.rotation, 360.0)) < 0.1, TextId::OutputRotation);
    if (!in.timecode.empty())
        require(in.timecode == out.timecode, TextId::OutputTimecode);
    for (const auto& [k, v] : in.tags.items()) {
        if (k == "creation_time" || k == "com.apple.quicktime.make" || k == "com.apple.quicktime.model" ||
            k == "com.apple.quicktime.creationdate")
            if (!out.tags.contains(k) || !SameMetadataValue(k, out.tags[k], v))
                r.warnings.emplace_back(TextId::MetadataChanged, std::initializer_list<std::string>{k});
    }
    return r;
}
std::vector<std::wstring> AppleLogMetadataWriter::Arguments(const MediaInfo& in, double exposureStops,
                                                            const ToneAdjustments& tone) {
    std::vector<std::wstring> args{
        L"-map_metadata",
        L"-1",
        L"-map_metadata:s:v:0",
        L"-1",
        L"-map_metadata:s:a",
        L"-1",
        L"-color_primaries",
        L"bt2020",
        L"-color_trc",
        L"2",
        L"-colorspace",
        L"bt2020nc",
        L"-color_range",
        L"tv",
        L"-chroma_sample_location",
        L"left",
        L"-movflags",
        L"+write_colr+use_metadata_tags",
        L"-metadata",
        L"logforge.transfer=Apple Log",
        L"-metadata",
        L"logforge.chroma_location=left",
        L"-metadata",
        L"logforge.reference=BT2408_HLG75pct_to_100pct_reflectance",
        L"-metadata",
        L"logforge.exposure_ev=" + std::to_wstring(exposureStops),
        L"-metadata",
        tone.enabled ? L"logforge.rendering=creative-luma-v1" : L"logforge.rendering=standard",
        L"-metadata",
        L"logforge.shadow_lift_ev=" + std::to_wstring(tone.enabled ? tone.shadowStops : 0.0),
        L"-metadata",
        L"logforge.highlight_compression_ev=" + std::to_wstring(tone.enabled ? tone.highlightStops : 0.0),
        L"-metadata",
        L"logforge.saturation=" + std::to_wstring(tone.enabled ? tone.saturation : 1.0),
        L"-metadata",
        L"logforge.version=" + Wide(Version),
        L"-metadata",
        L"logforge.build=" + Wide(BuildNumber),
        L"-metadata:s:v:0",
        L"encoder=LogForge / FFmpeg prores_ks"};
    const auto plan = CopyPlan(in);
    for (const auto& tag : plan["preserved"]) {
        const auto scope = tag.value("write_scope", tag["scope"].get<std::string>());
        args.push_back(scope == "format" ? L"-metadata" : L"-metadata:s:" + Wide(scope));
        args.push_back(Wide(tag["key"].get<std::string>()) + L"=" + Wide(tag["value"].get<std::string>()));
    }
    // Disabling automatic metadata copy also disables chapter titles. Restore
    // only their safe title field, with the chapter timeline mapped separately.
    const auto chapters = in.raw.value("chapters", Json::array());
    for (size_t i = 0; i < chapters.size(); ++i) {
        const auto tags = chapters[i].value("tags", Json::object());
        if (tags.contains("title") && tags.at("title").is_string()) {
            args.push_back(L"-metadata:c:" + std::to_wstring(i));
            args.push_back(L"title=" + Wide(tags.at("title").get<std::string>()));
        }
    }
    if (!in.timecode.empty()) {
        args.push_back(L"-timecode");
        args.push_back(Wide(in.timecode));
    }
    return args;
}
} // namespace logforge
