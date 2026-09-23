#pragma once
#include "Discovery.h"
#include "Settings.h"
#include <commctrl.h>

namespace logforge {
struct Palette {
    COLORREF background, surface, text, muted, border, accent;
};
class UiStyle {
  public:
    UiStyle() = default;
    ~UiStyle();
    UiStyle(const UiStyle&) = delete;
    UiStyle& operator=(const UiStyle&) = delete;
    void Apply(Theme theme, Language language, int dpi);
    int Scale(int value) const {
        return MulDiv(value, dpi_, 96);
    }
    void Window(HWND window) const;
    void Control(HWND control, bool compact = false) const;
    void Text(HDC dc, const std::wstring& text, RECT rect, bool muted = false, bool compact = false,
              UINT flags = DT_LEFT | DT_WORDBREAK | DT_NOPREFIX) const;
    void Card(HDC dc, RECT rect) const;
    LRESULT Color(UINT message, WPARAM w, LPARAM l, bool surface = false) const;
    void DrawItem(const DRAWITEMSTRUCT& item) const;
    Palette colors{};
    HBRUSH background{}, surface{};
    HFONT normal{}, compact{}, title{};
    Theme theme = Theme::Dark;

  private:
    int dpi_ = 96;
};
std::wstring WindowText(HWND window);
std::wstring EditLines(const std::wstring& text);
inline constexpr UINT FocusRevealMessage = WM_APP + 41;
int ScrollDeltaToReveal(HWND parent, HWND focused, int dpi);
HWND MakeControl(HWND parent, int id, const wchar_t* klass, const std::wstring& text, DWORD style,
                 const UiStyle& theme, bool compact = false);
void SetCombo(HWND combo, const std::vector<std::wstring>& choices, int selected);
bool ShowSettings(HWND owner, AppSettings& settings, const fs::path& snapshot = {}, bool exercise = false);
void ShowDetails(HWND owner, const std::wstring& title, const std::wstring& text,
                 const AppSettings& settings);
bool SaveWindowSnapshot(HWND window, const fs::path& destination);
std::optional<fs::path> ShowFFmpegCandidates(HWND owner, const std::vector<DiscoveryCandidate>& candidates,
                                             const AppSettings& settings, const fs::path& snapshot = {},
                                             int exerciseSelection = -2);
} // namespace logforge
