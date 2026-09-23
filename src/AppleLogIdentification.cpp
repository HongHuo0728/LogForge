#include "logforge/AppleLogIdentification.h"
#include <array>
#include <limits>

namespace logforge {
namespace {
[[noreturn]] void Fail(const std::string& detail) {
    throw AppError(Message(TextId::IdentificationWriteFailed, {detail}));
}
void CheckCancel(const std::atomic_bool& cancel) {
    if (cancel.load())
        throw AppError(TextId::Cancelled);
}
void At(HANDLE file, uint64_t offset) {
    LARGE_INTEGER pos{};
    pos.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file, pos, nullptr, FILE_BEGIN))
        Fail(Utf8(WinError()));
}
void ReadAt(HANDLE file, uint64_t offset, void* bytes, DWORD size) {
    At(file, offset);
    DWORD got = 0;
    if (!ReadFile(file, bytes, size, &got, nullptr) || got != size)
        Fail("Cannot read the complete encoded MOV header.");
}
void WriteAt(HANDLE file, uint64_t offset, const void* bytes, DWORD size) {
    At(file, offset);
    DWORD written = 0;
    if (!WriteFile(file, bytes, size, &written, nullptr) || written != size)
        Fail("Cannot write the complete identification metadata: " + Utf8(WinError()));
}
uint32_t U32(const unsigned char* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
std::array<unsigned char, 8> BE(uint64_t n) {
    std::array<unsigned char, 8> bytes{};
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[7 - i] = static_cast<unsigned char>(n >> (i * 8));
    return bytes;
}
bool DirectChild(const Json& atom, const std::string& parent) {
    const auto path = atom.value("path", "");
    return path.starts_with(parent + "/") && path.find('/', parent.size() + 1) == std::string::npos;
}
} // namespace

Json AppleLogIdentificationWriter::Validate(const Json& analysis) {
    const auto& entries = analysis.at("sample_entries");
    bool valid = entries.size() == 1 && entries[0].value("type", "") == "apch";
    const std::string path = entries.size() == 1 ? entries[0].value("path", "") : "";
    int logs = 0, colors = 0;
    for (const auto& a : analysis.at("atoms")) {
        const auto type = a.value("type", "");
        if (type == "logs") {
            ++logs;
            valid &= DirectChild(a, path) && a.value("log_transfer_function", "") == Identifier &&
                     a.value("size", uint64_t{}) == Identifier.size() + 8;
        }
        if (type == "colr") {
            ++colors;
            valid &= DirectChild(a, path) && a.value("color_type", "") == "nclc" &&
                     a.value("primaries", 0) == 9 && a.value("transfer", 0) == 2 && a.value("matrix", 0) == 9;
        }
        if (DirectChild(a, path) && (type == "gama" || type == "mdcv" || type == "clli"))
            valid = false;
    }
    valid &= logs == 1 && colors == 1;
    return {{"passed", valid},
            {"writer", "prores-sample-entry-logs-v1"},
            {"identifier", Identifier},
            {"sample_entry", path},
            {"colr", "nclc 9/2/9"},
            {"reference_sha256", "204a47fba09ff1c69e47936caf6bf1537f7d770474837a057deabedc241d2d42"},
            {"compatibility_test", "DaVinci Resolve Studio 20.3.2.9 / Windows / RCM; 2026-09-23"},
            {"this_file_imported_in_editor", false}};
}

void AppleLogIdentificationWriter::WriteToEncodedPartial(const fs::path& partial,
                                                         const std::atomic_bool& cancel) {
    CheckCancel(cancel);
    // Hold a deny-write/delete handle throughout analysis and mutation. Readers
    // may inspect the temporary file; publication is still owned by TranscodeJob.
    Handle file(CreateFileW(partial.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
        Fail(Utf8(WinError()));
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file.get(), &length) || length.QuadPart < 8)
        Fail("Invalid encoded MOV length.");
    const auto size = static_cast<uint64_t>(length.QuadPart);
    const auto analysis = ReferenceMovAnalyzer::Analyze(partial);
    const auto& entries = analysis.at("sample_entries");
    if (entries.size() != 1 || entries[0].value("type", "") != "apch")
        Fail("Expected one ProRes 422 HQ sample entry.");
    if (Validate(analysis).at("passed").get<bool>())
        return; // Idempotent; do not add duplicate identification atoms.
    const auto path = entries[0].at("path").get<std::string>();
    std::vector<Json> parents;
    int moov = 0, colr = 0;
    for (const auto& a : analysis.at("atoms")) {
        const auto type = a.value("type", ""), p = a.value("path", "");
        if (type == "logs")
            Fail("Existing, conflicting or duplicated Log identification metadata.");
        if (type == "moof")
            Fail("Fragmented MOV is not an encoded LogForge partial.");
        if (type == "moov") {
            ++moov;
            if (p != "/moov[0]" || a.at("offset").get<uint64_t>() + a.at("size").get<uint64_t>() != size)
                Fail("The encoded MOV must have one trailing moov; no media offsets will be moved.");
        }
        if (type == "colr") {
            ++colr;
            if (!DirectChild(a, path) || a.value("color_type", "") != "nclc" ||
                a.value("primaries", 0) != 9 || a.value("transfer", 0) != 2 || a.value("matrix", 0) != 9)
                Fail("Expected unchanged BT.2020 nclc 9/2/9 color declaration.");
        }
        if (DirectChild(a, path) && (type == "gama" || type == "mdcv" || type == "clli"))
            Fail("Conflicting transfer or HDR sample-entry extension.");
        if (p == path || path.starts_with(p + "/"))
            parents.push_back(a);
    }
    if (moov != 1 || colr != 1 || parents.size() != 7)
        Fail("Unexpected encoded MOV sample-entry hierarchy.");
    // Exact bytes from the native reference, also confirmed by isolated Resolve
    // A/B import. This is a plain UTF-8 identifier, NOT a FullBox or a H.273 ID.
    std::vector<unsigned char> atom{0, 0, 0, 35, 'l', 'o', 'g', 's'};
    atom.insert(atom.end(), Identifier.begin(), Identifier.end());
    const auto added = static_cast<uint64_t>(atom.size());
    if (size > static_cast<uint64_t>(std::numeric_limits<LONGLONG>::max()) - added)
        Fail("MOV length overflow.");
    struct SizePatch {
        uint64_t offset, size;
        bool extended;
    };
    std::vector<SizePatch> patches;
    for (const auto& parent : parents) {
        const auto offset = parent.at("offset").get<uint64_t>();
        const auto oldSize = parent.at("size").get<uint64_t>();
        std::array<unsigned char, 4> header{};
        ReadAt(file.get(), offset, header.data(), 4);
        const auto tag = U32(header.data());
        if (tag == 0 || (tag != 1 && oldSize + added > UINT32_MAX))
            Fail("Unsupported MOV ancestor size encoding.");
        patches.push_back({offset, oldSize + added, tag == 1});
    }
    uint64_t insert = entries[0].at("offset").get<uint64_t>() + entries[0].at("size").get<uint64_t>();
    for (const auto& t : analysis.at("video_sample_terminators"))
        if (t.at("sample_entry") == path)
            insert = t.at("offset").get<uint64_t>();
    // Shift only the tail of moov backwards in bounded chunks. mdat bytes,
    // packet offsets, audio, timecode and rotation are never changed or remuxed.
    std::vector<unsigned char> buffer(1024 * 1024);
    for (uint64_t end = size; end > insert;) {
        CheckCancel(cancel);
        const auto n = static_cast<DWORD>(std::min<uint64_t>(buffer.size(), end - insert));
        const auto begin = end - n;
        ReadAt(file.get(), begin, buffer.data(), n);
        WriteAt(file.get(), begin + added, buffer.data(), n);
        end = begin;
    }
    CheckCancel(cancel);
    WriteAt(file.get(), insert, atom.data(), static_cast<DWORD>(atom.size()));
    for (const auto& patch : patches) {
        const auto bytes = BE(patch.size);
        WriteAt(file.get(), patch.offset + (patch.extended ? 8 : 0), bytes.data() + (patch.extended ? 0 : 4),
                patch.extended ? 8 : 4);
    }
    if (!FlushFileBuffers(file.get()))
        Fail(Utf8(WinError()));
}
} // namespace logforge
