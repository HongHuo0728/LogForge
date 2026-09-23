#include "logforge/Transcode.h"
#include "logforge/Ui.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <memory>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <sstream>
#include <thread>
#include <wrl/client.h>

namespace logforge {
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT EventMessage = WM_APP + 1;
constexpr double ExposureValues[]{-4, -3, -2, -1, -.5, 0, .5, 1, 2, 3, 4};
enum Control {
    Open = 101,
    ChooseOutput,
    Convert,
    Cancel,
    Install,
    Manual,
    Logs,
    OutputEdit,
    SourceText,
    StageText,
    ToolText,
    ProgressBar,
    DropZone,
    Exposure,
    SettingsButton,
    DetailsButton,
    RescanButton
};
enum class Kind { Progress, Discovery, Detect, Probe, Convert, Install, Approval, Failure };
struct Event {
    Kind kind;
    JobProgress progress;
    DiscoveryProgress discovery;
    std::optional<FFmpegInstallation> tools;
    std::optional<MediaInfo> media;
    Message error{TextId::Unexpected, {"Unknown error"}};
    std::vector<Message> details;
    std::optional<ValidationReport> validation;
    std::vector<fs::path> candidates;
    ToolIdentity identity;
};
std::optional<fs::path> SelectFile(HWND owner, bool save, bool executable, Language language,
                                   const fs::path& defaultPath = {}) {
    ComPtr<IFileDialog> dialog;
    HRESULT hr =
        save ? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))
             : CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr))
        throw AppError(TextId::DialogOpenFailed);
    DWORD flags = 0;
    dialog->GetOptions(&flags);
    dialog->SetOptions(flags | FOS_FORCEFILESYSTEM | (save ? FOS_PATHMUSTEXIST : FOS_FILEMUSTEXIST));
    const auto movieLabel = TranslateWide(TextId::VideoFiles, language),
               allLabel = TranslateWide(TextId::AllFiles, language);
    const COMDLG_FILTERSPEC movie[] = {{movieLabel.c_str(), L"*.mov;*.mp4;*.mxf"},
                                       {allLabel.c_str(), L"*.*"}};
    const auto executableLabel = TranslateWide(TextId::FFmpegExecutable, language);
    const COMDLG_FILTERSPEC exe[] = {{executableLabel.c_str(), L"ffmpeg.exe"}};
    const COMDLG_FILTERSPEC mov[] = {{L"QuickTime MOV", L"*.mov"}};
    dialog->SetFileTypes(executable ? 1 : save ? 1 : 2, executable ? exe : save ? mov : movie);
    if (save)
        dialog->SetDefaultExtension(L"mov");
    if (!defaultPath.empty()) {
        dialog->SetFileName(defaultPath.filename().c_str());
        ComPtr<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(defaultPath.parent_path().c_str(), nullptr,
                                                  IID_PPV_ARGS(&folder))))
            dialog->SetFolder(folder.Get());
    }
    dialog->SetTitle(TranslateWide(executable ? TextId::SelectFFmpeg
                                   : save     ? TextId::SaveVideo
                                              : TextId::OpenVideo,
                                   language)
                         .c_str());
    hr = dialog->Show(owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return {};
    if (FAILED(hr))
        throw AppError(TextId::DialogFailed);
    ComPtr<IShellItem> item;
    dialog->GetResult(&item);
    PWSTR name = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)))
        throw AppError(TextId::PathReadFailed);
    fs::path result(name);
    CoTaskMemFree(name);
    return result;
}
} // namespace
class MainWindow {
  public:
    HWND window{}, panel{};
    Logger logger;
    AppSettings settings = SettingsStore::Load();
    UiStyle style;
    std::map<int, HWND> controls;
    std::jthread worker;
    std::atomic_bool cancelled = false;
    std::optional<FFmpegInstallation> tools;
    std::optional<MediaInfo> source;
    std::vector<fs::path> candidates;
    fs::path selected, smokeInput, smokeOutput, uiDirectory;
    std::string explicitInputChroma;
    bool busy = false, closing = false, detectionComplete = false, smoke = false, smokeStarted = false;
    bool uiTest = false, missingScenario = false, settingsExercised = false, lastSignalWarning = false;
    bool conversionTone = false, layingOut = false;
    Kind active = Kind::Detect;
    int dpi = 96, testDpi = 0, scroll = 0, footerTop = 0, exposureIndex = 5, exitCode = 0;
    Message stage{TextId::Starting};
    std::vector<Message> details;
    std::wstring rawDetails;
    JobProgress currentProgress;
    DiscoveryProgress discovery;
    ~MainWindow() {
        cancelled = true;
        if (worker.joinable())
            worker.join();
    }
    int S(int value) const {
        return style.Scale(value);
    }
    std::wstring T(const Message& m) const {
        return TranslateWide(m, settings.language);
    }
    HWND H(int id) const {
        auto it = controls.find(id);
        return it == controls.end() ? nullptr : it->second;
    }
    void Text(int id, const std::wstring& value) {
        SetWindowTextW(H(id), value.c_str());
    }
    void Post(Event event) {
        auto value = std::make_unique<Event>(std::move(event));
        if (PostMessageW(window, EventMessage, 0, reinterpret_cast<LPARAM>(value.get())))
            value.release();
    }
    template <class F> void Start(Kind kind, F work) {
        if (busy)
            return;
        if (worker.joinable())
            worker.join();
        busy = true;
        active = kind;
        cancelled = false;
        details.clear();
        rawDetails.clear();
        currentProgress = {};
        Buttons();
        Layout();
        worker = std::jthread([this, work = std::move(work)] {
            try {
                work();
            } catch (const AppError& error) {
                logger.Write(std::string("ERROR[") + MessageKey(error.message.id) + "] " + error.what());
                Event event{Kind::Failure};
                event.error = error.message;
                event.details = error.details;
                Post(std::move(event));
            } catch (const std::exception& error) {
                logger.Write(std::string("ERROR: ") + error.what());
                Event event{Kind::Failure};
                event.error = {TextId::Unexpected, {error.what()}};
                Post(std::move(event));
            } catch (...) {
                Event event{Kind::Failure};
                event.error = {TextId::Unexpected, {"Unknown exception"}};
                Post(std::move(event));
            }
        });
    }
    void Stage(Message value) {
        stage = std::move(value);
        RefreshStatus();
    }
    void RefreshStatus() {
        std::wostringstream s;
        s << T(stage);
        if (busy && currentProgress.fraction >= 0) {
            s << L"\r\n" << std::fixed << std::setprecision(1) << currentProgress.fraction * 100 << L"%";
            if (currentProgress.frame)
                s << L" · " << T({TextId::Frames, {std::to_string(currentProgress.frame)}});
            if (currentProgress.speed > 0 && currentProgress.seconds > 2 && source &&
                active == Kind::Convert) {
                const double remaining =
                    (source->videoDuration - currentProgress.seconds) / currentProgress.speed;
                if (remaining > 0 && remaining < 86400)
                    s << L" · " << T({TextId::ETA, {std::to_string(static_cast<int>(remaining))}});
            }
        }
        Text(StageText, s.str());
        Text(ToolText, T(tools                            ? TextId::ToolsReady
                         : busy && active == Kind::Detect ? TextId::Detecting
                                                          : TextId::NeedTools) +
                           L"\r\n" + T(settings.tone.enabled ? TextId::GradeOn : TextId::GradeOff));
        EnableWindow(H(DetailsButton), !details.empty() || !rawDetails.empty());
    }
    void Buttons() {
        for (int id : {Open, ChooseOutput, OutputEdit, Exposure, SettingsButton})
            EnableWindow(H(id), !busy);
        EnableWindow(H(Convert), !busy && tools && source && source->UnsupportedReasons().empty());
        EnableWindow(H(Cancel), busy);
        // Installation is a fallback only after full accessible-drive discovery finishes.
        const bool fallback = !tools && detectionComplete && !busy;
        for (int id : {Install, Manual}) {
            ShowWindow(H(id), fallback ? SW_SHOW : SW_HIDE);
            EnableWindow(H(id), fallback);
        }
        ShowWindow(H(RescanButton), !tools && !busy ? SW_SHOW : SW_HIDE);
        EnableWindow(H(RescanButton), !busy);
        RefreshStatus();
    }
    void Detect() {
        detectionComplete = false;
        Stage(TextId::Detecting);
        Start(Kind::Detect, [this] {
            FFmpegManager manager(logger);
            auto found = manager.Detect(cancelled, [this](const DiscoveryProgress& p) {
                Event e{Kind::Discovery};
                e.discovery = p;
                Post(std::move(e));
            });
            Event e{Kind::Detect};
            e.tools = std::move(found);
            e.candidates = manager.UnapprovedCandidates();
            Post(std::move(e));
        });
    }
    void ProbeInput(const fs::path& path) {
        if (busy && active != Kind::Detect)
            return;
        selected = path;
        source.reset();
        Text(DropZone, path.filename().wstring());
        Text(SourceText, T(TextId::ReadingMedia));
        Text(OutputEdit, (path.parent_path() / (path.stem().wstring() + L"_AppleLog.mov")).wstring());
        if (!tools || busy) {
            Stage(TextId::NeedTools);
            Buttons();
            return;
        }
        Stage(TextId::ReadingMedia);
        Start(Kind::Probe, [this, path] {
            auto media = Probe(tools->ffprobe, path, &cancelled);
            media.inputChromaOverride = explicitInputChroma;
            logger.Write("Selected input: " + media.raw.dump());
            Event e{Kind::Probe};
            e.media = std::move(media);
            Post(std::move(e));
        });
    }
    void BeginConvert() {
        if (!source || !tools)
            return;
        const auto path = smoke ? smokeOutput : fs::path(WindowText(H(OutputEdit)));
        const auto index = SendMessageW(H(Exposure), CB_GETCURSEL, 0, 0);
        if (index < 0 || index >= static_cast<LRESULT>(std::size(ExposureValues)))
            throw AppError(TextId::InvalidExposureChoice);
        exposureIndex = static_cast<int>(index);
        TranscodeOptions options{ExposureValues[index], settings.tone};
        conversionTone = options.tone.enabled;
        Stage(TextId::VerifyTiming);
        Start(Kind::Convert, [this, path, options] {
            auto report = TranscodeJob::Run(
                *tools, *source, path, logger, cancelled,
                [this](const JobProgress& p) {
                    Event e{Kind::Progress};
                    e.progress = p;
                    Post(std::move(e));
                },
                options);
            Event e{Kind::Convert};
            e.validation = std::move(report);
            Post(std::move(e));
        });
    }
    void Add(HWND parent, int id, const wchar_t* type, const std::wstring& label, DWORD flags = 0,
             bool compact = false) {
        controls[id] = MakeControl(parent, id, type, label, flags, style, compact);
    }
    void ApplyAppearance() {
        style.Apply(settings.theme, settings.language, dpi);
        style.Window(window);
        for (auto [id, h] : controls)
            style.Control(h, id == ToolText || id == StageText);
        for (auto [id, key] : {std::pair{Open, TextId::Open},
                               {ChooseOutput, TextId::ChooseOutput},
                               {Convert, TextId::Convert},
                               {Cancel, TextId::Cancel},
                               {Install, TextId::Install},
                               {Manual, TextId::Manual},
                               {Logs, TextId::Logs},
                               {SettingsButton, TextId::Settings},
                               {DetailsButton, TextId::Details},
                               {RescanButton, TextId::Rescan}})
            Text(id, T(key));
        Text(DropZone, selected.empty() ? T(TextId::Drop) : selected.filename().wstring());
        Text(SourceText, source ? source->Summary(settings.language) : T(TextId::AwaitInput));
        std::vector<std::wstring> exposure;
        for (double ev : ExposureValues) {
            std::wostringstream s;
            s << (ev > 0 ? L"+" : L"") << ev << L" EV";
            exposure.push_back(ev == 0 ? T(TextId::DefaultEV) : s.str());
        }
        SetCombo(H(Exposure), exposure, exposureIndex);
        SetWindowTextW(window, (L"LogForge " + Wide(DisplayVersion)).c_str());
        RefreshStatus();
        Layout();
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
    void Create() {
        dpi = testDpi ? testDpi : static_cast<int>(GetDpiForWindow(window));
        style.Apply(settings.theme, settings.language, dpi);
        if (testDpi)
            SetPropW(window, L"test-dpi", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(testDpi)));
        WNDCLASSEXW c{sizeof(c)};
        c.hInstance = GetModuleHandleW(nullptr);
        c.lpfnWndProc = PanelProc;
        c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        c.lpszClassName = L"LogForge.Content";
        RegisterClassExW(&c);
        panel = CreateWindowExW(WS_EX_CONTROLPARENT, c.lpszClassName, L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0, 10, 10, window,
                                nullptr, c.hInstance, this);
        if (!panel)
            throw AppError(TextId::DialogOpenFailed);
        Add(panel, DropZone, L"STATIC", L"", SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX | SS_PATHELLIPSIS);
        Add(panel, SourceText, L"STATIC", L"", SS_NOPREFIX);
        Add(panel, OutputEdit, L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL);
        Add(panel, Exposure, L"COMBOBOX", L"",
            WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL);
        Add(panel, ProgressBar, PROGRESS_CLASSW, L"", PBS_SMOOTH);
        SendMessageW(H(ProgressBar), PBM_SETRANGE32, 0, 1000);
        for (int id : {Open, ChooseOutput, Convert, Cancel})
            Add(panel, id, L"BUTTON", L"", WS_TABSTOP | BS_OWNERDRAW);
        SetPropW(H(Convert), L"primary", reinterpret_cast<HANDLE>(1));
        for (int id : {SettingsButton, Install, Manual, Logs, DetailsButton, RescanButton})
            Add(window, id, L"BUTTON", L"", WS_TABSTOP | BS_OWNERDRAW);
        Add(window, ToolText, L"STATIC", L"", SS_NOPREFIX, true);
        Add(window, StageText, L"STATIC", L"", SS_NOPREFIX, true);
        ApplyAppearance();
        Buttons();
        if (uiTest && missingScenario) {
            detectionComplete = true;
            Stage(TextId::ToolsMissing);
            Buttons();
            SetTimer(window, 9002, 300, nullptr);
        } else
            Detect();
    }
    std::wstring RightFooter() const {
        return T(TextId::Devices) + L"\r\n\r\n" + T(TextId::FormatRequired) + L"\r\n" +
               T(TextId::ResolutionChoice) + L"\r\n\r\n" + T(TextId::EditorHint);
    }
    void Layout() {
        if (layingOut || !panel)
            return;
        layingOut = true;
        RECT r{};
        GetClientRect(window, &r);
        const int width = MulDiv(r.right, 96, dpi), height = MulDiv(r.bottom, 96, dpi),
                  column = (width - 88) / 2;
        HDC dc = GetDC(window);
        auto old = SelectObject(dc, style.compact);
        RECT measured{0, 0, S(column), 0};
        const auto copy = RightFooter();
        DrawTextW(dc, copy.c_str(), -1, &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
        ReleaseDC(window, dc);
        const int footerHeight = std::max(238, MulDiv(measured.bottom, 96, dpi) + 36);
        footerTop = std::max(100, height - footerHeight);
        MoveWindow(panel, S(24), S(78), S(width - 48), S(std::max(22, footerTop - 90)), TRUE);
        MoveWindow(H(SettingsButton), S(width - 144), S(23), S(116), S(36), TRUE);
        auto place = [&](int id, int x, int y, int w, int h) {
            MoveWindow(H(id), S(x), S(y), S(w), S(h), TRUE);
        };
        place(ToolText, 32, footerTop + 14, column, 46);
        place(StageText, 32, footerTop + 64, column, 46);
        place(Logs, 32, footerTop + 112, 104, 30);
        place(DetailsButton, 144, footerTop + 112, 90, 30);
        place(RescanButton, 244, footerTop + 112, std::max(110, column - 212), 30);
        place(Install, 32, footerTop + 150, (column - 8) / 2, 32);
        place(Manual, 40 + (column - 8) / 2, footerTop + 150, (column - 8) / 2, 32);
        LayoutPanel();
        layingOut = false;
        InvalidateRect(window, nullptr, TRUE);
    }
    void LayoutPanel() {
        if (!panel || controls.empty())
            return;
        RECT r{};
        GetClientRect(panel, &r);
        const int width = MulDiv(r.right, 96, dpi), height = MulDiv(r.bottom, 96, dpi), content = 418;
        scroll = std::clamp(scroll, 0, std::max(0, content - height));
        SCROLLINFO si{sizeof(si),  SIF_RANGE | SIF_PAGE | SIF_POS, 0,
                      content - 1, static_cast<UINT>(height),      scroll};
        SetScrollInfo(panel, SB_VERT, &si, TRUE);
        auto place = [&](int id, int x, int y, int w, int h) {
            MoveWindow(H(id), S(x), S(y - scroll), S(w), S(h), TRUE);
        };
        place(DropZone, 20, 20, width - 200, 34);
        place(Open, width - 158, 18, 138, 38);
        place(SourceText, 20, 92, width - 40, 86);
        place(OutputEdit, 20, 258, width - 198, 34);
        place(ChooseOutput, width - 166, 258, 146, 34);
        place(Exposure, 108, 304, 152, 220);
        place(ProgressBar, 20, 380, width - 342, 12);
        place(Convert, width - 306, 365, 184, 42);
        place(Cancel, width - 112, 365, 92, 42);
        InvalidateRect(panel, nullptr, TRUE);
    }
    void Paint() {
        PAINTSTRUCT ps{};
        auto dc = BeginPaint(window, &ps);
        RECT r{};
        GetClientRect(window, &r);
        FillRect(dc, &r, style.background);
        const int width = MulDiv(r.right, 96, dpi), height = MulDiv(r.bottom, 96, dpi),
                  column = (width - 88) / 2;
        auto icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101),
                                                  IMAGE_ICON, S(42), S(42), LR_SHARED));
        DrawIconEx(dc, S(28), S(20), icon, S(42), S(42), 0, nullptr, DI_NORMAL);
        auto old = SelectObject(dc, style.title);
        SetTextColor(dc, style.colors.text);
        SetBkMode(dc, TRANSPARENT);
        RECT brand{S(82), S(19), S(240), S(58)};
        DrawTextW(dc, L"LogForge", -1, &brand, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(dc, old);
        style.Text(dc, Wide(DisplayVersion), {S(252), S(31), S(width - 164), S(55)}, true);
        HPEN pen = CreatePen(PS_SOLID, 1, style.colors.border);
        old = SelectObject(dc, pen);
        MoveToEx(dc, S(24), S(footerTop), nullptr);
        LineTo(dc, S(width - 24), S(footerTop));
        SelectObject(dc, old);
        DeleteObject(pen);
        style.Text(dc, RightFooter(), {S(56 + column), S(footerTop + 16), S(width - 32), S(height - 12)},
                   true, true);
        style.Text(dc, T(TextId::License), {S(32), S(height - 48), S(32 + column), S(height - 30)}, true,
                   true);
        style.Text(dc, T(TextId::Privacy), {S(32), S(height - 29), S(32 + column), S(height - 2)}, true,
                   true);
        EndPaint(window, &ps);
    }
    void PaintPanel() {
        PAINTSTRUCT ps{};
        auto dc = BeginPaint(panel, &ps);
        RECT r{};
        GetClientRect(panel, &r);
        FillRect(dc, &r, style.background);
        const int width = MulDiv(r.right, 96, dpi);
        auto rect = [&](int x, int y, int w, int h) {
            return RECT{S(x), S(y - scroll), S(x + w), S(y + h - scroll)};
        };
        style.Card(dc, rect(0, 4, width, 184));
        style.Card(dc, rect(0, 202, width, 146));
        style.Text(dc, T(TextId::Source), rect(20, 67, width - 40, 22), true, true);
        style.Text(dc, T(TextId::Output), rect(20, 214, 150, 20), true, true);
        style.Text(dc, L"Apple Log · BT.2020 · ProRes 422 HQ · 10-bit 4:2:2 · MOV",
                   rect(20, 234, width - 40, 24));
        style.Text(dc, T(TextId::Exposure), rect(20, 309, 84, 24), true, true);
        EndPaint(panel, &ps);
    }
    void OnEvent(std::unique_ptr<Event> e) {
        if (e->kind == Kind::Progress) {
            currentProgress = e->progress;
            stage = e->progress.stage;
            SendMessageW(H(ProgressBar), PBM_SETPOS,
                         static_cast<WPARAM>(std::clamp(e->progress.fraction, 0.0, 1.0) * 1000), 0);
            RefreshStatus();
            return;
        }
        if (e->kind == Kind::Discovery) {
            discovery = e->discovery;
            if (discovery.phase == DiscoveryPhase::ScanningDrive)
                Stage({TextId::Scanning,
                       {PathText(discovery.drive), std::to_string(discovery.directories),
                        std::to_string(discovery.candidates), std::to_string(discovery.skipped)}});
            else if (discovery.phase == DiscoveryPhase::CheckingCandidate)
                Stage({TextId::CheckingCandidate, {PathText(discovery.drive)}});
            return;
        }
        busy = false;
        if (worker.joinable())
            worker.join();
        if (closing) {
            DestroyWindow(window);
            return;
        }
        if (e->kind == Kind::Failure) {
            details = e->details;
            details.insert(details.begin(), e->error);
            Stage(cancelled ? TextId::Cancelled : TextId::Failed);
            Buttons();
            if (smoke || uiTest)
                Finish(false, Translate(e->error));
            return;
        }
        if (e->kind == Kind::Approval) {
            Buttons();
            const auto body = T(Message(TextId::FFmpegApproveBody, {e->identity.ToJson().dump(2)}));
            if (MessageBoxW(window, body.c_str(), T(TextId::FFmpegApprove).c_str(),
                            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
                Start(Kind::Detect, [this, identity = e->identity] {
                    ToolTrust::ApproveManual(identity);
                    FFmpegManager manager(logger);
                    auto checked = manager.Check(identity.ffmpeg, cancelled);
                    manager.SaveManual(identity.ffmpeg);
                    Event result{Kind::Detect};
                    result.tools = std::move(checked);
                    Post(std::move(result));
                });
            } else {
                Stage(TextId::FFmpegApprovalCancelled);
                Buttons();
            }
            return;
        }
        if (e->kind == Kind::Detect || e->kind == Kind::Install) {
            tools = std::move(e->tools);
            candidates = std::move(e->candidates);
            for (const auto& candidate : candidates)
                details.emplace_back(TextId::FFmpegUntrusted,
                                     std::initializer_list<std::string>{PathText(candidate)});
            detectionComplete = true;
            if (discovery.skipped)
                details.emplace_back(TextId::ScanSkipped);
            if (settings.recoveredDefaults)
                details.emplace_back(TextId::SettingsRecovered);
            Stage(tools                ? TextId::Ready
                  : candidates.empty() ? TextId::ToolsMissing
                                       : TextId::FFmpegDiscovered);
            Buttons();
            Layout();
            if (uiTest) {
                SetTimer(window, 9002, 300, nullptr);
                return;
            }
            if (smoke) {
                if (!tools) {
                    Finish(false, "FFmpeg unavailable");
                    return;
                }
                const auto name = smokeInput.wstring();
                auto memory = GlobalAlloc(GHND, sizeof(DROPFILES) + (name.size() + 2) * sizeof(wchar_t));
                if (!memory)
                    throw std::bad_alloc();
                auto drop = static_cast<DROPFILES*>(GlobalLock(memory));
                drop->pFiles = sizeof(DROPFILES);
                drop->fWide = TRUE;
                memcpy(reinterpret_cast<unsigned char*>(drop) + sizeof(DROPFILES), name.c_str(),
                       (name.size() + 1) * sizeof(wchar_t));
                GlobalUnlock(memory);
                SendMessageW(window, WM_DROPFILES, reinterpret_cast<WPARAM>(memory), 0);
                return;
            }
            if (!selected.empty() && tools)
                ProbeInput(selected);
            return;
        }
        if (e->kind == Kind::Probe) {
            source = std::move(e->media);
            if (!smoke && source->EffectiveChromaLocation().empty()) {
                const auto answer = MessageBoxW(window, T(TextId::InputChromaChoose).c_str(),
                                                T(TextId::InputChromaTitle).c_str(),
                                                MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON3);
                if (answer == IDYES)
                    source->inputChromaOverride = "left";
                else if (answer == IDNO)
                    source->inputChromaOverride = "center";
            }
            Text(SourceText, source->Summary(settings.language));
            details = source->UnsupportedReasons();
            Stage(details.empty() ? Message(TextId::InputReady) : details.front());
            Buttons();
            if (smoke && !smokeStarted) {
                smokeStarted = true;
                Text(OutputEdit, smokeOutput.wstring());
                if (details.empty())
                    BeginConvert();
                else
                    Finish(false, Translate(details.front()));
            }
            return;
        }
        if (e->kind == Kind::Convert) {
            lastSignalWarning = e->validation && e->validation->signalWarning;
            details = e->validation ? e->validation->warnings : std::vector<Message>{};
            Stage(lastSignalWarning ? TextId::CompleteWarning
                  : conversionTone  ? TextId::CompleteCreative
                                    : TextId::CompleteStandard);
            SendMessageW(H(ProgressBar), PBM_SETPOS, 1000, 0);
            Buttons();
            if (smoke)
                SetTimer(window, 9001, 300, nullptr);
        }
    }
    void Failure(const AppError& e) {
        details = e.details;
        details.insert(details.begin(), e.message);
        Stage(TextId::Failed);
        Buttons();
    }
    void Command(int id) {
        if (id == Cancel) {
            cancelled = true;
            Stage(TextId::Cancelling);
            return;
        }
        if (id == Logs) {
            ShellExecuteW(window, L"open", logger.Path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }
        if (id == DetailsButton) {
            std::wstring text;
            for (const auto& message : details)
                text += T(message) + L"\r\n\r\n";
            text += rawDetails;
            ShowDetails(window, T(TextId::Details), text, settings);
            return;
        }
        if (busy)
            return;
        if (id == SettingsButton) {
            exposureIndex = static_cast<int>(SendMessageW(H(Exposure), CB_GETCURSEL, 0, 0));
            if (ShowSettings(window, settings))
                ApplyAppearance();
        } else if (id == Open) {
            if (auto path = SelectFile(window, false, false, settings.language))
                ProbeInput(*path);
        } else if (id == ChooseOutput) {
            if (auto path =
                    SelectFile(window, true, false, settings.language, fs::path(WindowText(H(OutputEdit)))))
                Text(OutputEdit, path->wstring());
        } else if (id == Convert)
            BeginConvert();
        else if (id == RescanButton)
            Detect();
        else if (id == Manual) {
            if (auto path = SelectFile(window, false, true, settings.language,
                                       candidates.empty() ? fs::path{} : candidates.front())) {
                Stage(TextId::Detecting);
                Start(Kind::Detect, [this, path = *path] {
                    Event e{Kind::Approval};
                    e.identity = ToolTrust::Inspect(path);
                    Post(std::move(e));
                });
            }
        } else if (id == Install) {
            Stage(TextId::Downloading);
            Start(Kind::Install, [this] {
                GyanReleaseProvider provider;
                auto installed = FFmpegDownloader::Install(
                    provider, logger, cancelled,
                    [this, last = uint64_t{0}](uint64_t n, uint64_t total, const Message& phase) mutable {
                        if (total && n != total && n > last && n - last < 262144)
                            return;
                        last = n;
                        Event e{Kind::Progress};
                        e.progress.stage = phase;
                        e.progress.fraction = total ? static_cast<double>(n) / total : -1;
                        Post(std::move(e));
                    });
                Event e{Kind::Install};
                e.tools = std::move(installed);
                Post(std::move(e));
            });
        }
    }
    void Finish(bool passed, const std::string& note) {
        exitCode = passed ? 0 : 1;
        const auto image = uiTest ? uiDirectory / L"main.png" : fs::path(smokeOutput.wstring() + L".png");
        const bool captured = SaveWindowSnapshot(window, image);
        Json report{{"passed", passed && captured},
                    {"detail", note},
                    {"version", Version},
                    {"build", BuildNumber},
                    {"language", settings.language == Language::English ? "en" : "zh-CN"},
                    {"theme", settings.theme == Theme::Dark ? "dark" : "light"},
                    {"dpi", dpi},
                    {"snapshot_saved", captured},
                    {"window_created", IsWindow(window) != FALSE},
                    {"output", PathText(smokeOutput)},
                    {"convert_enabled", IsWindowEnabled(H(Convert)) != FALSE},
                    {"cancel_enabled", IsWindowEnabled(H(Cancel)) != FALSE},
                    {"install_visible", IsWindowVisible(H(Install)) != FALSE},
                    {"manual_visible", IsWindowVisible(H(Manual)) != FALSE},
                    {"settings_exercised", settingsExercised},
                    {"signal_warning", lastSignalWarning},
                    {"creative_enabled", settings.tone.enabled},
                    {"ui_only", uiTest},
                    {"missing_scenario_injected", missingScenario}};
        const auto path =
            uiTest ? uiDirectory / L"ui-test.json" : fs::path(smokeOutput.wstring() + L".gui-test.json");
        std::ofstream file(path);
        file << report.dump(2);
        file.close();
        if (!captured || !file)
            exitCode = 1;
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
    void UiTest() {
        const auto original = settings;
        if (!ShowSettings(window, settings, uiDirectory / L"settings.png", true)) {
            Finish(false, "Settings Save did not run");
            return;
        }
        ApplyAppearance();
        const auto loaded = SettingsStore::Load();
        settingsExercised = loaded.language != original.language && loaded.theme != original.theme &&
                            loaded.tone.enabled == original.tone.enabled &&
                            loaded.manualFFmpeg == original.manualFFmpeg;
        SaveWindowSnapshot(window, uiDirectory / L"switched.png");
        const bool cancelApplied =
            ShowSettings(window, settings, uiDirectory / L"settings-cancel.png", false);
        const auto afterCancel = SettingsStore::Load();
        settingsExercised = settingsExercised && !cancelApplied && afterCancel.language == loaded.language &&
                            afterCancel.theme == loaded.theme &&
                            afterCancel.tone.enabled == loaded.tone.enabled &&
                            afterCancel.tone.shadowStops == loaded.tone.shadowStops &&
                            afterCancel.tone.highlightStops == loaded.tone.highlightStops &&
                            afterCancel.tone.saturation == loaded.tone.saturation &&
                            afterCancel.manualFFmpeg == loaded.manualFFmpeg &&
                            afterCancel.detectedFFmpeg == loaded.detectedFFmpeg;
        settings = original;
        SettingsStore::SavePreferences(settings);
        ApplyAppearance();
        Buttons();
        const bool visibility = missingScenario
                                    ? IsWindowVisible(H(Install)) && IsWindowVisible(H(Manual))
                                    : tools && !IsWindowVisible(H(Install)) && !IsWindowVisible(H(Manual));
        Finish(settingsExercised && visibility,
               "Native settings Save/Cancel and FFmpeg fallback visibility exercised");
    }
    static LRESULT CALLBACK PanelProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self)
            return DefWindowProcW(h, m, w, l);
        switch (m) {
        case WM_PAINT:
            if (self->panel)
                self->PaintPanel();
            else
                return DefWindowProcW(h, m, w, l);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_COMMAND:
        case WM_DRAWITEM:
        case WM_MEASUREITEM:
            return SendMessageW(self->window, m, w, l);
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN:
            return self->style.Color(m, w, l, true);
        case WM_MOUSEWHEEL:
            self->scroll -= GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 48;
            self->LayoutPanel();
            return 0;
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
            self->LayoutPanel();
            return 0;
        }
        }
        return DefWindowProcW(h, m, w, l);
    }
    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
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
            case WM_ERASEBKGND:
                return 1;
            case WM_SIZE:
                self->Layout();
                return 0;
            case WM_DRAWITEM:
                self->style.DrawItem(*reinterpret_cast<DRAWITEMSTRUCT*>(l));
                return TRUE;
            case WM_MEASUREITEM:
                reinterpret_cast<MEASUREITEMSTRUCT*>(l)->itemHeight = self->S(25);
                return TRUE;
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORLISTBOX:
            case WM_CTLCOLORBTN:
                return self->style.Color(m, w, l);
            case WM_GETMINMAXINFO: {
                auto info = reinterpret_cast<MINMAXINFO*>(l);
                MONITORINFO monitor{sizeof(monitor)};
                GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &monitor);
                info->ptMinTrackSize = {
                    std::min(self->S(820), static_cast<int>(monitor.rcWork.right - monitor.rcWork.left)),
                    std::min(self->S(620), static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top))};
                return 0;
            }
            case WM_DPICHANGED: {
                self->exposureIndex = static_cast<int>(SendMessageW(self->H(Exposure), CB_GETCURSEL, 0, 0));
                self->dpi = HIWORD(w);
                self->ApplyAppearance();
                auto r = reinterpret_cast<RECT*>(l);
                SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                return 0;
            }
            case WM_COMMAND:
                if (HIWORD(w) == BN_CLICKED)
                    self->Command(LOWORD(w));
                return 0;
            case EventMessage:
                self->OnEvent(std::unique_ptr<Event>(reinterpret_cast<Event*>(l)));
                return 0;
            case WM_DROPFILES: {
                auto drop = reinterpret_cast<HDROP>(w);
                const auto count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
                std::wstring name;
                if (count == 1) {
                    const auto size = DragQueryFileW(drop, 0, nullptr, 0);
                    name.resize(size + 1);
                    DragQueryFileW(drop, 0, name.data(), size + 1);
                    name.resize(size);
                }
                DragFinish(drop);
                if (count == 1)
                    self->ProbeInput(name);
                else
                    self->Stage(TextId::OneFile);
                return 0;
            }
            case WM_TIMER:
                KillTimer(h, w);
                if (w == 9001)
                    self->Finish(true, "Native drop/probe/convert/validation completed");
                else if (w == 9002)
                    self->UiTest();
                return 0;
            case WM_CLOSE:
                if (self->busy) {
                    self->closing = true;
                    self->cancelled = true;
                    self->Stage(TextId::Stopping);
                    EnableWindow(h, FALSE);
                } else
                    DestroyWindow(h);
                return 0;
            case WM_DESTROY:
                PostQuitMessage(self->exitCode);
                return 0;
            }
        } catch (const AppError& e) {
            self->logger.Write(e.what());
            if (m == WM_CREATE)
                return -1;
            self->Failure(e);
            if (self->uiTest || self->smoke)
                self->Finish(false, e.what());
        } catch (const std::exception& e) {
            self->logger.Write(e.what());
            if (m == WM_CREATE)
                return -1;
            self->Failure(AppError({TextId::Unexpected, {e.what()}}));
            if (self->uiTest || self->smoke)
                self->Finish(false, e.what());
        }
        return DefWindowProcW(h, m, w, l);
    }
};
int Run(HINSTANCE instance) {
    MainWindow app;
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);
    LocalFree(argv);
    size_t option = 0;
    if (args.size() >= 3 && args[0] == L"--smoke-test") {
        app.smoke = true;
        app.smokeInput = args[1];
        app.smokeOutput = args[2];
        option = 3;
        if (option < args.size() && !args[option].starts_with(L"--")) {
            const double value = std::stod(args[option++]);
            const auto it = std::find(std::begin(ExposureValues), std::end(ExposureValues), value);
            if (it == std::end(ExposureValues))
                throw AppError(TextId::InvalidExposureChoice);
            app.exposureIndex = static_cast<int>(it - std::begin(ExposureValues));
        }
    } else if (args.size() >= 2 && args[0] == L"--ui-test") {
        app.uiTest = true;
        app.uiDirectory = args[1];
        fs::create_directories(app.uiDirectory);
        option = 2;
    }
    for (; option < args.size(); ++option) {
        if (args[option] == L"--tone")
            app.settings.tone.enabled = true;
        else if (args[option] == L"--input-chroma-location" && option + 1 < args.size()) {
            app.explicitInputChroma = Utf8(args[++option]);
            if (app.explicitInputChroma != "left" && app.explicitInputChroma != "center")
                throw AppError(TextId::InputChroma);
        } else if (args[option] == L"--missing-ffmpeg" && app.uiTest)
            app.missingScenario = true;
        else if (args[option] == L"--language" && option + 1 < args.size()) {
            const auto v = args[++option];
            if (v != L"en" && v != L"zh-CN")
                throw AppError(TextId::CLILanguage);
            app.settings.language = v == L"en" ? Language::English : Language::SimplifiedChinese;
        } else if (args[option] == L"--theme" && option + 1 < args.size()) {
            const auto v = args[++option];
            if (v != L"dark" && v != L"light")
                throw AppError(TextId::CLICommand);
            app.settings.theme = v == L"dark" ? Theme::Dark : Theme::Light;
        } else if (args[option] == L"--dpi" && app.uiTest && option + 1 < args.size()) {
            app.testDpi = std::stoi(args[++option]);
            if (app.testDpi < 96 || app.testDpi > 288)
                throw AppError(TextId::CLICommand);
        } else
            throw AppError(TextId::CLICommand);
    }
    WNDCLASSEXW c{sizeof(c)};
    c.style = CS_HREDRAW | CS_VREDRAW;
    c.lpfnWndProc = MainWindow::Proc;
    c.hInstance = instance;
    c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    c.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    c.hIconSm = c.hIcon;
    c.lpszClassName = L"LogForge.MainWindow";
    RegisterClassExW(&c);
    const auto dpi = app.testDpi ? app.testDpi : static_cast<int>(GetDpiForSystem());
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = std::min(MulDiv(1020, dpi, 96), static_cast<int>(work.right - work.left) - 24);
    const int height = std::min(MulDiv(840, dpi, 96), static_cast<int>(work.bottom - work.top) - 24);
    auto window = CreateWindowExW(
        WS_EX_ACCEPTFILES | WS_EX_CONTROLPARENT, c.lpszClassName, L"LogForge",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, work.left + (work.right - work.left - width) / 2,
        work.top + (work.bottom - work.top - height) / 2, width, height, nullptr, nullptr, instance, &app);
    if (!window)
        throw AppError(TextId::DialogOpenFailed);
    ShowWindow(window, app.uiTest || app.smoke ? SW_SHOWNOACTIVATE : SW_SHOW);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    while (PeekMessageW(&message, window, EventMessage, EventMessage, PM_REMOVE))
        delete reinterpret_cast<Event*>(message.lParam);
    return app.exitCode;
}
} // namespace logforge
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&controls);
    int result = 1;
    try {
        result = logforge::Run(instance);
    } catch (const std::exception& e) {
        MessageBoxW(nullptr, logforge::Wide(e.what()).c_str(), L"LogForge", MB_OK | MB_ICONERROR);
    }
    if (SUCCEEDED(com))
        CoUninitialize();
    return result;
}
