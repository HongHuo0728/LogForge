#include "logforge/Localization.h"
#include "logforge/Platform.h"
#include <array>
#include <set>

namespace logforge {
namespace {
struct Entry {
    const char* key;
    const char* en;
    const char* zh;
};
constexpr Entry entries[] = {
#define LF_TEXT(id, en, zh) {#id, en, zh},
#include "logforge/Messages.inc"
#undef LF_TEXT
};
static_assert(std::size(entries) == static_cast<size_t>(TextId::Count));
std::set<std::string> Placeholders(const std::string& text) {
    std::set<std::string> result;
    size_t start = 0;
    while ((start = text.find('{', start)) != std::string::npos) {
        auto end = text.find('}', start);
        if (end == std::string::npos)
            return {"invalid"};
        result.insert(text.substr(start, end - start + 1));
        start = end + 1;
    }
    return result;
}
} // namespace
std::string Translate(const Message& m, Language language) {
    const auto index = static_cast<size_t>(m.id);
    if (index >= std::size(entries))
        return "Unknown message";
    std::string result = language == Language::SimplifiedChinese ? entries[index].zh : entries[index].en;
    // Substitute in one pass: paths and diagnostic text are never interpreted as templates.
    std::string formatted;
    for (size_t pos = 0; pos < result.size();) {
        bool replaced = false;
        if (result[pos] == '{') {
            for (size_t i = 0; i < m.args.size(); ++i) {
                const auto token = "{" + std::to_string(i) + "}";
                if (result.compare(pos, token.size(), token) == 0) {
                    formatted += m.args[i];
                    pos += token.size();
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced)
            formatted += result[pos++];
    }
    return formatted;
}
std::wstring TranslateWide(const Message& m, Language language) {
    return Wide(Translate(m, language));
}
std::string Describe(const Message& m, const std::vector<Message>& details, Language language) {
    auto text = Translate(m, language);
    for (const auto& detail : details)
        text += "\n" + Translate(detail, language);
    return text;
}
const char* MessageKey(TextId id) {
    const auto i = static_cast<size_t>(id);
    return i < std::size(entries) ? entries[i].key : "Unknown";
}
bool ValidateTranslations() {
    std::set<std::string> keys;
    for (const auto& e : entries)
        if (!*e.en || !*e.zh || !keys.insert(e.key).second || Placeholders(e.en) != Placeholders(e.zh))
            return false;
    return true;
}
} // namespace logforge
