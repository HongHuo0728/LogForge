#include "logforge/Ui.h"
#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>

namespace logforge {
namespace {
constexpr int LanguageControl = 501, ThemeControl = 502, CreativeControl = 503, ShadowControl = 504,
              HighlightControl = 505, SaturationControl = 506, BackendControl = 507, SaveControl = IDOK,
              CancelControl = IDCANCEL;
constexpr DWORD ComboStyle = WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL;
class SettingsWindow {
  public:
    HWND window{}, owner{};
    AppSettings draft;
    UiStyle style;
    bool accepted = false, exercise = false;
    fs::path snapshot;
    int scroll = 0, dpi = 96;
    std::map<int, HWND> controls;
    std::vector<double> shadows{0, .5, 1, 1.5, 2, 2.5, 3}, highlights = shadows;
    std::vector<double> saturation{0, 25, 50, 65, 75, 85, 100, 115, 130, 150};
    std::wstring error;
    std::wstring T(TextId id) const {
        return TranslateWide(id, draft.language);
    }
    int S(int v) const {
        return style.Scale(v);
    }
    void Add(int id, const wchar_t* type, const std::wstring& label, DWORD flags) {
        controls[id] = MakeControl(window, id, type, label, flags, style);
    }
    void NumberCombo(int id, std::vector<double>& values, double value) {
        auto it = std::find(values.begin(), values.end(), value);
        if (it == values.end()) {
            values.push_back(value);
            it = values.end() - 1;
        }
        std::vector<std::wstring> labels;
        for (const auto v : values) {
            std::wostringstream s;
            s << v;
            labels.push_back(s.str());
        }
        SetCombo(controls[id], labels, static_cast<int>(it - values.begin()));
    }
    void Create() {
        style.Apply(draft.theme, draft.language, dpi);
        style.Window(window);
        Add(LanguageControl, L"COMBOBOX", L"", ComboStyle);
        Add(ThemeControl, L"COMBOBOX", L"", ComboStyle);
        Add(BackendControl, L"COMBOBOX", L"", ComboStyle);
        Add(CreativeControl, L"BUTTON", L"", WS_TABSTOP | BS_OWNERDRAW);
        for (int id : {ShadowControl, HighlightControl, SaturationControl})
            Add(id, L"COMBOBOX", L"", ComboStyle);
        Add(SaveControl, L"BUTTON", T(TextId::Save), WS_TABSTOP | BS_OWNERDRAW);
        Add(CancelControl, L"BUTTON", T(TextId::Cancel), WS_TABSTOP | BS_OWNERDRAW);
        SetPropW(controls[SaveControl], L"primary", reinterpret_cast<HANDLE>(1));
        SetCombo(controls[LanguageControl], {L"English", L"简体中文"},
                 draft.language == Language::English ? 0 : 1);
        SetCombo(controls[ThemeControl], {T(TextId::Dark), T(TextId::Light)},
                 draft.theme == Theme::Dark ? 0 : 1);
        SetCombo(controls[BackendControl], {T(TextId::BackendAuto), L"CPU", L"NVIDIA RTX CUDA"},
                 static_cast<int>(draft.backend));
        NumberCombo(ShadowControl, shadows, draft.tone.shadowStops);
        NumberCombo(HighlightControl, highlights, draft.tone.highlightStops);
        NumberCombo(SaturationControl, saturation, draft.tone.saturation * 100);
        Layout();
        SetFocus(controls[LanguageControl]);
        if (!snapshot.empty() || exercise)
            SetTimer(window, 1, 300, nullptr);
    }
    void Layout() {
        RECT r{};
        GetClientRect(window, &r);
        const int width = MulDiv(r.right, 96, dpi), height = MulDiv(r.bottom, 96, dpi);
        const int content = draft.tone.enabled ? 580 : 442;
        scroll = std::clamp(scroll, 0, std::max(0, content - height));
        SCROLLINFO si{sizeof(si),  SIF_RANGE | SIF_PAGE | SIF_POS, 0,
                      content - 1, static_cast<UINT>(height),      scroll};
        SetScrollInfo(window, SB_VERT, &si, TRUE);
        auto place = [&](int id, int x, int y, int w, int h) {
            MoveWindow(controls[id], S(x), S(y - scroll), S(w), S(h), TRUE);
        };
        place(LanguageControl, 240, 64, width - 268, 260);
        place(ThemeControl, 240, 125, width - 268, 180);
        place(BackendControl, 240, 182, width - 268, 180);
        place(CreativeControl, 28, 252, width - 56, 38);
        SetWindowTextW(controls[CreativeControl],
                       T(draft.tone.enabled ? TextId::GradeOn : TextId::GradeOff).c_str());
        if (draft.tone.enabled)
            SetPropW(controls[CreativeControl], L"checked", reinterpret_cast<HANDLE>(1));
        else
            RemovePropW(controls[CreativeControl], L"checked");
        int y = 366;
        for (int id : {ShadowControl, HighlightControl, SaturationControl}) {
            place(id, 320, y, width - 348, 220);
            y += 42;
            ShowWindow(controls[id], draft.tone.enabled ? SW_SHOW : SW_HIDE);
            EnableWindow(controls[id], draft.tone.enabled);
        }
        place(SaveControl, width - 240, content - 58, 98, 36);
        place(CancelControl, width - 132, content - 58, 104, 36);
        InvalidateRect(window, nullptr, TRUE);
    }
    void Paint(HDC printDC = nullptr) {
        PAINTSTRUCT ps{};
        auto dc = printDC ? printDC : BeginPaint(window, &ps);
        RECT r{};
        GetClientRect(window, &r);
        FillRect(dc, &r, style.background);
        const int width = MulDiv(r.right, 96, dpi);
        auto label = [&](TextId id, int y, int h = 28, bool compact = false) {
            RECT rect{S(28), S(y - scroll), S(width - 28), S(y + h - scroll)};
            style.Text(dc, T(id), rect, compact, compact);
        };
        label(TextId::Settings, 18);
        label(TextId::LanguageLabel, 68);
        label(TextId::Appearance, 129);
        label(TextId::Backend, 186);
        label(TextId::CreativeHelp, 302, 48, true);
        if (draft.tone.enabled) {
            label(TextId::Shadows, 369);
            label(TextId::Highlights, 411);
            label(TextId::Saturation, 453);
        }
        if (!error.empty()) {
            RECT rect{S(28), S((draft.tone.enabled ? 490 : 354) - scroll), S(width - 28),
                      S((draft.tone.enabled ? 518 : 382) - scroll)};
            style.Text(dc, error, rect, false, true);
        }
        if (!printDC)
            EndPaint(window, &ps);
    }
    double Choice(int id, const std::vector<double>& values) {
        auto index = SendMessageW(controls[id], CB_GETCURSEL, 0, 0);
        if (index < 0 || static_cast<size_t>(index) >= values.size())
            throw AppError(TextId::InvalidToneChoice);
        return values[static_cast<size_t>(index)];
    }
    void Command(int id) {
        if (id == CancelControl) {
            DestroyWindow(window);
            return;
        }
        if (id == CreativeControl) {
            draft.tone.enabled = !draft.tone.enabled;
            Layout();
            return;
        }
        if (id != SaveControl)
            return;
        auto next = draft;
        const auto language = SendMessageW(controls[LanguageControl], CB_GETCURSEL, 0, 0);
        const auto theme = SendMessageW(controls[ThemeControl], CB_GETCURSEL, 0, 0);
        if (language < 0 || language > 1 || theme < 0 || theme > 1)
            throw AppError(TextId::SettingsSaveFailed);
        next.language = language == 0 ? Language::English : Language::SimplifiedChinese;
        next.theme = theme == 0 ? Theme::Dark : Theme::Light;
        const auto backend = SendMessageW(controls[BackendControl], CB_GETCURSEL, 0, 0);
        if (backend < 0 || backend > 2)
            throw AppError(TextId::SettingsSaveFailed);
        next.backend = static_cast<ProcessingBackend>(backend);
        next.tone.shadowStops = Choice(ShadowControl, shadows);
        next.tone.highlightStops = Choice(HighlightControl, highlights);
        next.tone.saturation = Choice(SaturationControl, saturation) / 100;
        SettingsStore::SavePreferences(next);
        draft = next;
        accepted = true;
        DestroyWindow(window);
    }
    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            self = static_cast<SettingsWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            self->window = h;
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self)
            return DefWindowProcW(h, m, w, l);
        try {
            switch (m) {
            case WM_CREATE:
                self->Create();
                return 0;
            case WM_PAINT:
                self->Paint();
                return 0;
            case WM_PRINTCLIENT:
                self->Paint(reinterpret_cast<HDC>(w));
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_SIZE:
                if (!self->controls.empty())
                    self->Layout();
                return 0;
            case FocusRevealMessage:
                self->scroll += ScrollDeltaToReveal(h, reinterpret_cast<HWND>(l), self->dpi);
                self->Layout();
                return 0;
            case WM_SETTINGCHANGE:
            case WM_SYSCOLORCHANGE:
                self->style.Apply(self->draft.theme, self->draft.language, self->dpi);
                for (auto [id, control] : self->controls)
                    self->style.Control(control);
                self->style.Window(h);
                return 0;
            case WM_GETMINMAXINFO: {
                auto info = reinterpret_cast<MINMAXINFO*>(l);
                MONITORINFO monitor{sizeof(monitor)};
                GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &monitor);
                info->ptMinTrackSize = {
                    std::min(self->S(540), static_cast<int>(monitor.rcWork.right - monitor.rcWork.left)),
                    std::min(self->S(300), static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top))};
                return 0;
            }
            case WM_COMMAND:
                self->Command(LOWORD(w));
                return 0;
            case WM_CLOSE:
                DestroyWindow(h);
                return 0;
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORLISTBOX:
            case WM_CTLCOLORBTN:
                return self->style.Color(m, w, l);
            case WM_DRAWITEM:
                self->style.DrawItem(*reinterpret_cast<DRAWITEMSTRUCT*>(l));
                return TRUE;
            case WM_MEASUREITEM:
                reinterpret_cast<MEASUREITEMSTRUCT*>(l)->itemHeight = self->S(25);
                return TRUE;
            case WM_VSCROLL: {
                SCROLLINFO si{sizeof(si), SIF_ALL};
                GetScrollInfo(h, SB_VERT, &si);
                if (LOWORD(w) == SB_THUMBTRACK)
                    self->scroll = si.nTrackPos;
                else if (LOWORD(w) == SB_LINEUP)
                    self->scroll -= 24;
                else if (LOWORD(w) == SB_LINEDOWN)
                    self->scroll += 24;
                else if (LOWORD(w) == SB_PAGEUP)
                    self->scroll -= static_cast<int>(si.nPage);
                else if (LOWORD(w) == SB_PAGEDOWN)
                    self->scroll += static_cast<int>(si.nPage);
                self->Layout();
                return 0;
            }
            case WM_MOUSEWHEEL:
                self->scroll -= GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 48;
                self->Layout();
                return 0;
            case WM_DPICHANGED: {
                self->dpi = HIWORD(w);
                self->style.Apply(self->draft.theme, self->draft.language, self->dpi);
                for (auto [id, c] : self->controls)
                    self->style.Control(c);
                auto r = reinterpret_cast<RECT*>(l);
                SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                self->Layout();
                return 0;
            }
            case WM_TIMER:
                KillTimer(h, 1);
                if (!self->snapshot.empty() && !SaveWindowSnapshot(h, self->snapshot))
                    throw AppError(TextId::SettingsSaveFailed);
                if (self->exercise) {
                    // Drive the real controls and Save handler; test data stays in LOGFORGE_DATA_DIR.
                    SendMessageW(self->controls[LanguageControl], CB_SETCURSEL,
                                 self->draft.language == Language::English ? 1 : 0, 0);
                    SendMessageW(self->controls[ThemeControl], CB_SETCURSEL,
                                 self->draft.theme == Theme::Dark ? 1 : 0, 0);
                    self->Command(CreativeControl);
                    self->Command(CreativeControl);
                    self->Command(SaveControl);
                } else {
                    // Change the draft, then cancel: no preference may leak through.
                    SendMessageW(self->controls[LanguageControl], CB_SETCURSEL,
                                 self->draft.language == Language::English ? 1 : 0, 0);
                    SendMessageW(self->controls[ThemeControl], CB_SETCURSEL,
                                 self->draft.theme == Theme::Dark ? 1 : 0, 0);
                    SendMessageW(self->controls[ShadowControl], CB_SETCURSEL, 0, 0);
                    self->Command(CreativeControl);
                    self->Command(CancelControl);
                }
                return 0;
            }
        } catch (const AppError& e) {
            self->error = Wide(Describe(e.message, e.details, self->draft.language));
            self->Layout();
        } catch (const std::exception& e) {
            self->error = TranslateWide({TextId::Unexpected, {e.what()}}, self->draft.language);
            self->Layout();
        }
        return DefWindowProcW(h, m, w, l);
    }
};
void RunModal(HWND owner, HWND dialog) {
    EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    MSG message{};
    while (IsWindow(dialog)) {
        const auto result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            if (result == 0)
                PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (IsWindow(dialog))
        DestroyWindow(dialog);
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
}
RECT ModalRect(HWND owner, int width, int height, int dpi) {
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor);
    const int w =
        std::min(MulDiv(width, dpi, 96), static_cast<int>(monitor.rcWork.right - monitor.rcWork.left) - 16);
    const int h =
        std::min(MulDiv(height, dpi, 96), static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top) - 16);
    const int x =
        std::clamp(static_cast<int>((ownerRect.left + ownerRect.right - w) / 2),
                   static_cast<int>(monitor.rcWork.left), static_cast<int>(monitor.rcWork.right - w));
    const int y =
        std::clamp(static_cast<int>((ownerRect.top + ownerRect.bottom - h) / 2),
                   static_cast<int>(monitor.rcWork.top), static_cast<int>(monitor.rcWork.bottom - h));
    return {x, y, x + w, y + h};
}
struct DetailsWindow {
    UiStyle style;
    AppSettings settings;
    std::wstring text;
    HWND edit{};
    int dpi = 96;
    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto self = reinterpret_cast<DetailsWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            self = static_cast<DetailsWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self)
            return DefWindowProcW(h, m, w, l);
        switch (m) {
        case WM_CREATE:
            self->style.Window(h);
            self->edit = MakeControl(h, 901, L"EDIT", self->text,
                                     WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                                     self->style);
            return 0;
        case WM_SIZE: {
            const int inset = self->style.Scale(16);
            MoveWindow(self->edit, inset, inset, std::max(1, static_cast<int>(LOWORD(l)) - 2 * inset),
                       std::max(1, static_cast<int>(HIWORD(l)) - 2 * inset), TRUE);
            return 0;
        }
        case WM_DPICHANGED: {
            self->dpi = HIWORD(w);
            self->style.Apply(self->settings.theme, self->settings.language, self->dpi);
            self->style.Control(self->edit);
            self->style.Window(h);
            auto r = reinterpret_cast<RECT*>(l);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
            return self->style.Color(m, w, l, true);
        case WM_ERASEBKGND: {
            RECT r{};
            GetClientRect(h, &r);
            FillRect(reinterpret_cast<HDC>(w), &r, self->style.background);
            return 1;
        }
        case WM_COMMAND:
            if (LOWORD(w) == IDCANCEL)
                DestroyWindow(h);
            return 0;
        case WM_CLOSE:
            DestroyWindow(h);
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }
};
struct CandidateWindow {
    HWND window{}, list{}, info{}, help{}, select{}, cancel{};
    UiStyle style;
    AppSettings settings;
    std::vector<DiscoveryCandidate> candidates;
    std::optional<fs::path> selected;
    int dpi = 96, exercise = -2;
    fs::path snapshot;
    int S(int n) const {
        return style.Scale(n);
    }
    void Extent() {
        auto dc = GetDC(list);
        const auto font = SelectObject(dc, style.normal);
        int extent = 0;
        for (const auto& c : candidates) {
            const auto text = c.ffmpeg.wstring();
            SIZE size{};
            GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
            extent = std::max(extent, static_cast<int>(size.cx) + S(12));
        }
        SelectObject(dc, font);
        ReleaseDC(list, dc);
        SendMessageW(list, LB_SETHORIZONTALEXTENT, extent, 0);
    }
    void Layout() {
        RECT r{};
        GetClientRect(window, &r);
        const int width = MulDiv(r.right, 96, dpi), height = MulDiv(r.bottom, 96, dpi);
        const int listHeight = std::max(60, (height - 138) / 2);
        MoveWindow(help, S(20), S(14), S(width - 40), S(46), TRUE);
        MoveWindow(list, S(20), S(66), S(width - 40), S(listHeight), TRUE);
        MoveWindow(info, S(20), S(76 + listHeight), S(width - 40), S(std::max(24, height - listHeight - 138)),
                   TRUE);
        MoveWindow(select, S(std::max(20, width - 316)), S(height - 48), S(180), S(32), TRUE);
        MoveWindow(cancel, S(width - 120), S(height - 48), S(100), S(32), TRUE);
        InvalidateRect(window, nullptr, TRUE);
    }
    void Selection() {
        const auto i = SendMessageW(list, LB_GETCURSEL, 0, 0);
        EnableWindow(select, i >= 0 && static_cast<size_t>(i) < candidates.size() && candidates[i].paired);
        if (i < 0 || static_cast<size_t>(i) >= candidates.size())
            return;
        const auto& c = candidates[i];
        const auto text = TranslateWide(
            {TextId::CandidateInfo,
             {PathText(c.ffmpeg), c.source, PathText(c.ffprobe), Translate(c.issue, settings.language)}},
            settings.language);
        SetWindowTextW(info, EditLines(text).c_str());
    }
    void Create() {
        style.Apply(settings.theme, settings.language, dpi);
        style.Window(window);
        help = MakeControl(window, 700, L"STATIC", TranslateWide(TextId::CandidateHelp, settings.language),
                           SS_NOPREFIX, style, true);
        list = MakeControl(
            window, 701, L"LISTBOX", L"",
            WS_TABSTOP | WS_BORDER | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, style);
        info =
            MakeControl(window, 702, L"EDIT", L"",
                        WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, style, true);
        select =
            MakeControl(window, IDOK, L"BUTTON", TranslateWide(TextId::CandidateSelect, settings.language),
                        WS_TABSTOP | BS_OWNERDRAW, style);
        cancel = MakeControl(window, IDCANCEL, L"BUTTON", TranslateWide(TextId::Cancel, settings.language),
                             WS_TABSTOP | BS_OWNERDRAW, style);
        for (const auto& c : candidates) {
            const auto text = c.ffmpeg.wstring();
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        }
        Extent();
        if (!candidates.empty())
            SendMessageW(list, LB_SETCURSEL, 0, 0);
        Selection();
        Layout();
        SetFocus(list);
        if (exercise != -2)
            SetTimer(window, 1, 100, nullptr);
    }
    void Command(int id) {
        if (id == IDCANCEL)
            DestroyWindow(window);
        else if (id == IDOK && IsWindowEnabled(select)) {
            const auto i = SendMessageW(list, LB_GETCURSEL, 0, 0);
            if (i >= 0 && static_cast<size_t>(i) < candidates.size())
                selected = candidates[i].ffmpeg;
            DestroyWindow(window);
        }
    }
    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            self = static_cast<CandidateWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            self->window = h;
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self)
            return DefWindowProcW(h, m, w, l);
        try {
            switch (m) {
            case WM_CREATE:
                self->Create();
                return 0;
            case WM_SIZE:
                self->Layout();
                return 0;
            case WM_SETTINGCHANGE:
            case WM_SYSCOLORCHANGE:
                self->style.Apply(self->settings.theme, self->settings.language, self->dpi);
                for (const auto c : {self->help, self->list, self->info, self->select, self->cancel})
                    self->style.Control(c, c == self->help || c == self->info);
                self->Extent();
                self->style.Window(h);
                return 0;
            case WM_COMMAND:
                if (LOWORD(w) == 701 && HIWORD(w) == LBN_SELCHANGE)
                    self->Selection();
                else if (HIWORD(w) == BN_CLICKED)
                    self->Command(LOWORD(w));
                return 0;
            case WM_DRAWITEM:
                self->style.DrawItem(*reinterpret_cast<DRAWITEMSTRUCT*>(l));
                return TRUE;
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORLISTBOX:
            case WM_CTLCOLORBTN:
                return self->style.Color(m, w, l);
            case WM_PAINT:
            case WM_PRINTCLIENT: {
                PAINTSTRUCT ps{};
                const auto dc = m == WM_PRINTCLIENT ? reinterpret_cast<HDC>(w) : BeginPaint(h, &ps);
                RECT r{};
                GetClientRect(h, &r);
                FillRect(dc, &r, self->style.background);
                if (m != WM_PRINTCLIENT)
                    EndPaint(h, &ps);
                return 0;
            }
            case WM_GETMINMAXINFO: {
                auto v = reinterpret_cast<MINMAXINFO*>(l);
                MONITORINFO monitor{sizeof(monitor)};
                GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &monitor);
                v->ptMinTrackSize = {
                    std::min(self->S(400), static_cast<int>(monitor.rcWork.right - monitor.rcWork.left)),
                    std::min(self->S(320), static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top))};
                return 0;
            }
            case WM_DPICHANGED: {
                self->dpi = HIWORD(w);
                self->style.Apply(self->settings.theme, self->settings.language, self->dpi);
                for (const auto c : {self->help, self->list, self->info, self->select, self->cancel})
                    self->style.Control(c, c == self->help || c == self->info);
                const auto r = reinterpret_cast<RECT*>(l);
                SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                self->Extent();
                self->Layout();
                return 0;
            }
            case WM_TIMER:
                KillTimer(h, 1);
                if (!self->snapshot.empty() && !SaveWindowSnapshot(h, self->snapshot))
                    throw AppError(TextId::SettingsSaveFailed);
                if (self->exercise >= 0) {
                    SendMessageW(self->list, LB_SETCURSEL, self->exercise, 0);
                    self->Selection();
                    if (IsWindowEnabled(self->select))
                        self->Command(IDOK);
                    else
                        self->Command(IDCANCEL);
                } else
                    self->Command(IDCANCEL);
                return 0;
            case WM_CLOSE:
                self->Command(IDCANCEL);
                return 0;
            }
        } catch (...) {
            self->selected.reset();
            DestroyWindow(h);
        }
        return DefWindowProcW(h, m, w, l);
    }
};
} // namespace
std::optional<fs::path> ShowFFmpegCandidates(HWND owner, const std::vector<DiscoveryCandidate>& candidates,
                                             const AppSettings& settings, const fs::path& snapshot,
                                             int exerciseSelection) {
    CandidateWindow state;
    state.settings = settings;
    state.candidates = candidates;
    state.snapshot = snapshot;
    state.exercise = exerciseSelection;
    state.dpi = static_cast<int>(GetDpiForWindow(owner));
    if (auto testDpi = GetPropW(owner, L"test-dpi"))
        state.dpi = static_cast<int>(reinterpret_cast<INT_PTR>(testDpi));
    WNDCLASSEXW c{sizeof(c)};
    c.hInstance = GetModuleHandleW(nullptr);
    c.lpfnWndProc = CandidateWindow::Proc;
    c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    c.lpszClassName = L"LogForge.Candidates";
    RegisterClassExW(&c);
    const auto r = ModalRect(owner, 840, 580, state.dpi);
    auto h = CreateWindowExW(WS_EX_DLGMODALFRAME, c.lpszClassName,
                             TranslateWide(TextId::CandidateTitle, settings.language).c_str(),
                             WS_CAPTION | WS_SYSMENU | WS_SIZEBOX, r.left, r.top, r.right - r.left,
                             r.bottom - r.top, owner, nullptr, c.hInstance, &state);
    if (!h)
        throw AppError(TextId::DialogOpenFailed);
    RunModal(owner, h);
    return state.selected;
}
bool ShowSettings(HWND owner, AppSettings& settings, const fs::path& snapshot, bool exercise) {
    SettingsWindow state;
    state.owner = owner;
    state.draft = settings;
    state.snapshot = snapshot;
    state.exercise = exercise;
    state.dpi = static_cast<int>(GetDpiForWindow(owner));
    if (auto testDpi = GetPropW(owner, L"test-dpi"))
        state.dpi = static_cast<int>(reinterpret_cast<INT_PTR>(testDpi));
    WNDCLASSEXW c{sizeof(c)};
    c.hInstance = GetModuleHandleW(nullptr);
    c.lpfnWndProc = SettingsWindow::Proc;
    c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    c.lpszClassName = L"LogForge.Settings";
    RegisterClassExW(&c);
    auto rect = ModalRect(owner, 600, settings.tone.enabled ? 574 : 436, state.dpi);
    auto h = CreateWindowExW(
        WS_EX_DLGMODALFRAME, c.lpszClassName, TranslateWide(TextId::Settings, settings.language).c_str(),
        WS_CAPTION | WS_SYSMENU | WS_SIZEBOX | WS_VSCROLL, rect.left, rect.top, rect.right - rect.left,
        rect.bottom - rect.top, owner, nullptr, c.hInstance, &state);
    if (!h)
        throw AppError(TextId::DialogOpenFailed);
    RunModal(owner, h);
    if (state.accepted)
        settings = state.draft;
    return state.accepted;
}
void ShowDetails(HWND owner, const std::wstring& title, const std::wstring& text,
                 const AppSettings& settings) {
    DetailsWindow state;
    state.settings = settings;
    state.text = text;
    state.dpi = static_cast<int>(GetDpiForWindow(owner));
    state.style.Apply(settings.theme, settings.language, state.dpi);
    WNDCLASSEXW c{sizeof(c)};
    c.hInstance = GetModuleHandleW(nullptr);
    c.lpfnWndProc = DetailsWindow::Proc;
    c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    c.lpszClassName = L"LogForge.Details";
    RegisterClassExW(&c);
    auto r = ModalRect(owner, 720, 480, state.dpi);
    auto h = CreateWindowExW(WS_EX_DLGMODALFRAME, c.lpszClassName, title.c_str(),
                             WS_CAPTION | WS_SYSMENU | WS_SIZEBOX, r.left, r.top, r.right - r.left,
                             r.bottom - r.top, owner, nullptr, c.hInstance, &state);
    if (!h)
        throw AppError(TextId::DialogOpenFailed);
    RunModal(owner, h);
}
} // namespace logforge
