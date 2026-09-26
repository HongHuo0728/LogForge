#include "logforge/Settings.h"
#include <cmath>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>

namespace logforge {
namespace {
using Json = nlohmann::json;
std::mutex settingsMutex;
Json Read(bool* recovered = nullptr) {
    try {
        std::ifstream file(DataDirectory() / L"settings.json");
        if (!file)
            return Json::object();
        Json data;
        file >> data;
        if (!data.is_object())
            throw std::runtime_error("Invalid settings object");
        return data;
    } catch (...) {
        if (recovered)
            *recovered = true;
        return Json::object();
    }
}
void Write(const Json& data) {
    const auto dir = DataDirectory();
    fs::create_directories(dir);
    auto temporary = dir / (L"settings-" + std::to_wstring(GetCurrentProcessId()) + L".tmp");
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            std::error_code ec;
            fs::remove(path, ec);
        }
    } cleanup{temporary};
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << data.dump(2) << '\n';
        file.flush();
        if (!file)
            throw AppError(TextId::SettingsSaveFailed);
    }
    if (!MoveFileExW(temporary.c_str(), (dir / L"settings.json").c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw AppError(Message(TextId::SettingsSaveDetail, {Utf8(WinError())}));
}
} // namespace
AppSettings SettingsStore::Load() {
    std::lock_guard lock(settingsMutex);
    StateLock processLock;
    AppSettings result;
    const auto data = Read(&result.recoveredDefaults);
    const auto read = [&](const char* key, const auto& action) {
        if (data.contains(key))
            try {
                action(data.at(key));
            } catch (...) {
                result.recoveredDefaults = true;
            }
    };
    read("language", [&](const Json& v) {
        const auto s = v.get<std::string>();
        if (s == "zh-CN")
            result.language = Language::SimplifiedChinese;
        else if (s != "en")
            throw std::runtime_error("Invalid language");
    });
    read("theme", [&](const Json& v) {
        const auto s = v.get<std::string>();
        if (s == "light")
            result.theme = Theme::Light;
        else if (s != "dark")
            throw std::runtime_error("Invalid theme");
    });
    read("processing_backend", [&](const Json& v) {
        const auto b = v.get<std::string>();
        if (b == "cpu")
            result.backend = ProcessingBackend::CPU;
        else if (b == "cuda")
            result.backend = ProcessingBackend::CUDA;
        else if (b != "auto")
            throw std::runtime_error("Invalid backend");
    });
    read("ffmpeg", [&](const Json& v) { result.manualFFmpeg = Wide(v.get<std::string>()); });
    read("detected_ffmpeg", [&](const Json& v) { result.detectedFFmpeg = Wide(v.get<std::string>()); });
    read("creative", [&](const Json& v) {
        ToneAdjustments tone;
        tone.enabled = v.value("enabled", false);
        tone.shadowStops = v.value("shadow_stops", 3.0);
        tone.highlightStops = v.value("highlight_stops", 1.0);
        tone.saturation = v.value("saturation", .85);
        ValidateToneAdjustments(tone);
        result.tone = tone;
    });
    return result;
}
void SettingsStore::SavePreferences(const AppSettings& settings) {
    ValidateToneAdjustments(settings.tone);
    std::lock_guard lock(settingsMutex);
    StateLock processLock;
    auto data = Read();
    data["language"] = settings.language == Language::SimplifiedChinese ? "zh-CN" : "en";
    data["theme"] = settings.theme == Theme::Light ? "light" : "dark";
    data["processing_backend"] = BackendName(settings.backend);
    data["creative"] = {{"enabled", settings.tone.enabled},
                        {"shadow_stops", settings.tone.shadowStops},
                        {"highlight_stops", settings.tone.highlightStops},
                        {"saturation", settings.tone.saturation}};
    Write(data);
}
void SettingsStore::SaveFFmpeg(const fs::path& path, bool automatic) {
    std::lock_guard lock(settingsMutex);
    StateLock processLock;
    auto data = Read();
    data[automatic ? "detected_ffmpeg" : "ffmpeg"] = PathText(fs::absolute(path));
    Write(data);
}
} // namespace logforge
