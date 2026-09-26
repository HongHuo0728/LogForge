#pragma once
#include "Color.h"
#include "CudaTransformer.h"
#include "Localization.h"
#include "Platform.h"

namespace logforge {
enum class Theme { Dark, Light };
struct AppSettings {
    Language language = Language::English;
    Theme theme = Theme::Dark;
    ToneAdjustments tone;
    ProcessingBackend backend = ProcessingBackend::Auto;
    fs::path manualFFmpeg, detectedFFmpeg;
    bool recoveredDefaults = false;
};
class SettingsStore {
  public:
    static AppSettings Load();
    static void SavePreferences(const AppSettings& settings);
    static void SaveFFmpeg(const fs::path& path, bool automatic);
};
} // namespace logforge
