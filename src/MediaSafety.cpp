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
bool SupportedDisplayMatrix(const std::string& matrix) {
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
    if (v.size() != 9 || v[2] != 0 || v[5] != 0 || v[8] != 1073741824 || v[6] != 0 || v[7] != 0)
        return false;
    // Only the four exact unit rotations. Reflections and translated/cropped
    // matrices are not reduced to an angle and silently discarded.
    for (const auto& r : {std::array<int64_t, 4>{65536, 0, 0, 65536},
                          {0, -65536, 65536, 0},
                          {-65536, 0, 0, -65536},
                          {0, 65536, -65536, 0}})
        if (v[0] == r[0] && v[1] == r[1] && v[3] == r[2] && v[4] == r[3])
            return true;
    return false;
}
Json PreserveMovCreationTimes(const Json& source, const fs::path& partial, const std::atomic_bool& cancel) {
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
        if (!p.at("verified").get<bool>())
            throw AppError(
                Message(TextId::OutputMetadataPreserve, {key + ": creation_time header verification"}));
        p.erase("offset");
    }
    return patches;
}
Json InspectMovTimeline(const MediaInfo& in, const Json& analysis, const fs::path& ffprobe,
                        const std::atomic_bool* cancel) {
    Json result{{"chapters", "copied and validated"},
                {"edit_lists", Json::array()},
                {"removed_streams", Json::array()}};
    for (const auto& a : analysis.at("atoms")) {
        if (a.value("type", "") == "mvhd" && a.contains("display_matrix") &&
            a.at("display_matrix") != Json::array({65536, 0, 0, 0, 65536, 0, 0, 0, 1073741824}))
            throw AppError(TextId::InputDisplayMatrix);
        if (a.value("type", "") == "moof")
            throw AppError(Message(TextId::InputEditList, {"fragmented MOV"}));
        if (a.value("type", "") != "elst")
            continue;
        const auto& edits = a.at("edits");
        // A single unit-rate, zero-origin entry is a muxer duration declaration.
        // Trims, empty edits, repeats, speed changes and segmented timelines
        // require a timeline-aware remux and are rejected before encoding.
        if (edits.size() != 1 || edits[0].at("media_time").get<int64_t>() < 0 ||
            edits[0].at("rate_integer") != 1 || edits[0].at("rate_fraction") != 0)
            throw AppError(Message(TextId::InputEditList, {a.at("path").get<std::string>()}));
        const auto path = a.at("path").get<std::string>();
        const auto track = path.substr(0, path.find("/edts"));
        uint64_t trackId = 0, movieScale = 0, mediaScale = 0, duration = 0;
        for (const auto& header : analysis.at("atoms")) {
            const auto p = header.value("path", "");
            if (header.value("type", "") == "mvhd")
                movieScale = header.value("timescale", uint64_t{});
            if (p.starts_with(track + "/")) {
                if (header.value("type", "") == "tkhd")
                    trackId = header.value("track_id", uint64_t{});
                if (header.value("type", "") == "mdhd") {
                    mediaScale = header.value("timescale", uint64_t{});
                    duration = header.value("duration_ticks", uint64_t{});
                }
            }
        }
        const auto mediaTime = edits[0].at("media_time").get<uint64_t>();
        if (!movieScale || !mediaScale || duration < mediaTime ||
            std::abs(static_cast<long double>(edits[0].at("duration_ticks").get<uint64_t>()) -
                     static_cast<long double>(duration - mediaTime) * movieScale / mediaScale) > 1.0L)
            throw AppError(Message(TextId::InputEditList, {path + ": trimmed or inconsistent duration"}));
        if (mediaTime) {
            // Preserve AAC encoder priming only when the actual first packet
            // confirms exactly this skip. Positive edits are otherwise trims.
            int streamIndex = -1;
            for (const auto& s : in.raw.value("streams", Json::array())) {
                const auto id = s.value("id", std::string());
                if (!id.empty() && std::stoull(id, nullptr, 0) == trackId &&
                    s.value("codec_name", "") == "aac")
                    streamIndex = s.value("index", -1);
            }
            if (streamIndex < 0 || ffprobe.empty())
                throw AppError(Message(TextId::InputEditList, {path + ": nonzero media origin"}));
            auto p = RunProcess(ffprobe,
                                {L"-v", L"error", L"-select_streams", std::to_wstring(streamIndex),
                                 L"-read_intervals", L"%+#1", L"-show_packets", L"-of", L"json",
                                 in.path.wstring()},
                                cancel, 30);
            bool priming = false;
            if (!p.exitCode)
                for (const auto& packet : Json::parse(p.output).value("packets", Json::array()))
                    for (const auto& side : packet.value("side_data_list", Json::array()))
                        priming |= side.value("side_data_type", "") == "Skip Samples" &&
                                   side.value("skip_samples", uint64_t{}) == mediaTime;
            if (!priming)
                throw AppError(Message(TextId::InputEditList,
                                       {path + ": priming not confirmed by packet skip samples"}));
        }
        result["edit_lists"].push_back({{"path", a.at("path")},
                                        {"policy", mediaTime ? "packet-verified AAC priming copied"
                                                             : "zero-origin, unit-rate duration declaration"},
                                        {"edits", edits}});
    }
    for (const auto& s : in.raw.value("streams", Json::array())) {
        const auto type = s.value("codec_type", "");
        if (type == "video" || type == "audio")
            continue;
        if (s.value("codec_tag_string", "") == "tmcd")
            continue;
        // Text chapters are regenerated from the chapter list; timed metadata
        // does not participate in the accepted video/audio timeline.
        result["removed_streams"].push_back(
            {{"index", s.value("index", -1)},
             {"type", type},
             {"codec_tag", s.value("codec_tag_string", "")},
             {"reason", "not video/audio/timecode; chapters copied separately"}});
    }
    return result;
}
} // namespace logforge
