#include "logforge/Media.h"
#include <array>
#include <charconv>
#include <cmath>
#include <iomanip>
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
        throw std::runtime_error("ffprobe did not return a media stream list.");
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
            if (++m.videoStreams > 1)
                continue;
            m.codec = str(s, "codec_name");
            m.profile = str(s, "profile");
            m.pixelFormat = str(s, "pix_fmt");
            m.width = static_cast<int>(number(s, "width"));
            m.height = static_cast<int>(number(s, "height"));
            m.bitDepth = static_cast<int>(number(s, "bits_per_raw_sample"));
            if (!m.bitDepth && m.pixelFormat == "yuv422p10le")
                m.bitDepth = 10;
            m.primaries = str(s, "color_primaries");
            m.transfer = str(s, "color_transfer");
            m.matrix = str(s, "color_space");
            m.range = str(s, "color_range");
            m.fieldOrder = str(s, "field_order");
            m.chromaLocation = str(s, "chroma_location");
            m.sampleAspect = str(s, "sample_aspect_ratio");
            m.fps = Rational::Parse(str(s, "avg_frame_rate"));
            m.nominalFps = Rational::Parse(str(s, "r_frame_rate"));
            m.timeBase = Rational::Parse(str(s, "time_base"));
            m.frames = static_cast<int64_t>(number(s, "nb_frames"));
            m.videoDuration = number(s, "duration", m.duration);
            m.startTime = number(s, "start_time");
            m.videoTags = ReadTags(s);
            m.timecode = str(m.videoTags, "timecode");
            m.rotation = number(m.videoTags, "rotate");
            for (const auto& side : s.value("side_data_list", Json::array()))
                if (side.contains("rotation"))
                    m.rotation = number(side, "rotation");
        } else if (type == "audio") {
            m.audio.push_back({str(s, "codec_name"), str(s, "channel_layout"),
                               static_cast<int>(number(s, "channels")),
                               static_cast<int>(number(s, "sample_rate")), number(s, "start_time"),
                               number(s, "duration"), ReadTags(s)});
        } else if (m.timecode.empty() && str(s, "codec_tag_string") == "tmcd")
            m.timecode = str(ReadTags(s), "timecode");
    }
    if (m.timecode.empty())
        m.timecode = str(m.tags, "timecode");
    return m;
}
std::vector<std::string> MediaInfo::UnsupportedReasons() const {
    std::vector<std::string> e;
    if (videoStreams != 1)
        e.push_back("V1 需要恰好一个视频流。");
    if (codec != "prores")
        e.push_back("输入不是 ProRes。");
    if (profile != "Standard" && profile != "HQ")
        e.push_back("V1 只支持 ProRes 422 / 422 HQ。");
    if (bitDepth != 10 || pixelFormat != "yuv422p10le")
        e.push_back("输入不是 10-bit 4:2:2。");
    if (primaries != "bt2020")
        e.push_back("输入没有明确标记为 BT.2020 色域。");
    if (transfer != "arib-std-b67")
        e.push_back("输入没有明确标记为 HLG。");
    if (matrix != "bt2020nc")
        e.push_back("V1 要求 BT.2020 non-constant-luminance YCbCr 矩阵。");
    if (range != "tv" && range != "pc")
        e.push_back("输入色彩范围未知，无法安全解码。");
    if (width <= 0 || height <= 0 || width % 2 || width > 8192 || height > 8192)
        e.push_back("不支持的分辨率（需要偶数宽度，最大 8192 × 8192）。");
    if (fps.Value() <= 0 || fps.Value() > 120 || timeBase.Value() <= 0 || videoDuration <= 0)
        e.push_back("帧率、时间基准或时长无效。");
    if (fieldOrder != "progressive" && fieldOrder != "unknown" && !fieldOrder.empty())
        e.push_back("V1 仅支持逐行扫描视频。");
    if (!sampleAspect.empty() && sampleAspect != "1:1" && sampleAspect != "0:1" && sampleAspect != "N/A")
        e.push_back("V1 仅支持方形像素；不支持变形宽银幕像素比例。");
    if (fps.Value() > 0 && nominalFps.Value() > 0 &&
        std::abs(fps.Value() - nominalFps.Value()) > fps.Value() * 0.001)
        e.push_back("V1 仅支持固定帧率；检测到帧率不一致。");
    return e;
}
std::wstring MediaInfo::Summary() const {
    std::wostringstream s;
    s << (codec == "prores" ? L"ProRes" : Wide(codec)) << L" " << Wide(profile) << L"   ·   " << width
      << L" × " << height << L"   ·   " << std::fixed << std::setprecision(3) << fps.Value() << L" fps\r\n";
    s << (pixelFormat == "yuv422p10le" ? L"10-bit 4:2:2" : Wide(pixelFormat)) << L"   ·   "
      << (primaries == "bt2020" ? L"BT.2020" : Wide(primaries)) << L" / "
      << (transfer == "arib-std-b67" ? L"HLG" : Wide(transfer)) << L" / "
      << (matrix == "bt2020nc" ? L"BT.2020 NCL" : Wide(matrix)) << L"   ·   Range: " << Wide(range)
      << L"\r\n";
    s << L"Duration: " << std::setprecision(2) << videoDuration << L" s   ·   Audio: ";
    if (audio.empty())
        s << L"None";
    else
        for (const auto& a : audio)
            s << Wide(a.codec) << L" " << a.channels << L" ch / " << a.sampleRate << L" Hz  ";
    s << L"\r\nTimecode: " << (timecode.empty() ? L"—" : Wide(timecode)) << L"   ·   Rotation: " << rotation
      << L"°";
    return s.str();
}
MediaInfo Probe(const fs::path& ffprobe, const fs::path& path, const std::atomic_bool* cancel) {
    const auto r = RunProcess(
        ffprobe, {L"-v", L"error", L"-show_format", L"-show_streams", L"-of", L"json", path.wstring()},
        cancel, 60);
    if (r.exitCode)
        throw std::runtime_error("无法分析视频：" + r.error);
    try {
        return MediaInfo::Parse(Json::parse(r.output), path);
    } catch (const Json::exception& e) {
        throw std::runtime_error(std::string("Invalid ffprobe JSON: ") + e.what());
    }
}
int64_t VerifyConstantFrameRate(const fs::path& ffprobe, const MediaInfo& m, const std::atomic_bool& cancel) {
    int64_t count = 0, previous = 0;
    bool good = true;
    const double expected = 1.0 / m.fps.Value() / m.timeBase.Value();
    auto r =
        RunProcess(ffprobe,
                   {L"-v", L"error", L"-select_streams", L"v:0", L"-show_packets", L"-show_entries",
                    L"packet=pts,duration", L"-of", L"csv=p=0", m.path.wstring()},
                   &cancel, 0, [&](const std::string& line) {
                       auto c = line.find(',');
                       if (c == std::string::npos) {
                           good = false;
                           return;
                       }
                       try {
                           int64_t pts = std::stoll(line.substr(0, c));
                           int64_t dur = std::stoll(line.substr(c + 1));
                           if (dur <= 0 || std::abs(static_cast<double>(dur) - expected) > 1.05)
                               good = false;
                           if (count && (pts <= previous ||
                                         std::abs(static_cast<double>(pts - previous) - expected) > 1.05))
                               good = false;
                           previous = pts;
                           ++count;
                       } catch (...) {
                           good = false;
                       }
                   });
    if (r.exitCode || !good || count == 0)
        throw std::runtime_error("输入时间戳不是完整的固定帧率序列；V1 拒绝重建或猜测 VFR 时间戳。");
    if (std::abs(static_cast<double>(count) / m.fps.Value() - m.videoDuration) >
        std::max(0.05, 2.0 / m.fps.Value()))
        throw std::runtime_error("输入帧数与视频时长不一致。");
    return count;
}
Json ValidationReport::ToJson() const {
    return {{"passed", passed}, {"errors", errors}, {"warnings", warnings}};
}
ValidationReport ValidateOutput(const MediaInfo& in, const MediaInfo& out, int64_t frames) {
    ValidationReport r;
    auto require = [&](bool ok, const char* msg) {
        if (!ok) {
            r.passed = false;
            r.errors.push_back(msg);
        }
    };
    require(out.container.find("mov") != std::string::npos &&
                out.tags.value("major_brand", std::string()) == "qt  ",
            "Output is not QuickTime MOV.");
    require(out.codec == "prores" && out.profile == "HQ", "Output is not ProRes 422 HQ.");
    require(out.pixelFormat == "yuv422p10le" && out.bitDepth == 10, "Output is not 10-bit 4:2:2.");
    require(out.width == in.width && out.height == in.height, "Resolution changed.");
    require(std::abs(out.fps.Value() - in.fps.Value()) < 0.00001, "Frame rate changed.");
    require(out.frames == frames, "Output frame count differs from processed frame count.");
    require(out.primaries == "bt2020" && out.matrix == "bt2020nc",
            "Output BT.2020 primaries or matrix are wrong.");
    require(out.transfer.empty() || out.transfer == "unknown" || out.transfer == "unspecified",
            "Conflicting output transfer metadata.");
    require(out.tags.value("logforge.transfer", std::string()) == "Apple Log",
            "LogForge Apple Log declaration missing.");
    require(out.range == "tv", "Output is not video range.");
    const double tolerance = std::max(0.05, 2.0 / in.fps.Value());
    require(std::abs(out.videoDuration - in.videoDuration) <= tolerance, "Video duration changed.");
    require(std::abs(out.duration - in.duration) <= std::max(0.1, tolerance), "Container duration changed.");
    require(out.audio.size() == in.audio.size(), "Audio stream count changed.");
    for (size_t i = 0; i < std::min(in.audio.size(), out.audio.size()); ++i) {
        const auto& a = in.audio[i];
        const auto& b = out.audio[i];
        require(a.codec == b.codec && a.channels == b.channels && a.sampleRate == b.sampleRate &&
                    a.layout == b.layout,
                "Audio format changed.");
        if (a.duration > 0 && b.duration > 0)
            require(std::abs(a.duration - b.duration) < 0.1, "Audio duration changed.");
        require(std::abs((a.start - in.startTime) - (b.start - out.startTime)) < 0.05,
                "Audio/video start offset changed.");
    }
    require(std::abs(std::remainder(out.rotation - in.rotation, 360.0)) < 0.1, "Rotation changed.");
    if (!in.timecode.empty())
        require(in.timecode == out.timecode, "Timecode changed or is missing.");
    for (const auto& [k, v] : in.tags.items()) {
        if (k == "creation_time" || k == "com.apple.quicktime.make" || k == "com.apple.quicktime.model" ||
            k == "com.apple.quicktime.creationdate")
            if (!out.tags.contains(k) || out.tags[k] != v)
                r.warnings.push_back("Metadata not preserved exactly: " + k);
    }
    r.warnings.push_back("Apple Log pixels; nclc transfer=2 (unspecified). Assign Apple Log / Rec.2020 "
                         "manually in your editor. Automatic Apple identification is not certified.");
    return r;
}
std::vector<std::wstring> AppleLogMetadataWriter::Arguments(const MediaInfo& in) {
    std::vector<std::wstring> args{
        L"-map_metadata",       L"1",
        L"-map_metadata:s:v:0", L"1:s:v:0",
        L"-color_primaries",    L"bt2020",
        L"-color_trc",          L"2",
        L"-colorspace",         L"bt2020nc",
        L"-color_range",        L"tv",
        L"-movflags",           L"+write_colr+use_metadata_tags",
        L"-metadata",           L"logforge.transfer=Apple Log",
        L"-metadata",           L"logforge.reference=HLG75pct_to_90pct_reflectance",
        L"-metadata",           L"logforge.version=0.1.0",
        L"-metadata:s:v:0",     L"encoder=LogForge / FFmpeg prores_ks"};
    // Drop source HDR declarations which no longer describe the encoded pixels.
    for (const auto& [key, value] : in.tags.items()) {
        std::string lower = key;
        for (auto& c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.find("transfer") != std::string::npos || lower.find("hdr") != std::string::npos ||
            lower.find("dolby") != std::string::npos || lower.find("colorspace") != std::string::npos ||
            lower.find("color_space") != std::string::npos)
            if (key.rfind("logforge.", 0) != 0) {
                args.push_back(L"-metadata");
                args.push_back(Wide(key) + L"=");
            }
    }
    if (!in.timecode.empty()) {
        args.push_back(L"-timecode");
        args.push_back(Wide(in.timecode));
    }
    return args;
}
namespace {
uint32_t be32(const unsigned char* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
uint64_t be64(const unsigned char* p) {
    return (uint64_t(be32(p)) << 32) | be32(p + 4);
}
void atoms(std::ifstream& f, uint64_t begin, uint64_t end, const std::string& parent, Json& list, int depth) {
    if (depth > 20)
        throw std::runtime_error("MOV nesting limit exceeded.");
    for (uint64_t pos = begin; pos + 8 <= end;) {
        if (list.size() > 100000)
            throw std::runtime_error("MOV atom count limit exceeded.");
        std::array<unsigned char, 96> b{};
        f.seekg(static_cast<std::streamoff>(pos));
        f.read(reinterpret_cast<char*>(b.data()), 8);
        if (!f)
            throw std::runtime_error("Truncated MOV atom.");
        uint64_t size = be32(b.data()), header = 8;
        std::string type;
        for (size_t k = 4; k < 8; ++k) {
            if (b[k] >= 32 && b[k] < 127)
                type += static_cast<char>(b[k]);
            else {
                std::ostringstream escaped;
                escaped << "\\x" << std::hex << std::setw(2) << std::setfill('0')
                        << static_cast<unsigned>(b[k]);
                type += escaped.str();
            }
        }
        if (size == 1) {
            if (end - pos < 16)
                throw std::runtime_error("Truncated extended MOV atom header.");
            f.read(reinterpret_cast<char*>(b.data() + 8), 8);
            if (!f)
                throw std::runtime_error("Truncated extended MOV atom header.");
            size = be64(b.data() + 8);
            header = 16;
        }
        if (size == 0)
            size = end - pos;
        if (size < header || size > end - pos)
            throw std::runtime_error("Invalid MOV atom size.");
        std::string path = parent + "/" + type;
        Json item{{"path", path}, {"offset", pos}, {"size", size}, {"type", type}};
        if (type == "colr" && size >= header + 10) {
            f.seekg(static_cast<std::streamoff>(pos + header));
            f.read(reinterpret_cast<char*>(b.data()), 10);
            item["color_type"] = std::string(reinterpret_cast<char*>(b.data()), 4);
            if (item["color_type"] == "nclc" || item["color_type"] == "nclx") {
                item["primaries"] = (b[4] << 8) | b[5];
                item["transfer"] = (b[6] << 8) | b[7];
                item["matrix"] = (b[8] << 8) | b[9];
                if (item["color_type"] == "nclx" && size >= header + 11) {
                    char flag = 0;
                    f.read(&flag, 1);
                    item["full_range"] = (static_cast<unsigned char>(flag) & 0x80) != 0;
                }
            }
        }
        list.push_back(item);
        if (type == "moov" || type == "trak" || type == "mdia" || type == "minf" || type == "stbl" ||
            type == "udta" || type == "ilst" || type == "dinf" || type == "edts" || type == "tref")
            atoms(f, pos + header, pos + size, path, list, depth + 1);
        else if (type == "meta" || type == "stsd")
            atoms(f, pos + header + (type == "meta" ? 4 : 8), pos + size, path, list, depth + 1);
        else if (type == "apch" || type == "apcn" || type == "apcs" || type == "apco" || type == "ap4h" ||
                 type == "ap4x")
            atoms(f, pos + header + 78, pos + size, path, list, depth + 1);
        pos += size;
    }
}
} // namespace
Json ReferenceMovAnalyzer::Analyze(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot read MOV.");
    Json list = Json::array();
    atoms(f, 0, fs::file_size(path), "", list, 0);
    return {{"atoms", list}, {"bytes", fs::file_size(path)}};
}
} // namespace logforge
