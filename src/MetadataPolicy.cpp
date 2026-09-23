#include "logforge/Media.h"
#include <set>

namespace logforge {
namespace {
std::string Lower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
const std::set<std::string> Safe{"creation_time",
                                 "com.apple.quicktime.creationdate",
                                 "com.apple.quicktime.make",
                                 "com.apple.quicktime.model",
                                 "make",
                                 "model",
                                 "timecode",
                                 "language"};
} // namespace
bool AppleLogMetadataWriter::IsConflict(const std::string& key) {
    const auto s = Lower(key);
    if (s.starts_with("logforge."))
        return false;
    for (const auto* word : {"hlg", "hdr", "dolby", "dovi", "pq", "gamma", "transfer", "colorspace",
                             "color_space", "colorprimaries", "color_primaries", "matrixcoefficients",
                             "mastering", "content_light", "content light", "color_range", "apple-log"})
        if (s.find(word) != std::string::npos)
            return true;
    return false;
}
Json AppleLogMetadataWriter::CopyPlan(const MediaInfo& in) {
    Json result{{"policy", "whitelist-v1"}, {"preserved", Json::array()}, {"removed", Json::array()}};
    const auto collect = [&](const Json& tags, const std::string& scope) {
        for (const auto& [key, value] : tags.items()) {
            const bool safe = Safe.contains(Lower(key)) && value.is_string() && !IsConflict(key);
            Json entry{{"scope", scope}, {"key", key}, {"value", value}};
            // MOV's ordinary video stream tag writer does not serialize arbitrary
            // camera keys. Promote a real source value to mdta without inventing it.
            if (safe && scope == "v:0" && Lower(key) != "creation_time" && Lower(key) != "timecode" &&
                Lower(key) != "language" && (!in.tags.contains(key) || in.tags[key] == value))
                entry["write_scope"] = "format";
            if (!safe)
                entry["reason"] = IsConflict(key) ? "color metadata conflict" : "not whitelisted";
            result[safe ? "preserved" : "removed"].push_back(entry);
        }
    };
    collect(in.tags, "format");
    collect(in.videoTags, "v:0");
    for (size_t i = 0; i < in.audio.size(); ++i)
        collect(in.audio[i].tags, "a:" + std::to_string(i));
    return result;
}
} // namespace logforge
