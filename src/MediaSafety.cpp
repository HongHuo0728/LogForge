#include "logforge/Media.h"
#include <charconv>
#include <chrono>
#include <cmath>
#include <map>
#include <optional>
#include <sstream>

namespace logforge {
namespace {
// ISO 8601 calendar timestamps, including QuickTime's colonless zone suffix.
// No tolerance: compare the represented instant, not its formatting.
std::optional<int64_t> Timestamp(const std::string& s) {
    if (s.size() < 19)
        return {};
    auto digits = [&](size_t at, size_t n) -> int {
        if (at + n > s.size())
            return -1;
        int v = 0;
        auto r = std::from_chars(s.data() + at, s.data() + at + n, v);
        return r.ec == std::errc{} && r.ptr == s.data() + at + n ? v : -1;
    };
    const int y = digits(0, 4), mo = digits(5, 2), d = digits(8, 2), h = digits(11, 2), mi = digits(14, 2),
              sec = digits(17, 2);
    using namespace std::chrono;
    const year_month_day date{year{y}, month{static_cast<unsigned>(mo)}, day{static_cast<unsigned>(d)}};
    if (y < 1601 || y > 9999 || !date.ok() || h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 59 ||
        s[4] != '-' || s[7] != '-' || (s[10] != 'T' && s[10] != ' ') || s[13] != ':' || s[16] != ':')
        return {};
    size_t pos = 19;
    int64_t fraction = 0, factor = 100000;
    if (pos < s.size() && s[pos] == '.') {
        ++pos;
        const auto first = pos;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
            if (factor) {
                fraction += (s[pos] - '0') * factor;
                factor /= 10;
            } else if (s[pos] != '0')
                return {}; // Do not silently discard finer precision.
            ++pos;
        }
        if (pos == first)
            return {};
    }
    int zone = 0;
    if (pos < s.size() && (s[pos] == 'Z' || s[pos] == 'z'))
        ++pos;
    else if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
        const int sign = s[pos++] == '+' ? 1 : -1;
        const int zh = digits(pos, 2);
        pos += 2;
        if (pos < s.size() && s[pos] == ':')
            ++pos;
        const int zm = digits(pos, 2);
        pos += 2;
        if (zh < 0 || zh > 23 || zm < 0 || zm > 59)
            return {};
        zone = sign * (zh * 3600 + zm * 60);
    } else
        return {}; // Ambiguous local timestamps only match verbatim.
    if (pos != s.size())
        return {};
    return (duration_cast<seconds>(sys_days(date).time_since_epoch()).count() + h * 3600 + mi * 60 + sec -
            zone) *
               1000000 +
           fraction;
}
} // namespace
bool SameMetadataValue(const std::string& key, const Json& a, const Json& b) {
    if (a == b)
        return true;
    if ((key != "creation_time" && key != "com.apple.quicktime.creationdate") || !a.is_string() ||
        !b.is_string())
        return false;
    const auto x = Timestamp(a.get<std::string>()), y = Timestamp(b.get<std::string>());
    return x && y && *x == *y;
}
bool SupportedDisplayMatrix(const std::string& matrix, int width, int height) {
    std::istringstream input(matrix);
    std::string line;
    std::vector<int64_t> v;
    while (std::getline(input, line)) {
        if (line.empty())
            continue;
        auto colon = line.find(':');
        if (colon == std::string::npos)
            return false;
        std::istringstream row(line.substr(colon + 1));
        int64_t a, b, c;
        if (!(row >> a >> b >> c))
            return false;
        std::string extra;
        if (row >> extra)
            return false;
        v.insert(v.end(), {a, b, c});
    }
    if (v.size() != 9 || v[2] != 0 || v[5] != 0 || v[8] != 1073741824)
        return false;
    // QTFF row-vector convention: x'=a*x+c*y+tx, y'=b*x+d*y+ty.
    // Accept a pure unit rotation, or its exact translation that rebases the
    // rotated raster to (0,0). Arbitrary placement/cropping remains unsupported.
    for (const auto& r : {std::array<int64_t, 4>{65536, 0, 0, 65536},
                          {0, -65536, 65536, 0},
                          {-65536, 0, 0, -65536},
                          {0, 65536, -65536, 0}})
        if (v[0] == r[0] && v[1] == r[1] && v[3] == r[2] && v[4] == r[3]) {
            if (v[6] == 0 && v[7] == 0)
                return true;
            if (width <= 0 || height <= 0)
                return false;
            const auto tx = -std::min<int64_t>(0, v[0] * width) - std::min<int64_t>(0, v[3] * height);
            const auto ty = -std::min<int64_t>(0, v[1] * width) - std::min<int64_t>(0, v[4] * height);
            return v[6] == tx && v[7] == ty;
        }
    return false;
}
Json PreserveMovCreationTimes(const Json& source, const fs::path& partial, const std::atomic_bool& cancel,
                              bool orientationBaked) {
    // FFmpeg MOV currently assigns the global creation time to every track.
    // Restore only documented fixed-width header fields from the source. No
    // atom resizing, media offsets, sample tables or mdat bytes are changed.
    auto headers = [](const Json& analysis) {
        std::map<std::string, Json> result;
        std::map<std::string, unsigned> ordinals;
        for (const auto& a : analysis.at("atoms")) {
            const auto type = a.value("type", "");
            if (type == "mvhd") {
                if (result.contains("movie/mvhd"))
                    throw AppError(TextId::MovSize);
                result["movie/mvhd"] = a;
            }
            if (type != "trak")
                continue;
            const auto prefix = a.at("path").get<std::string>();
            std::string handler;
            for (const auto& b : analysis.at("atoms"))
                if (b.value("path", "") == prefix + "/mdia[0]/hdlr[0]")
                    handler = b.value("handler_type", "");
            if (handler != "vide" && handler != "soun" && handler != "tmcd")
                continue;
            const auto key = handler + "/" + std::to_string(ordinals[handler]++);
            for (const auto& b : analysis.at("atoms")) {
                const auto path = b.value("path", "");
                if (path == prefix + "/tkhd[0]")
                    result[key + "/tkhd"] = b;
                if (path == prefix + "/mdia[0]/mdhd[0]")
                    result[key + "/mdhd"] = b;
            }
        }
        return result;
    };
    const auto in = headers(source), out = headers(ReferenceMovAnalyzer::Analyze(partial));
    Json patches = Json::array();
    for (const auto& [key, a] : in) {
        // Only v:0 is encoded, and at most one source timecode is regenerated.
        // Admission has already refused multiple primary pictures; other video
        // entries are attached pictures. Omitted auxiliary headers cannot be
        // restored into a stream that was deliberately not mapped.
        if (!out.contains(key) &&
            ((key.starts_with("vide/") && !key.starts_with("vide/0/")) || key.starts_with("tmcd/")))
            continue;
        if (!out.contains(key))
            throw AppError(Message(TextId::OutputMetadataPreserve, {key + ": creation_time header missing"}));
        const auto& b = out.at(key);
        const auto value = a.at("creation_time_1904_seconds").get<uint64_t>();
        const auto bytes = b.at("version") == 1 ? 8u : 4u;
        if (bytes == 4 && value > UINT32_MAX)
            throw AppError(
                Message(TextId::OutputMetadataPreserve, {key + ": creation_time exceeds header capacity"}));
        const auto offset = b.at("offset").get<uint64_t>() + b.at("header_size").get<uint64_t>() + 4;
        if (offset + bytes > b.at("offset").get<uint64_t>() + b.at("size").get<uint64_t>())
            throw AppError(TextId::MovSize);
        patches.push_back(
            {{"header", key}, {"offset", offset}, {"bytes", bytes}, {"creation_time_1904_seconds", value}});
        if (key.starts_with("vide/") && a.value("type", "") == "tkhd") {
            // Rotation remux can replace a valid translated matrix with an
            // angle-only matrix. Restore the complete, validated video matrix.
            const auto matrix = orientationBaked
                                    ? Json::array({65536, 0, 0, 0, 65536, 0, 0, 0, 1073741824})
                                    : a.at("display_matrix");
            if (matrix.size() != 9)
                throw AppError(TextId::InputDisplayMatrix);
            const auto at = b.at("offset").get<uint64_t>() + b.at("header_size").get<uint64_t>() +
                            (b.at("version") == 1 ? 52 : 40);
            if (at + 36 > b.at("offset").get<uint64_t>() + b.at("size").get<uint64_t>())
                throw AppError(TextId::MovSize);
            patches.back()["display_matrix"] = matrix;
            patches.back()["matrix_offset"] = at;
        }
    }
    std::fstream file(partial, std::ios::binary | std::ios::in | std::ios::out);
    if (!file)
        throw AppError(TextId::MovRead);
    for (const auto& p : patches) {
        if (cancel.load())
            throw AppError(TextId::Cancelled);
        const auto bytes = p.at("bytes").get<unsigned>();
        const auto value = p.at("creation_time_1904_seconds").get<uint64_t>();
        std::array<char, 8> b{};
        for (unsigned i = 0; i < bytes; ++i)
            b[i] = static_cast<char>(value >> ((bytes - 1 - i) * 8));
        file.seekp(p.at("offset").get<std::streamoff>());
        file.write(b.data(), bytes);
        if (p.contains("display_matrix")) {
            file.seekp(p.at("matrix_offset").get<std::streamoff>());
            for (const auto& element : p.at("display_matrix")) {
                const auto word = static_cast<uint32_t>(element.get<int32_t>());
                char encoded[4];
                for (unsigned i = 0; i < 4; ++i)
                    encoded[i] = static_cast<char>(word >> ((3 - i) * 8));
                file.write(encoded, 4);
            }
        }
        if (!file)
            throw AppError(TextId::MovWrite);
    }
    file.flush();
    if (!file)
        throw AppError(TextId::MovWrite);
    file.close();
    const auto verified = headers(ReferenceMovAnalyzer::Analyze(partial));
    for (auto& p : patches) {
        const auto key = p.at("header").get<std::string>();
        p["verified"] = verified.contains(key) && verified.at(key).at("creation_time_1904_seconds") ==
                                                      p.at("creation_time_1904_seconds");
        if (p.contains("display_matrix")) {
            p["display_matrix_verified"] = verified.contains(key) &&
                                           verified.at(key).at("display_matrix") == p.at("display_matrix");
            if (!p.at("display_matrix_verified").get<bool>())
                throw AppError(TextId::InputDisplayMatrix);
            p.erase("matrix_offset");
        }
        if (!p.at("verified").get<bool>())
            throw AppError(
                Message(TextId::OutputMetadataPreserve, {key + ": creation_time header verification"}));
        p.erase("offset");
    }
    return patches;
}
Json InspectMovTimeline(const MediaInfo& in, const Json& analysis, const fs::path&,
                        const std::atomic_bool*) {
    Json result{{"chapters", "copied and validated"},
                {"policy", "FFmpeg playback timestamps; edit lists are informational, not admission gates"},
                {"edit_lists", Json::array()}, {"removed_streams", Json::array()},
                {"preserved_streams", Json::array()}, {"regenerated_streams", Json::array()}};
    for (const auto& a : analysis.at("atoms")) {
        if (a.value("type", "") == "mvhd") {
            const auto scale = a.value("timescale", uint64_t{});
            if (scale > 0 && scale <= INT32_MAX)
                result["preserve_movie_timescale"] = scale;
        }
        if (a.value("type", "") != "elst")
            continue;
        const auto path = a.at("path").get<std::string>();
        const auto track = path.substr(0, path.find("/edts"));
        std::string handler;
        for (const auto& header : analysis.at("atoms"))
            if (header.value("path", "") == track + "/mdia[0]/hdlr[0]")
                handler = header.value("handler_type", "");
        // In particular, iPhone mebx tracks can have nonzero origins. They are
        // not copied, and must not prevent conversion of the video/audio.
        result["edit_lists"].push_back(
            {{"path", path}, {"handler", handler}, {"edits", a.at("edits")},
             {"policy", handler == "vide" || handler == "soun"
                            ? "playback timeline interpreted by FFmpeg"
                            : "auxiliary track omitted; timecode regenerated if present"}});
    }
    bool selectedVideo = false, selectedTimecode = false;
    for (const auto& stream : in.raw.value("streams", Json::array())) {
        const auto type = stream.value("codec_type", "");
        Json entry{{"index", stream.value("index", -1)}, {"type", type},
                   {"codec_tag", stream.value("codec_tag_string", "")}};
        if (type == "audio" || (type == "video" && !selectedVideo)) {
            if (type == "video") selectedVideo = true;
            entry["operation"] = type == "audio" ? "stream copy" : "re-encode selected v:0";
            result["preserved_streams"].push_back(entry);
            continue;
        }
        if (stream.value("codec_tag_string", "") == "tmcd" && !in.timecode.empty() && !selectedTimecode) {
            selectedTimecode = true;
            entry["operation"] = "one timecode track regenerated from selected source timecode";
            result["regenerated_streams"].push_back(entry);
            continue;
        }
        entry["reason"] = type == "video" ? "additional attached picture; only v:0 is encoded"
                                           : "unmapped auxiliary/subtitle/data/timecode track";
        result["removed_streams"].push_back(entry);
    }
    return result;
}
} // namespace logforge
