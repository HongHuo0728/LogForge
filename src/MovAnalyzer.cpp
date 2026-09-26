#include "logforge/Media.h"
#include <bit>
#include <cmath>
#include <map>

namespace logforge {
namespace {
uint32_t U32(const unsigned char* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
uint64_t U64(const unsigned char* p) {
    return (uint64_t(U32(p)) << 32) | U32(p + 4);
}
std::string Hex(const std::vector<unsigned char>& b) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string s;
    s.reserve(b.size() * 2);
    for (auto c : b) {
        s += digits[c >> 4];
        s += digits[c & 15];
    }
    return s;
}
std::string FourCC(uint32_t n) {
    std::vector<unsigned char> b{static_cast<unsigned char>(n >> 24), static_cast<unsigned char>(n >> 16),
                                 static_cast<unsigned char>(n >> 8), static_cast<unsigned char>(n)};
    if (std::all_of(b.begin(), b.end(), [](auto c) { return c >= 32 && c < 127; }))
        return std::string(b.begin(), b.end());
    return "0x" + Hex(b);
}
struct Box {
    uint64_t offset, size, header;
    uint32_t id;
    std::string type;
    uint64_t Begin() const {
        return offset + header;
    }
    uint64_t End() const {
        return offset + size;
    }
};
class Reader {
    std::ifstream file;
    Json atoms = Json::array(), metadata = Json::array(), entries = Json::array(),
         videoTerminators = Json::array();
    size_t count = 0;
    std::vector<unsigned char> Read(uint64_t at, uint64_t size) {
        if (size > 4 * 1024 * 1024)
            throw AppError(TextId::MovMetadataLimit);
        std::vector<unsigned char> b(static_cast<size_t>(size));
        file.seekg(static_cast<std::streamoff>(at));
        file.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(size));
        if (!file)
            throw AppError(TextId::MovTruncated);
        return b;
    }
    std::vector<Box> Boxes(uint64_t begin, uint64_t end, bool videoExtensions = false) {
        std::vector<Box> boxes;
        for (auto pos = begin; pos < end;) {
            // QTFF video sample descriptions may end with exactly four zero
            // bytes. This is not an atom and is not permitted in other scopes.
            if (videoExtensions && end - pos == 4 && U32(Read(pos, 4).data()) == 0)
                break;
            if (end - pos < 8)
                throw AppError(TextId::MovTruncated);
            auto b = Read(pos, 8);
            uint64_t size = U32(b.data()), header = 8;
            if (size == 1) {
                if (end - pos < 16)
                    throw AppError(TextId::MovExtended);
                size = U64(Read(pos + 8, 8).data());
                header = 16;
            }
            if (size == 0)
                size = end - pos;
            if (size < header || size > end - pos)
                throw AppError(TextId::MovSize);
            if (++count > 100000)
                throw AppError(TextId::MovCount);
            boxes.push_back({pos, size, header, U32(b.data() + 4), FourCC(U32(b.data() + 4))});
            pos += size;
        }
        return boxes;
    }
    Json Value(const Box& box) {
        auto b = Read(box.Begin(), box.size - box.header);
        if (b.size() < 8)
            throw AppError(TextId::MovTruncated);
        const auto type = U32(b.data()) & 0xffffff;
        Json result{{"type", type}, {"type_flags", b[0]}, {"locale", U32(b.data() + 4)}};
        std::vector<unsigned char> payload(b.begin() + 8, b.end());
        result["raw_hex"] = Hex(payload);
        if (type == 1 || type == 4) {
            std::string s(payload.begin(), payload.end());
            if (!s.empty() && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                                   static_cast<int>(s.size()), nullptr, 0))
                result["decode_error"] = "Invalid UTF-8";
            else
                result["value"] = s;
        } else if ((type == 2 || type == 5) && payload.size() % 2 == 0) {
            std::wstring text;
            for (size_t i = 0; i < payload.size(); i += 2)
                text += static_cast<wchar_t>((payload[i] << 8) | payload[i + 1]);
            result["value"] = Utf8(text);
        } else if ((type == 21 || type == 22 || type == 65 || type == 66 || type == 67 || type == 74 ||
                    type == 75 || type == 76 || type == 77 || type == 78) &&
                   (payload.size() == 1 || payload.size() == 2 || payload.size() == 4 ||
                    payload.size() == 8)) {
            uint64_t n = 0;
            for (auto c : payload)
                n = (n << 8) | c;
            if (type == 21 || type == 65 || type == 66 || type == 67 || type == 74) {
                if (payload.size() < 8 && (payload[0] & 128))
                    n |= ~uint64_t{0} << (payload.size() * 8);
                result["value"] = std::bit_cast<int64_t>(n);
            } else
                result["value"] = n;
        } else if ((type == 23 && payload.size() == 4) || (type == 24 && payload.size() == 8)) {
            const double v = type == 23 ? static_cast<double>(std::bit_cast<float>(U32(payload.data())))
                                        : std::bit_cast<double>(U64(payload.data()));
            if (std::isfinite(v))
                result["value"] = v;
            else
                result["decode_error"] = "Non-finite float";
        } else
            result["encoding"] = "binary (raw_hex)";
        return result;
    }
    void Walk(uint64_t begin, uint64_t end, const std::string& parent, int depth,
              const std::vector<Json>& inheritedKeys = {}, const std::string& metadataScope = "",
              bool videoExtensions = false) {
        if (depth > 20)
            throw AppError(TextId::MovNesting);
        const auto boxes = Boxes(begin, end, videoExtensions);
        if (videoExtensions && end - (boxes.empty() ? begin : boxes.back().End()) == 4)
            videoTerminators.push_back({{"sample_entry", parent}, {"offset", end - 4}, {"bytes", 4}});
        std::vector<Json> keys = inheritedKeys;
        // Resolve keys before values even when ilst precedes keys in file order.
        for (const auto& box : boxes)
            if (box.type == "keys") {
                auto b = Read(box.Begin(), box.size - box.header);
                if (b.size() < 8)
                    throw AppError(TextId::MovTruncated);
                const auto n = U32(b.data() + 4);
                if (n > 65536)
                    throw AppError(TextId::MovCount);
                size_t pos = 8;
                keys.clear();
                for (uint32_t i = 0; i < n; ++i) {
                    if (b.size() - pos < 8)
                        throw AppError(TextId::MovTruncated);
                    auto size = U32(b.data() + pos);
                    if (size < 8 || size > b.size() - pos)
                        throw AppError(TextId::MovSize);
                    keys.push_back({{"namespace", FourCC(U32(b.data() + pos + 4))},
                                    {"name", std::string(b.begin() + pos + 8, b.begin() + pos + size)}});
                    pos += size;
                }
                if (pos != b.size())
                    throw AppError(TextId::MovSize);
            }
        std::map<std::string, int> ordinals;
        for (const auto& box : boxes) {
            const auto path = parent + "/" + box.type + "[" + std::to_string(ordinals[box.type]++) + "]";
            Json item{{"path", path},
                      {"type", box.type},
                      {"offset", box.offset},
                      {"size", box.size},
                      {"header_size", box.header}};
            const auto available = box.size - box.header;
            const auto prores = box.type == "apch" || box.type == "apcn" || box.type == "apcs" ||
                                box.type == "apco" || box.type == "ap4h" || box.type == "ap4x";
            if (box.type == "mvhd" || box.type == "mdhd" || box.type == "tkhd") {
                auto b = Read(box.Begin(), std::min<uint64_t>(available, 112));
                if (b.size() < 24 || b[0] > 1)
                    throw AppError(TextId::MovTruncated);
                const bool wide = b[0] == 1;
                item["version"] = b[0];
                const size_t t = wide ? 20 : 12;
                if (b.size() < t + (wide ? 16u : 12u))
                    throw AppError(TextId::MovTruncated);
                item["creation_time_1904_seconds"] = wide ? U64(b.data() + 4) : U32(b.data() + 4);
                if (box.type != "tkhd") {
                    item["timescale"] = U32(b.data() + t);
                    item["duration_ticks"] = wide ? U64(b.data() + t + 4) : U32(b.data() + t + 4);
                    if (box.type == "mvhd") {
                        const size_t matrix = wide ? 48 : 36;
                        if (b.size() < matrix + 36)
                            throw AppError(TextId::MovTruncated);
                        item["display_matrix"] = Json::array();
                        for (size_t n = 0; n < 9; ++n)
                            item["display_matrix"].push_back(
                                std::bit_cast<int32_t>(U32(b.data() + matrix + n * 4)));
                    }
                } else {
                    item["track_id"] = U32(b.data() + t);
                    const size_t matrix = wide ? 52 : 40;
                    if (b.size() < matrix + 36)
                        throw AppError(TextId::MovTruncated);
                    item["display_matrix"] = Json::array();
                    for (size_t n = 0; n < 9; ++n)
                        item["display_matrix"].push_back(
                            std::bit_cast<int32_t>(U32(b.data() + matrix + n * 4)));
                }
            }
            if (box.type == "elst") {
                auto b = Read(box.Begin(), available);
                if (b.size() < 8 || b[0] > 1)
                    throw AppError(TextId::MovTruncated);
                const size_t n = U32(b.data() + 4), stride = b[0] ? 20 : 12;
                if (n > 65536 || b.size() != 8 + n * stride)
                    throw AppError(TextId::MovSize);
                item["edits"] = Json::array();
                for (size_t i = 0; i < n; ++i) {
                    const auto* p = b.data() + 8 + i * stride;
                    item["edits"].push_back(
                        {{"duration_ticks", b[0] ? U64(p) : U32(p)},
                         {"media_time", b[0] ? std::bit_cast<int64_t>(U64(p + 8))
                                             : static_cast<int64_t>(std::bit_cast<int32_t>(U32(p + 4)))},
                         {"rate_integer", static_cast<int16_t>((p[stride - 4] << 8) | p[stride - 3])},
                         {"rate_fraction", static_cast<int16_t>((p[stride - 2] << 8) | p[stride - 1])}});
                }
            }
            if (box.type == "keys")
                item["keys"] = keys;
            if (box.type == "hdlr") {
                auto b = Read(box.Begin(), std::min<uint64_t>(available, 32));
                if (b.size() >= 12)
                    item["handler_type"] = FourCC(U32(b.data() + 8));
            }
            if (box.type == "colr") {
                auto b = Read(box.Begin(), std::min<uint64_t>(available, 11));
                if (b.size() < 4)
                    throw AppError(TextId::MovTruncated);
                item["color_type"] = FourCC(U32(b.data()));
                if (item["color_type"] == "nclc" || item["color_type"] == "nclx") {
                    if (b.size() < 10)
                        throw AppError(TextId::MovTruncated);
                    item["primaries"] = (b[4] << 8) | b[5];
                    item["transfer"] = (b[6] << 8) | b[7];
                    item["matrix"] = (b[8] << 8) | b[9];
                    if (item["color_type"] == "nclx") {
                        if (b.size() < 11)
                            throw AppError(TextId::MovTruncated);
                        item["full_range"] = (b[10] & 128) != 0;
                    }
                }
            } else if (box.type == "pasp" || box.type == "gama" || box.type == "fiel" || box.type == "clap" ||
                       box.type == "mdcv" || box.type == "clli") {
                auto b = Read(box.Begin(), available);
                item["payload_hex"] = Hex(b);
                if (box.type == "pasp" && b.size() == 8) {
                    item["horizontal_spacing"] = U32(b.data());
                    item["vertical_spacing"] = U32(b.data() + 4);
                }
                if (box.type == "gama" && b.size() == 4)
                    item["gamma"] = U32(b.data()) / 65536.;
                if (box.type == "fiel" && b.size() == 2) {
                    item["fields"] = b[0];
                    item["detail"] = b[1];
                }
                if (box.type == "clap" && b.size() == 32) {
                    item["clean_aperture_rationals"] = Json::array();
                    for (size_t i = 0; i < 32; i += 8)
                        item["clean_aperture_rationals"].push_back(
                            {U32(b.data() + i), U32(b.data() + i + 4)});
                }
            } else if (box.type == "logs" && videoExtensions) {
                // Observed verbatim in the unmodified iPhone 15 Pro Max /
                // Blackmagic Camera reference. No FullBox header or NUL suffix.
                // Preserve raw bytes even for an unknown future identifier.
                auto b = Read(box.Begin(), available);
                item["payload_hex"] = Hex(b);
                std::string identifier(b.begin(), b.end());
                if (!identifier.empty() && identifier.find('\0') == std::string::npos &&
                    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, identifier.data(),
                                        static_cast<int>(identifier.size()), nullptr, 0))
                    item["log_transfer_function"] = identifier;
                else
                    item["decode_error"] = "Invalid log transfer identifier";
            } else if (prores) {
                if (available < 78)
                    throw AppError(TextId::MovTruncated);
                auto b = Read(box.Begin(), 78);
                item["data_reference_index"] = (b[6] << 8) | b[7];
                item["sample_entry_version"] = (b[8] << 8) | b[9];
                item["vendor"] = FourCC(U32(b.data() + 12));
                item["width"] = (b[24] << 8) | b[25];
                item["height"] = (b[26] << 8) | b[27];
                item["horizontal_resolution"] = U32(b.data() + 28) / 65536.;
                item["vertical_resolution"] = U32(b.data() + 32) / 65536.;
                item["compressor_name"] =
                    std::string(b.begin() + 43, b.begin() + 43 + std::min<unsigned>(b[42], 31));
                item["depth"] = (b[74] << 8) | b[75];
                entries.push_back(item);
            }
            atoms.push_back(item);
            if (box.type == "meta") {
                if (available < 8)
                    throw AppError(TextId::MovTruncated);
                auto b = Read(box.Begin(), 8);
                const uint64_t skip = U32(b.data()) == 0 ? 4 : 0;
                Walk(box.Begin() + skip, box.End(), path, depth + 1, {}, path);
            } else if (box.type == "ilst") {
                for (const auto& entry : Boxes(box.Begin(), box.End())) {
                    Json resolved{
                        {"scope", metadataScope}, {"key_index", entry.id}, {"values", Json::array()}};
                    if (entry.id > 0 && entry.id <= keys.size()) {
                        resolved["key"] = keys[entry.id - 1]["name"];
                        resolved["namespace"] = keys[entry.id - 1]["namespace"];
                    } else {
                        resolved["key"] = entry.type;
                        resolved["namespace"] = keys.empty() ? "legacy-fourcc" : "unresolved-index";
                    }
                    for (const auto& value : Boxes(entry.Begin(), entry.End())) {
                        atoms.push_back({{"path", path + "/" + entry.type + "/" + value.type},
                                         {"type", value.type},
                                         {"offset", value.offset},
                                         {"size", value.size}});
                        if (value.type == "data")
                            resolved["values"].push_back(Value(value));
                        else if (value.type == "name" || value.type == "mean") {
                            auto b = Read(value.Begin(), value.size - value.header);
                            if (b.size() < 4)
                                throw AppError(TextId::MovTruncated);
                            resolved[value.type] = std::string(b.begin() + 4, b.end());
                        }
                    }
                    if (resolved.contains("mean") && resolved.contains("name")) {
                        resolved["namespace"] = resolved["mean"];
                        resolved["key"] = resolved["name"];
                    }
                    metadata.push_back(resolved);
                }
            } else if (box.type == "stsd") {
                if (available < 8)
                    throw AppError(TextId::MovTruncated);
                Walk(box.Begin() + 8, box.End(), path, depth + 1, keys, metadataScope);
            } else if (prores)
                Walk(box.Begin() + 78, box.End(), path, depth + 1, keys, metadataScope, true);
            else if (box.type == "moov" || box.type == "trak" || box.type == "mdia" || box.type == "minf" ||
                     box.type == "stbl" || box.type == "udta" || box.type == "dinf" || box.type == "edts" ||
                     box.type == "tref")
                Walk(box.Begin(), box.End(), path, depth + 1, keys, metadataScope);
            else if (parent.find("/apch[") != std::string::npos ||
                     parent.find("/apcn[") != std::string::npos) {
                if (available <= 65536)
                    atoms.back()["uninterpreted_payload_hex"] = Hex(Read(box.Begin(), available));
                else
                    atoms.back()["uninterpreted_payload_bytes"] = available;
            }
        }
    }

  public:
    explicit Reader(const fs::path& p) : file(p, std::ios::binary) {
        if (!file)
            throw AppError(TextId::MovRead);
    }
    Json Analyze(uint64_t size) {
        Walk(0, size, "", 0);
        Json semantics{{"metadata", Json::object()},
                       {"sample_entries", Json::array()},
                       {"color_and_extensions", Json::array()}};
        for (auto m : metadata) {
            const auto key = m["scope"].get<std::string>() + "/" + m["namespace"].get<std::string>() + ":" +
                             m["key"].get<std::string>();
            m.erase("key_index");
            if (!semantics["metadata"].contains(key))
                semantics["metadata"][key] = Json::array();
            semantics["metadata"][key].push_back(m);
        }
        for (auto e : entries) {
            e.erase("offset");
            e.erase("size");
            semantics["sample_entries"].push_back(e);
        }
        for (auto a : atoms)
            if (a.contains("color_type") || a.contains("payload_hex") ||
                a.contains("uninterpreted_payload_hex")) {
                a.erase("offset");
                a.erase("size");
                semantics["color_and_extensions"].push_back(a);
            }
        return {{"atoms", atoms},
                {"bytes", size},
                {"metadata", metadata},
                {"sample_entries", entries},
                {"video_sample_terminators", videoTerminators},
                {"semantic", semantics}};
    }
};
} // namespace
Json ReferenceMovAnalyzer::Analyze(const fs::path& path) {
    return Reader(path).Analyze(fs::file_size(path));
}
Json ReferenceMovAnalyzer::SemanticDiff(const Json& a, const Json& b) {
    auto first = a.at("semantic"), second = b.at("semantic");
    auto probeSummary = [](const Json& j) {
        Json r = Json::object();
        if (!j.contains("ffprobe"))
            return r;
        const auto m = MediaInfo::Parse(j["ffprobe"]);
        r = {{"codec", m.codec},
             {"profile", m.profile},
             {"width", m.width},
             {"height", m.height},
             {"pixel_format", m.pixelFormat},
             {"primaries", m.primaries},
             {"transfer", m.transfer},
             {"matrix", m.matrix},
             {"range", m.range},
             {"chroma_location", m.chromaLocation},
             {"avg_fps", m.averageFps.Value()},
             {"nominal_fps", m.nominalFps.Value()},
             {"timecode", m.timecode},
             {"rotation", m.rotation},
             {"format_tags", m.tags},
             {"video_tags", m.videoTags},
             {"audio", Json::array()}};
        for (const auto& audio : m.audio)
            r["audio"].push_back({{"codec", audio.codec},
                                  {"channels", audio.channels},
                                  {"sample_rate", audio.sampleRate},
                                  {"layout", audio.layout},
                                  {"tags", audio.tags}});
        return r;
    };
    first["stream_parameters"] = probeSummary(a);
    second["stream_parameters"] = probeSummary(b);
    return Json::diff(first, second);
}
} // namespace logforge
