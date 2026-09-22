#pragma once
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

namespace logforge {
enum class Language { English, SimplifiedChinese };
enum class TextId {
#define LF_TEXT(id, en, zh) id,
#include "Messages.inc"
#undef LF_TEXT
    Count
};
struct Message {
    TextId id;
    std::vector<std::string> args;
    Message(TextId key, std::initializer_list<std::string> values = {}) : id(key), args(values) {}
};
std::string Translate(const Message& message, Language language = Language::English);
std::wstring TranslateWide(const Message& message, Language language = Language::English);
const char* MessageKey(TextId id);
bool ValidateTranslations();
std::string Describe(const Message& message, const std::vector<Message>& details, Language language);
class AppError : public std::runtime_error {
  public:
    explicit AppError(Message m, std::vector<Message> d = {})
        : std::runtime_error(Describe(m, d, Language::English)), message(std::move(m)),
          details(std::move(d)) {}
    explicit AppError(TextId id) : AppError(Message(id)) {}
    Message message;
    std::vector<Message> details;
};
} // namespace logforge
