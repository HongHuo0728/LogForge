#include "logforge/AppleLogIdentification.h"
#include <iostream>

using namespace logforge;
namespace {
void Check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
std::string BE(uint64_t n, int bytes = 4) {
    std::string s;
    for (int i = bytes - 1; i >= 0; --i)
        s += static_cast<char>(n >> (i * 8));
    return s;
}
std::string Atom(const std::string& t, const std::string& p, bool extended = false) {
    return extended ? BE(1) + t + BE(p.size() + 16, 8) + p : BE(p.size() + 8) + t + p;
}
const std::string Color = Atom("colr", "nclc" + std::string("\0\x09\0\x02\0\x09", 6));
const std::string Log = Atom("logs", std::string(AppleLogIdentificationWriter::Identifier));
std::string Movie(const std::string& extensions, const std::string& suffix = {}, bool extended = false,
                  const std::string& codec = "apch") {
    auto p = Atom(codec, std::string(78, 0) + extensions);
    p = Atom("stsd", BE(0) + BE(1) + p);
    p = Atom("stbl", p + Atom("stco", BE(0) + BE(1) + BE(32)), extended);
    for (const auto* t : {"minf", "mdia", "trak"})
        p = Atom(t, p);
    return Atom("moov", p + suffix, extended);
}
void Save(const fs::path& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    f.close();
    Check(bool(f), "Cannot save fixture");
}
std::string Load(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
} // namespace

int main() {
    const auto root = DataDirectory() / (L"identification-" + std::to_wstring(GetCurrentProcessId()));
    const auto file = root / L"synthetic.partial.mov";
    try {
        fs::create_directories(root);
        std::atomic_bool cancel = false;
        // Large tail exercises backwards chunk overlap, with extended-size mdat
        // and ancestors. stco is deliberately nonzero and must stay byte-exact.
        const auto media = Atom("mdat", std::string(4096, 'M'), true);
        const auto tail = Atom("free", std::string(3 * 1024 * 1024 + 79, 'T'));
        for (bool terminator : {false, true})
            for (bool extended : {false, true}) {
                const auto terminatorBytes = terminator ? BE(0) : std::string{};
                Save(file, media + Movie(Color + terminatorBytes, tail, extended));
                Check(!AppleLogIdentificationWriter::Validate(ReferenceMovAnalyzer::Analyze(file))["passed"],
                      "Missing identification validated");
                AppleLogIdentificationWriter::WriteToEncodedPartial(file, cancel);
                Check(Load(file) == media + Movie(Color + Log + terminatorBytes, tail, extended),
                      "Writer changed media, chunk offsets, unknown data or parent sizes");
                Check(AppleLogIdentificationWriter::Validate(ReferenceMovAnalyzer::Analyze(file))["passed"],
                      "Written identification did not validate");
                const auto hash = SHA256(file);
                AppleLogIdentificationWriter::WriteToEncodedPartial(file, cancel);
                Check(SHA256(file) == hash, "Idempotent write changed the file");
            }
        const auto badColor = Atom("colr", "nclc" + std::string("\0\x09\0\x12\0\x09", 6));
        for (const auto& invalid :
             {media + Movie(Color + Atom("logs", "wrong.identifier")), media + Movie(Color + Log + Log),
              media + Movie(badColor), media + Movie(Color + Color), media + Movie(""),
              media + Movie(Color, "", false, "apcn"), Movie(Color) + media,
              media + Movie(Color + Atom("gama", BE(65536)))}) {
            Save(file, invalid);
            bool failed = false;
            try {
                AppleLogIdentificationWriter::WriteToEncodedPartial(file, cancel);
            } catch (const AppError&) {
                failed = true;
            }
            Check(failed && Load(file) == invalid, "Invalid MOV mutated or accepted");
        }
        Save(file, media + Movie(Color));
        const auto before = SHA256(file);
        cancel = true;
        bool cancelled = false;
        try {
            AppleLogIdentificationWriter::WriteToEncodedPartial(file, cancel);
        } catch (const AppError& e) {
            cancelled = e.message.id == TextId::Cancelled;
        }
        Check(cancelled && SHA256(file) == before, "Cancelled writer modified MOV");
        fs::remove(file);
        fs::remove(root);
        std::cout << "PASS: identification, byte preservation, extended sizes, terminators, "
                     "chunked relocation, idempotence, conflict rejection and cancellation\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
