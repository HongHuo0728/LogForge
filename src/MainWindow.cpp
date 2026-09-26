#include "logforge/Queue.h"
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
#include <utility>
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
    RescanButton,
    DeepSearchButton,
    CandidatesButton,
    QueueText,
    SourceLabel,
    OutputLabel,
    TargetLabel,
    ExposureLabel,
    CreativeText,
    FooterLeft,
    FooterRight
};
enum class Kind {
    QueueItem,
    QueueDone,
    Progress,
    Discovery,
    Detect,
    Verify,
    Probe,
    Convert,
    Install,
    Approval,
    Failure
};
struct Event {
    Kind kind;
    JobProgress progress;
    DiscoveryProgress discovery;
    std::optional<FFmpegInstallation> tools;
    std::optional<MediaInfo> media;
    Message error{TextId::Unexpected, {"Unknown error"}};
    std::vector<Message> details;
    std::optional<ValidationReport> validation;
    DiscoveryReport discoveryReport;
    ToolIdentity identity;
    Json queue;
    size_t itemIndex = 0, itemCount = 0;
};
std::optional<fs::path> SelectFile(HWND owner, bool save, bool executable, Language language,
                                   const fs::path& defaultPath = {},
                                   std::vector<fs::path>* selection = nullptr, bool selectFolder = false) {
    ComPtr<IFileDialog> dialog;
    HRESULT hr =
        save ? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))
             : CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr))
        throw AppError(TextId::DialogOpenFailed);
    DWORD flags = 0;
    dialog->GetOptions(&flags);
    dialog->SetOptions(flags | FOS_FORCEFILESYSTEM |
                       (selectFolder ? FOS_PICKFOLDERS | FOS_PATHMUSTEXIST
                        : save       ? FOS_PATHMUSTEXIST
                                     : FOS_FILEMUSTEXIST) |
                       (selection ? FOS_ALLOWMULTISELECT : 0));
    const auto movieLabel = TranslateWide(TextId::VideoFiles, language),
               allLabel = TranslateWide(TextId::AllFiles, language);
    const COMDLG_FILTERSPEC movie[] = {{movieLabel.c_str(), L"*.mov"}, {allLabel.c_str(), L"*.*"}};
    const auto executableLabel = TranslateWide(TextId::FFmpegExecutable, language);
    const COMDLG_FILTERSPEC exe[] = {{executableLabel.c_str(), L"ffmpeg.exe"}};
    const COMDLG_FILTERSPEC mov[] = {{L"QuickTime MOV", L"*.mov"}};
    if (!selectFolder)
        dialog->SetFileTypes(executable ? 1 : save ? 1 : 2, executable ? exe : save ? mov : movie);
    if (save)
        dialog->SetDefaultExtension(L"mov");
    if (!defaultPath.empty()) {
        if (!selectFolder)
            dialog->SetFileName(defaultPath.filename().c_str());
        ComPtr<IShellItem> folder;
        if (SUCCEEDED(
                SHCreateItemFromParsingName((selectFolder ? defaultPath : defaultPath.parent_path()).c_str(),
                                            nullptr, IID_PPV_ARGS(&folder))))
            dialog->SetFolder(folder.Get());
    }
    dialog->SetTitle(TranslateWide(selectFolder ? TextId::ChooseOutput
                                   : executable ? TextId::SelectFFmpeg
                                   : save       ? TextId::SaveVideo
                                                : TextId::OpenVideo,
                                   language)
                         .c_str());
    hr = dialog->Show(owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return {};
    if (FAILED(hr))
        throw AppError(TextId::DialogFailed);
    if (selection) {
        ComPtr<IFileOpenDialog> multi;
        ComPtr<IShellItemArray> items;
        if (FAILED(dialog.As(&multi)) || FAILED(multi->GetResults(&items)))
            throw AppError(TextId::DialogFailed);
        DWORD n = 0;
        items->GetCount(&n);
        for (DWORD i = 0; i < n; ++i) {
            ComPtr<IShellItem> item;
            PWSTR path = nullptr;
            if (SUCCEEDED(items->GetItemAt(i, &item)) &&
                SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                selection->emplace_back(path);
                CoTaskMemFree(path);
            }
        }
        return selection->empty() ? std::optional<fs::path>{} : std::optional(selection->front());
    }
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item)) || !item)
        throw AppError(TextId::DialogFailed);
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
    std::map<int, RECT> layoutRects;
    std::map<int, std::wstring> wrappedText;
    std::vector<RECT> cards;
    int contentHeight = 0, footerContentTop = 0;
    std::jthread worker;
    std::atomic_bool cancelled = false;
    std::optional<FFmpegInstallation> tools;
    std::optional<MediaInfo> source;
    std::vector<fs::path> queueInputs;
    Json queueResults;
    std::optional<ValidationReport> lastValidation;
    std::vector<DiscoveryCandidate> candidates;
    DiscoveryOptions discoveryOptions;
    DiscoveryReport discoveryReport;
    fs::path reviewAfterSearch, reviewing;
    fs::path selected, smokeInput, smokeSecond, smokeOutput, uiDirectory;
    std::string explicitInputChroma;
    bool busy = false, closing = false, detectionComplete = false, smoke = false, smokeStarted = false;
    bool layoutOnly = false;
    bool uiTest = false, missingScenario = false, settingsExercised = false, lastSignalWarning = false;
    bool conversionTone = false, layingOut = false;
    bool exercisingFlow = false, closeSearchTest = false;
    Json uiChecks = Json::object();
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
        SendMessageW(H(ProgressBar), PBM_SETPOS, 0, 0);
        Buttons();
        Layout();
        struct RestoreOnStartFailure {
            MainWindow& owner;
            bool started = false;
            ~RestoreOnStartFailure() {
                if (!started) {
                    owner.busy = false;
                    owner.Buttons();
                    owner.Layout();
                }
            }
        } startGuard{*this};
        worker = std::jthread([this, work = std::move(work)] {
            try {
                work();
            } catch (const AppError& error) {
                try {
                    logger.Write(std::string("ERROR[") + MessageKey(error.message.id) + "] " + error.what());
                } catch (...) {
                }
                Event event{Kind::Failure};
                event.error = error.message;
                event.details = error.details;
                Post(std::move(event));
            } catch (const std::exception& error) {
                try {
                    logger.Write(std::string("ERROR: ") + error.what());
                } catch (...) {
                }
                Event event{Kind::Failure};
                event.error = {TextId::Unexpected, {error.what()}};
                Post(std::move(event));
            } catch (...) {
                Event event{Kind::Failure};
                event.error = {TextId::Unexpected, {"Unknown exception"}};
                Post(std::move(event));
            }
        });
        startGuard.started = true;
    }
    void Stage(Message value) {
        stage = std::move(value);
        RefreshStatus();
    }
    void RefreshStatus() {
        std::wostringstream s;
        s << T(stage);
        if (!busy && discoveryReport.elapsedMs && (active == Kind::Detect || active == Kind::Verify))
            s << L"\r\n"
              << T({TextId::DiscoveryTiming,
                    {std::to_string(discoveryReport.elapsedMs),
                     std::to_string(discoveryReport.verificationMs)}});
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
        Text(ToolText, T(tools ? TextId::ToolsReady
                         : busy && (active == Kind::Verify ||
                                    (active == Kind::Detect && discovery.phase == DiscoveryPhase::Verifying))
                             ? TextId::VerifyingTools
                         : busy && active == Kind::Detect ? TextId::Detecting
                                                          : TextId::NeedTools));
        Text(CreativeText, T(settings.tone.enabled ? TextId::GradeOn : TextId::GradeOff));
        Text(QueueText,
             queueInputs.size() > 1 ? T({TextId::QueueSelected, {std::to_string(queueInputs.size())}}) : L"");
        EnableWindow(H(DetailsButton), !details.empty() || !rawDetails.empty() || source.has_value());
        Layout();
    }
    void Buttons() {
        for (int id : {Open, ChooseOutput, OutputEdit, Exposure, SettingsButton})
            EnableWindow(H(id), !busy);
        EnableWindow(H(Convert),
                     !busy && tools &&
                         (queueInputs.size() > 1 || (source && source->UnsupportedReasons().empty())));
        EnableWindow(H(Cancel), busy);
        // Cancelled/failed discovery must not strand the user without an alternative.
        const bool fallback = !tools && !busy;
        for (int id : {Install, Manual}) {
            ShowWindow(H(id), fallback ? SW_SHOW : SW_HIDE);
            EnableWindow(H(id), fallback);
        }
        ShowWindow(H(RescanButton), !tools && !busy ? SW_SHOW : SW_HIDE);
        EnableWindow(H(RescanButton), !busy);
        ShowWindow(H(DeepSearchButton), fallback ? SW_SHOW : SW_HIDE);
        EnableWindow(H(DeepSearchButton), fallback);
        const bool review =
            !tools && !candidates.empty() &&
            (!busy || (active == Kind::Detect && discoveryOptions.mode == DiscoveryMode::Deep &&
                       discovery.phase != DiscoveryPhase::Verifying));
        ShowWindow(H(CandidatesButton), !tools && !candidates.empty() ? SW_SHOW : SW_HIDE);
        EnableWindow(H(CandidatesButton), review);
        RefreshStatus();
    }
    void Detect(DiscoveryMode mode = DiscoveryMode::Quick) {
        detectionComplete = false;
        discovery = {};
        discoveryReport = {};
        candidates.clear();
        reviewing.clear();
        discoveryOptions.mode = mode;
        Stage(TextId::Detecting);
        Start(Kind::Detect, [this] {
            FFmpegManager manager(logger);
            auto found = manager.Detect(
                cancelled,
                [this](const DiscoveryProgress& p) {
                    Event e{Kind::Discovery};
                    e.discovery = p;
                    Post(std::move(e));
                },
                discoveryOptions);
            Event e{Kind::Detect};
            e.tools = std::move(found);
            e.discoveryReport = manager.Discovery();
            Post(std::move(e));
        });
    }
    void InspectCandidate(const fs::path& path) {
        reviewing = path;
        Stage(TextId::VerifyingTools);
        Start(Kind::Verify, [this, path] {
            Event e{Kind::Approval};
            e.identity = ToolTrust::Inspect(path);
            Post(std::move(e));
        });
    }
    void SelectInputs(std::vector<fs::path> paths) {
        if (paths.empty() || (busy && active != Kind::Detect))
            return;
        queueInputs = std::move(paths);
        queueResults = Json();
        lastValidation.reset();
        ProbeInput(queueInputs.front());
    }
    void ProbeInput(const fs::path& path) {
        if (busy && active != Kind::Detect)
            return;
        selected = path;
        source.reset();
        Text(DropZone, path.filename().wstring());
        Text(SourceText, T(TextId::ReadingMedia));
        Text(OutputEdit,
             (queueInputs.size() > 1 ? path.parent_path()
                                     : path.parent_path() / (path.stem().wstring() + L"_AppleLog.mov"))
                 .wstring());
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
        if (!tools || (!source && queueInputs.size() < 2))
            return;
        const auto path = smoke ? smokeOutput : fs::path(WindowText(H(OutputEdit)));
        const auto index = SendMessageW(H(Exposure), CB_GETCURSEL, 0, 0);
        if (index < 0 || index >= static_cast<LRESULT>(std::size(ExposureValues)))
            throw AppError(TextId::InvalidExposureChoice);
        exposureIndex = static_cast<int>(index);
        TranscodeOptions options{ExposureValues[index], settings.tone, settings.backend};
        conversionTone = options.tone.enabled;
        lastValidation.reset();
        if (queueInputs.size() > 1) {
            const auto inputs = queueInputs;
            const auto directory = path;
            // A label on one file is not a user declaration for other files.
            const auto chroma = source ? source->inputChromaOverride : explicitInputChroma;
            Stage(TextId::Starting);
            Start(Kind::Convert, [this, inputs, directory, chroma, options] {
                auto result = RunQueue(
                    *tools, inputs, directory, logger, cancelled, options, chroma,
                    [this](size_t index, size_t total, const MediaInfo& media, const JobProgress& p) {
                        Event e{Kind::QueueItem};
                        e.itemIndex = index;
                        e.itemCount = total;
                        e.media = media;
                        e.progress = p;
                        Post(std::move(e));
                    });
                Event e{Kind::QueueDone};
                e.queue = std::move(result);
                Post(std::move(e));
            });
            return;
        }
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
        if (wcscmp(type, L"STATIC") == 0)
            flags = (flags & ~SS_TYPEMASK) | SS_OWNERDRAW;
        controls[id] = MakeControl(parent, id, type, label, flags | WS_CLIPSIBLINGS, style, compact);
    }
    void ApplyAppearance() {
        style.Apply(settings.theme, settings.language, dpi);
        style.Window(window);
        for (auto [id, h] : controls)
            style.Control(h, id == ToolText || id == StageText || id == CreativeText || id == FooterLeft ||
                                 id == FooterRight || id == SourceLabel || id == OutputLabel ||
                                 id == ExposureLabel);
        for (auto [id, key] : {std::pair{Open, TextId::Open},
                               {ChooseOutput, TextId::ChooseOutput},
                               {Convert, TextId::Convert},
                               {Cancel, TextId::Cancel},
                               {Install, TextId::Install},
                               {Manual, TextId::Manual},
                               {Logs, TextId::Logs},
                               {SettingsButton, TextId::Settings},
                               {DetailsButton, TextId::Details},
                               {RescanButton, TextId::Rescan},
                               {DeepSearchButton, TextId::DeepSearch},
                               {CandidatesButton, TextId::ReviewFFmpeg}})
            Text(id, T(key));
        Text(DropZone, selected.empty() ? T(TextId::Drop) : selected.filename().wstring());
        Text(SourceText, source ? source->Summary(settings.language) : T(TextId::AwaitInput));
        Text(SourceLabel, T(TextId::Source));
        Text(OutputLabel, T(TextId::Output));
        Text(TargetLabel, L"Apple Log · BT.2020 · ProRes 422 HQ · 10-bit 4:2:2 · MOV");
        Text(ExposureLabel, T(TextId::Exposure));
        Text(FooterLeft, T(TextId::License) + L"\r\n" + T(TextId::Privacy));
        Text(FooterRight, RightFooter());
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
        Add(window, SettingsButton, L"BUTTON", L"", WS_TABSTOP | BS_OWNERDRAW);
        for (int id :
             {Install, Manual, Logs, DetailsButton, RescanButton, DeepSearchButton, CandidatesButton})
            Add(panel, id, L"BUTTON", L"", WS_TABSTOP | BS_OWNERDRAW);
        for (int id : {ToolText, StageText, CreativeText, QueueText, SourceLabel, OutputLabel, ExposureLabel,
                       FooterLeft, FooterRight})
            Add(panel, id, L"STATIC", L"", SS_NOPREFIX, true);
        Add(panel, TargetLabel, L"STATIC", L"", SS_NOPREFIX);
        ApplyAppearance();
        Buttons();
        if (uiTest && closeSearchTest) {
            Start(Kind::Detect, [this] {
                RunProcess(ExecutableDirectory() / L"logforge_process_fixture.exe", {L"heldpipe"}, &cancelled,
                           10, [&](const std::string& line) {
                               std::ofstream file(uiDirectory / L"descendant.txt");
                               file << line;
                               file.close();
                               PostMessageW(window, WM_CLOSE, 0, 0);
                           });
                Post(Event{Kind::Detect});
            });
        } else if (uiTest && missingScenario) {
            detectionComplete = true;
            Stage(TextId::ToolsMissing);
            Buttons();
            SetTimer(window, 9002, 300, nullptr);
        } else
            Detect();
    }
    std::wstring RightFooter() const {
        return T(TextId::Devices) + L"\r\n" + T(TextId::FormatRequired) + L"\r\n" +
               T(TextId::ResolutionChoice) + L"\r\n" + T(TextId::EditorHint);
    }
    // One layout owner. All text, controls and card bounds derive from these
    // measured rectangles; Paint never computes a competing layout.
    void Layout() {
        if (layingOut || !panel || !H(FooterRight))
            return;
        layingOut = true;
        RECT client{};
        GetClientRect(window, &client);
        const int width = MulDiv(client.right, 96, dpi);
        MoveWindow(panel, S(18), S(78), std::max(1, static_cast<int>(client.right) - S(36)),
                   std::max(1, static_cast<int>(client.bottom) - S(90)), FALSE);
        MoveWindow(H(SettingsButton), S(width - 140), S(22), S(116), S(36), FALSE);
        RECT viewport{};
        GetClientRect(panel, &viewport);
        const int w = std::max(320, MulDiv(viewport.right, 96, dpi)),
                  h = std::max(1, MulDiv(viewport.bottom, 96, dpi));
        HDC dc = GetDC(panel);
        wrappedText.clear();
        auto textHeight = [&](int id, int available, int minimum = 0) {
            auto font = reinterpret_cast<HFONT>(SendMessageW(H(id), WM_GETFONT, 0, 0));
            auto old = SelectObject(dc, font);
            RECT r{0, 0, S(std::max(1, available)), 0};
            const auto original = WindowText(H(id));
            std::wstring text;
            // Break an overlong path/token as well as ordinary words. Use this
            // exact string for both measurement and drawing.
            size_t at = 0;
            while (at < original.size()) {
                auto end = original.find(L'\n', at);
                if (end == std::wstring::npos)
                    end = original.size();
                auto paragraph = original.substr(at, end - at);
                if (!paragraph.empty() && paragraph.back() == L'\r')
                    paragraph.pop_back();
                size_t offset = 0;
                while (offset < paragraph.size()) {
                    int fit = 0;
                    SIZE extent{};
                    GetTextExtentExPointW(dc, paragraph.c_str() + offset,
                                          static_cast<int>(paragraph.size() - offset), r.right, &fit, nullptr,
                                          &extent);
                    size_t count = std::max(1, fit);
                    if (offset + count < paragraph.size()) {
                        const auto split = paragraph.find_last_of(L" /\\", offset + count - 1);
                        if (split != std::wstring::npos && split >= offset)
                            count = split - offset + 1;
                        if (count && offset + count < paragraph.size() &&
                            IS_HIGH_SURROGATE(paragraph[offset + count - 1]) &&
                            IS_LOW_SURROGATE(paragraph[offset + count]))
                            count = count > 1 ? count - 1 : 2;
                    }
                    text.append(paragraph, offset, count);
                    offset += count;
                    if (offset < paragraph.size())
                        text += L'\n';
                }
                if (end < original.size())
                    text += L'\n';
                at = end + 1;
            }
            wrappedText[id] = text;
            DrawTextW(dc, text.c_str(), -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(dc, old);
            return std::max(minimum, MulDiv(r.bottom + S(3), 96, dpi));
        };
        layoutRects.clear();
        cards.clear();
        auto place = [&](int id, int x, int y, int cw, int ch) { layoutRects[id] = {x, y, x + cw, y + ch}; };
        auto line = [&](int id, int& y, int minimum = 20) {
            const int th = textHeight(id, w - 40, minimum);
            place(id, 20, y, w - 40, th);
            y += th + 8;
        };
        int y = 18;
        place(DropZone, 20, y, w - 194, 36);
        place(Open, w - 158, y, 138, 36);
        y += 50;
        line(SourceLabel, y);
        line(SourceText, y, 92);
        if (queueInputs.size() > 1) {
            ShowWindow(H(QueueText), SW_SHOW);
            line(QueueText, y);
        } else
            ShowWindow(H(QueueText), SW_HIDE);
        cards.push_back({0, 4, w, y + 8});
        y += 30;
        const int outputTop = y;
        line(OutputLabel, y);
        line(TargetLabel, y);
        place(OutputEdit, 20, y, w - 198, 34);
        place(ChooseOutput, w - 166, y, 146, 34);
        y += 48;
        place(ExposureLabel, 20, y + 3, 90, 28);
        place(Exposure, 118, y, 152, 30);
        y += 48;
        place(Convert, 20, y, 184, 42);
        place(Cancel, 216, y, 100, 42);
        y += 56;
        place(ProgressBar, 20, y, w - 40, 14);
        y += 32;
        cards.push_back({0, outputTop - 10, w, y});
        y += 20;
        const int statusTop = y;
        line(ToolText, y);
        line(CreativeText, y);
        line(StageText, y);
        place(Logs, 20, y, 110, 34);
        place(DetailsButton, 142, y, 110, 34);
        if (IsWindowVisible(H(CandidatesButton)))
            place(CandidatesButton, 264, y, w - 284, 34);
        y += 48;
        if (IsWindowVisible(H(RescanButton))) {
            const int half = (w - 52) / 2;
            place(RescanButton, 20, y, half, 38);
            place(DeepSearchButton, 32 + half, y, half, 38);
            y += 50;
            place(Install, 20, y, half, 38);
            place(Manual, 32 + half, y, half, 38);
            y += 50;
        }
        cards.push_back({0, statusTop - 8, w, y});
        y += 20;
        footerContentTop = y;
        const int column = (w - 52) / 2;
        const int left = textHeight(FooterLeft, column), right = textHeight(FooterRight, column);
        place(FooterLeft, 20, y + 12, column, left);
        place(FooterRight, 32 + column, y + 12, column, right);
        contentHeight = y + std::max(left, right) + 28;
        ReleaseDC(panel, dc);
        scroll = std::clamp(scroll, 0, std::max(0, contentHeight - h));
        SCROLLINFO si{sizeof(si),
                      SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL,
                      0,
                      contentHeight - 1,
                      static_cast<UINT>(h),
                      scroll};
        SetScrollInfo(panel, SB_VERT, &si, TRUE);
        auto batch = BeginDeferWindowPos(static_cast<int>(layoutRects.size()));
        for (const auto& [id, r] : layoutRects) {
            if (batch)
                batch = DeferWindowPos(batch, H(id), nullptr, S(r.left), S(r.top - scroll),
                                       S(r.right - r.left), S(id == Exposure ? 220 : r.bottom - r.top),
                                       SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
            else
                SetWindowPos(H(id), nullptr, S(r.left), S(r.top - scroll), S(r.right - r.left),
                             S(id == Exposure ? 220 : r.bottom - r.top),
                             SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        }
        if (batch)
            EndDeferWindowPos(batch);
        layingOut = false;
        // Erase the old rectangles before repainting moved children. No stale
        // status glyphs can survive a resize, theme, language or state change.
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
    void LayoutPanel() {
        Layout();
    }
    void Draw(const DRAWITEMSTRUCT& item) {
        const int saved = SaveDC(item.hDC);
        struct Restore {
            HDC dc;
            int saved;
            ~Restore() {
                if (saved)
                    RestoreDC(dc, saved);
            }
        } restore{item.hDC, saved};
        if (item.CtlType != ODT_STATIC) {
            style.DrawItem(item);
            return;
        }
        const int id = GetDlgCtrlID(item.hwndItem);
        const bool footer = id == FooterLeft || id == FooterRight;
        FillRect(item.hDC, &item.rcItem, footer ? style.background : style.surface);
        const bool compact = GetPropW(item.hwndItem, L"compact") != nullptr;
        const auto flags = id == DropZone
                               ? DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX
                               : DT_LEFT | DT_WORDBREAK | DT_NOPREFIX;
        const auto text = wrappedText.contains(id) ? wrappedText.at(id) : WindowText(item.hwndItem);
        style.Text(item.hDC, text, item.rcItem, compact, compact, flags);
    }
    void Paint(HDC printDC = nullptr) {
        PAINTSTRUCT ps{};
        auto dc = printDC ? printDC : BeginPaint(window, &ps);
        RECT r{};
        GetClientRect(window, &r);
        FillRect(dc, &r, style.background);
        const int width = MulDiv(r.right, 96, dpi);
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
        if (!printDC)
            EndPaint(window, &ps);
    }
    void PaintPanel(HDC printDC = nullptr) {
        PAINTSTRUCT ps{};
        auto dc = printDC ? printDC : BeginPaint(panel, &ps);
        RECT r{};
        GetClientRect(panel, &r);
        FillRect(dc, &r, style.background);
        for (const auto& card : cards) {
            RECT bounds{S(card.left), S(card.top - scroll), S(card.right), S(card.bottom - scroll)};
            style.Card(dc, bounds);
        }
        HPEN pen = CreatePen(PS_SOLID, 1, style.colors.border);
        auto old = SelectObject(dc, pen);
        MoveToEx(dc, S(20), S(footerContentTop - scroll), nullptr);
        LineTo(dc, r.right - S(20), S(footerContentTop - scroll));
        SelectObject(dc, old);
        DeleteObject(pen);
        if (!printDC)
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
            if (discovery.candidate) {
                candidates.push_back(*discovery.candidate);
                Buttons();
                Layout();
            }
            if (discovery.phase == DiscoveryPhase::ScanningDrive)
                Stage({TextId::Scanning,
                       {PathText(discovery.drive), std::to_string(discovery.directories),
                        std::to_string(discovery.candidates), std::to_string(discovery.skipped)}});
            else if (discovery.phase == DiscoveryPhase::CheckingCandidate)
                Stage({TextId::CheckingCandidate, {PathText(discovery.drive)}});
            else if (discovery.phase == DiscoveryPhase::Verifying) {
                Stage(TextId::VerifyingTools);
                Buttons();
            }
            return;
        }
        if (e->kind == Kind::QueueItem) {
            source = std::move(e->media);
            Text(SourceText, source->Summary(settings.language));
            Text(DropZone, source->path.filename().wstring());
            currentProgress = e->progress;
            stage = e->progress.stage;
            SendMessageW(H(ProgressBar), PBM_SETPOS,
                         static_cast<WPARAM>(std::clamp(e->progress.fraction, 0., 1.) * 1000), 0);
            RefreshStatus();
            Text(QueueText, T({TextId::QueueProgress,
                               {std::to_string(e->itemIndex + 1), std::to_string(e->itemCount)}}));
            Layout();
            return;
        }
        busy = false;
        if (worker.joinable())
            worker.join();
        if (closing) {
            DestroyWindow(window);
            return;
        }
        if (e->kind == Kind::QueueDone) {
            queueResults = e->queue;
            rawDetails = Wide(queueResults.dump(2));
            Stage(cancelled ? Message(TextId::Cancelled)
                            : Message(TextId::QueueComplete,
                                      {queueResults["successful"].dump(), queueResults["failed"].dump()}));
            Buttons();
            if (smoke)
                Finish(queueResults["successful"].get<size_t>() > 0 &&
                           queueResults["remaining"].get<size_t>() == 0,
                       "Native queue completed; each item retains its own result");
            return;
        }
        if (e->kind == Kind::Failure) {
            if (active == Kind::Probe && queueInputs.size() > 1)
                Text(SourceText, T(TextId::QueueFirstFailed));
            detectionComplete = true;
            details = e->details;
            details.insert(details.begin(), e->error);
            for (auto& candidate : candidates)
                if (candidate.ffmpeg == reviewing) {
                    candidate.issue = e->error;
                    candidate.state = e->error.id == TextId::FFmpegHashChanged ? CandidateState::Changed
                                                                               : CandidateState::Incompatible;
                }
            Stage(cancelled ? TextId::Cancelled : TextId::Failed);
            Buttons();
            Layout();
            if (!reviewAfterSearch.empty()) {
                auto path = std::exchange(reviewAfterSearch, {});
                InspectCandidate(path);
                return;
            }
            if (smoke && active == Kind::Probe && queueInputs.size() > 1 && !smokeStarted) {
                smokeStarted = true;
                Text(OutputEdit, smokeOutput.wstring());
                BeginConvert();
                return;
            }
            if (smoke || (uiTest && !exercisingFlow))
                Finish(false, Translate(e->error));
            return;
        }
        if (e->kind == Kind::Approval) {
            Buttons();
            const auto body = T(Message(TextId::FFmpegApproveBody, {e->identity.ToJson().dump(2)}));
            if (MessageBoxW(window, body.c_str(), T(TextId::FFmpegApprove).c_str(),
                            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
                Stage(TextId::VerifyingTools);
                Start(Kind::Verify, [this, identity = e->identity] {
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
                Layout();
            }
            return;
        }
        if (e->kind == Kind::Detect || e->kind == Kind::Install) {
            tools = std::move(e->tools);
            discoveryReport = std::move(e->discoveryReport);
            candidates = discoveryReport.candidates;
            rawDetails = Wide(discoveryReport.ToJson().dump(2));
            for (const auto& candidate : candidates)
                details.push_back(candidate.issue);
            detectionComplete = true;
            if (discovery.skipped)
                details.emplace_back(TextId::ScanSkipped);
            if (settings.recoveredDefaults)
                details.emplace_back(TextId::SettingsRecovered);
            Stage(tools                                            ? TextId::Ready
                  : discoveryReport.end == DiscoveryEnd::Cancelled ? TextId::Cancelled
                  : discoveryReport.end == DiscoveryEnd::TimedOut  ? TextId::QuickSearchEnded
                  : discoveryReport.end == DiscoveryEnd::Failed    ? TextId::DiscoveryFailed
                  : candidates.empty()                             ? TextId::ToolsMissing
                                                                   : TextId::FFmpegDiscovered);
            Buttons();
            Layout();
            if (!reviewAfterSearch.empty()) {
                auto path = std::exchange(reviewAfterSearch, {});
                InspectCandidate(path);
                return;
            }
            if (uiTest && !exercisingFlow) {
                SetTimer(window, 9002, 300, nullptr);
                return;
            }
            if (smoke) {
                if (!tools) {
                    Finish(false, "FFmpeg unavailable");
                    return;
                }
                auto name = smokeInput.wstring();
                if (!smokeSecond.empty()) {
                    name += L'\0';
                    name += smokeSecond.wstring();
                }
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
                const auto question = T(TextId::InputChromaChoose) +
                                      (queueInputs.size() > 1 ? L"\r\n\r\n" + T(TextId::QueueChroma) : L"");
                const auto answer = MessageBoxW(window, question.c_str(), T(TextId::InputChromaTitle).c_str(),
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
            lastValidation = e->validation;
            rawDetails = e->validation ? Wide(e->validation->ToJson().dump(2)) : L"";
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
            if (source) {
                text = source->Summary(settings.language) + L"\r\n\r\n";
                text += T({TextId::DetailsRates,
                           {std::to_string(source->averageFps.Value()),
                            std::to_string(source->nominalFps.Value()), std::to_string(source->frames),
                            BackendName(settings.backend), std::to_string(ExposureValues[exposureIndex])}}) +
                        L"\r\n";
                text += T(settings.tone.enabled ? TextId::GradeOn : TextId::GradeOff) + L"\r\n\r\n" +
                        T(TextId::DetailsTarget) + L"\r\n\r\n";
            }
            for (const auto& message : details)
                text += T(message) + L"\r\n\r\n";
            text += rawDetails;
            ShowDetails(window, T(TextId::Details), text, settings);
            return;
        }
        if (id == CandidatesButton && IsWindowEnabled(H(CandidatesButton))) {
            // Copy the list: a deep-search worker can post new entries during the modal loop.
            const auto snapshot = candidates;
            if (auto path = ShowFFmpegCandidates(window, snapshot, settings)) {
                if (busy) {
                    reviewAfterSearch = *path;
                    cancelled = true;
                    Stage(TextId::Cancelling);
                } else
                    InspectCandidate(*path);
            }
            return;
        }
        if (busy)
            return;
        if (id == SettingsButton) {
            exposureIndex = static_cast<int>(SendMessageW(H(Exposure), CB_GETCURSEL, 0, 0));
            if (ShowSettings(window, settings))
                ApplyAppearance();
        } else if (id == Open) {
            std::vector<fs::path> paths;
            if (auto path = SelectFile(window, false, false, settings.language, {}, &paths))
                SelectInputs(std::move(paths));
        } else if (id == ChooseOutput) {
            if (auto path = SelectFile(window, queueInputs.size() < 2, false, settings.language,
                                       fs::path(WindowText(H(OutputEdit))), nullptr, queueInputs.size() > 1))
                Text(OutputEdit, path->wstring());
        } else if (id == Convert)
            BeginConvert();
        else if (id == RescanButton)
            Detect();
        else if (id == DeepSearchButton)
            Detect(DiscoveryMode::Deep);
        else if (id == Manual) {
            if (auto path = SelectFile(window, false, true, settings.language,
                                       candidates.empty() ? fs::path{} : candidates.front().ffmpeg)) {
                InspectCandidate(*path);
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
        report["reliability_checks"] = uiChecks;
        if (!queueResults.is_null())
            report["queue"] = queueResults;
        const auto path =
            uiTest ? uiDirectory / L"ui-test.json" : fs::path(smokeOutput.wstring() + L".gui-test.json");
        std::ofstream file(path);
        file << report.dump(2);
        file.close();
        if (!captured || !file)
            exitCode = 1;
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
    void LayoutRegression() {
        const auto saved = settings;
        const auto savedTools = tools;
        const auto savedStage = stage;
        const auto gdiBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        Json cases = Json::array();
        RECT original{};
        GetWindowRect(window, &original);
        for (int testScale : {96, 120, 144, 168, 192})
            for (const auto size : {std::pair{1280, 720}, std::pair{1920, 1080}, std::pair{2560, 1440}})
                for (auto language : {Language::English, Language::SimplifiedChinese})
                    for (auto theme : {Theme::Dark, Theme::Light})
                        for (bool creative : {false, true})
                            for (bool ready : {false, true})
                                for (auto state : {TextId::Ready, TextId::Converting, TextId::Failed,
                                                   TextId::CompleteStandard}) {
                                    settings.language = language;
                                    settings.theme = theme;
                                    settings.tone.enabled = creative;
                                    if (ready)
                                        tools.emplace();
                                    else
                                        tools.reset();
                                    busy = state == TextId::Converting;
                                    stage = state;
                                    active = Kind::Convert;
                                    RECT bounds{0, 0, size.first, size.second};
                                    AdjustWindowRectExForDpi(&bounds, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                                             FALSE, WS_EX_ACCEPTFILES | WS_EX_CONTROLPARENT,
                                                             testScale);
                                    bounds.right -= bounds.left;
                                    bounds.bottom -= bounds.top;
                                    bounds.left = 0;
                                    bounds.top = 0;
                                    SendMessageW(window, WM_DPICHANGED, MAKELONG(testScale, testScale),
                                                 reinterpret_cast<LPARAM>(&bounds));
                                    ApplyAppearance();
                                    Buttons();
                                    Layout();
                                    if (dpi != testScale || style.theme != theme)
                                        throw std::runtime_error("DPI/theme state changed during layout");
                                    Text(StageText, T(state) + L"\r\n" + std::wstring(180, L'W'));
                                    Layout();
                                    bool valid = true;
                                    RECT viewport{};
                                    GetClientRect(panel, &viewport);
                                    for (const auto& [id, r] : layoutRects) {
                                        valid &= r.left >= 0 && r.right <= MulDiv(viewport.right, 96, dpi) &&
                                                 r.top >= 0 && r.bottom <= contentHeight;
                                        for (const auto& [other, b] : layoutRects)
                                            if (id < other) {
                                                RECT intersection{};
                                                if (IntersectRect(&intersection, &r, &b))
                                                    valid = false;
                                            }
                                        // Scroll content is clipped by its viewport. Every logical item
                                        // must be reachable and the footer must follow the status card.
                                        if (id == FooterLeft || id == FooterRight)
                                            valid &=
                                                r.top >= footerContentTop && r.top >= cards.back().bottom;
                                    }
                                    auto status = layoutRects.at(StageText);
                                    scroll = std::max(0, static_cast<int>(status.top) - 20);
                                    Layout();
                                    RECT actual{};
                                    GetWindowRect(H(StageText), &actual);
                                    MapWindowPoints(nullptr, panel, reinterpret_cast<POINT*>(&actual), 2);
                                    // Very long errors can exceed one viewport; scrolling must expose
                                    // both ends, and content range includes every rendered line.
                                    valid &= actual.bottom > 0 && actual.top < viewport.bottom;
                                    if (!valid)
                                        throw std::runtime_error(
                                            "Production layout overlap / unreachable control");
                                    cases.push_back(
                                        {{"dpi", testScale},
                                         {"size", {size.first, size.second}},
                                         {"language", language == Language::English ? "en" : "zh-CN"},
                                         {"theme", theme == Theme::Dark ? "dark" : "light"},
                                         {"creative", creative},
                                         {"ffmpeg_ready", ready},
                                         {"state", MessageKey(state)},
                                         {"passed", true}});
                                    if (size.first == 1280 && !creative && ready && state == TextId::Failed) {
                                        scroll =
                                            std::max(0, contentHeight - MulDiv(viewport.bottom, 96, dpi));
                                        Layout();
                                        const auto name = std::to_wstring(testScale) +
                                                          (language == Language::English ? L"-en" : L"-zh") +
                                                          (theme == Theme::Dark ? L"-dark" : L"-light") +
                                                          L".png";
                                        if (!SaveWindowSnapshot(window, uiDirectory / name))
                                            throw std::runtime_error("Layout snapshot failed");
                                    }
                                }
        std::ofstream(uiDirectory / L"layout-matrix.json") << cases.dump(2);
        settings = saved;
        tools = savedTools;
        stage = savedStage;
        busy = false;
        dpi = testDpi ? testDpi : 96;
        scroll = 0;
        SetWindowPos(window, nullptr, original.left, original.top, original.right - original.left,
                     original.bottom - original.top, SWP_NOZORDER | SWP_NOACTIVATE);
        uiChecks["layout_gdi_before"] = gdiBefore;
        uiChecks["layout_gdi_after"] = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        for (int repeat = 0; repeat < 20; ++repeat) {
            ApplyAppearance();
            Layout();
        }
        const auto warmed = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        for (int repeat = 0; repeat < 50; ++repeat) {
            ApplyAppearance();
            Layout();
        }
        uiChecks["layout_gdi_stable_after_warmup"] =
            GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == warmed;
        if (!uiChecks["layout_gdi_stable_after_warmup"].get<bool>())
            throw std::runtime_error("GDI resources grow after repeated layout/theme refresh");
        ApplyAppearance();
        Buttons();
        Layout();
        uiChecks["layout_matrix_cases"] = cases.size();
        uiChecks["layout_nonoverlap_reachable_footer_separate"] = true;
    }
    void UiTest() {
        if (layoutOnly) {
            LayoutRegression();
            Finish(true, "Production layout matrix verified");
            return;
        }
        const auto original = settings;
        SetFocus(H(OutputEdit));
        uiChecks["keyboard_focus_visible"] = ScrollDeltaToReveal(panel, H(OutputEdit), dpi) == 0;
        SetFocus(H(Open));
        uiChecks["keyboard_focus_visible"] =
            uiChecks["keyboard_focus_visible"].get<bool>() && ScrollDeltaToReveal(panel, H(Open), dpi) == 0;
        uiChecks["multiline_edit_crlf"] = EditLines(L"a\nb\r\nc") == L"a\r\nb\r\nc";
        scroll = 0;
        LayoutPanel();
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
        const auto savedTools = tools;
        const auto savedCandidates = candidates;
        exercisingFlow = true;
        tools.reset();
        DiscoveryCandidate sample;
        sample.ffmpeg =
            uiDirectory / L"long Unicode 测试 path with spaces for candidate display/bin/ffmpeg.exe";
        sample.ffprobe = sample.ffmpeg.parent_path() / L"ffprobe.exe";
        sample.paired = true;
        sample.source = "ui-test-fixture";
        auto incomplete = sample;
        incomplete.paired = false;
        incomplete.state = CandidateState::MissingProbe;
        incomplete.issue = TextId::FFmpegPair;
        std::vector<DiscoveryCandidate> list{sample, incomplete};
        const auto chosen = ShowFFmpegCandidates(window, list, settings, uiDirectory / L"candidates.png", 0);
        const auto declined = ShowFFmpegCandidates(window, list, settings, {}, -1);
        const auto missing = ShowFFmpegCandidates(window, list, settings, {}, 1);
        uiChecks["candidate_select_cancel_missing_probe"] = chosen == sample.ffmpeg && !declined && !missing;
        // Warmed native dialog resource counts: no settings or trust are changed.
        // Let native close animations and deferred HWND destruction settle before
        // both samples; comparing transient live dialogs is not a leak measurement.
        auto settleDialogs = [&] {
            const auto until = GetTickCount64() + 400;
            while (GetTickCount64() < until) {
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
        };
        // Warm native font/listbox caches with the same sequence being measured,
        // independently of the preceding Settings/snapshot/selection scenarios.
        for (int i = 0; i < 20; ++i)
            ShowFFmpegCandidates(window, list, settings, {}, -1);
        settleDialogs();
        auto windowInventory = [] {
            Json result = Json::array();
            const auto collect = [](HWND h, LPARAM context) -> BOOL {
                wchar_t name[256]{};
                GetClassNameW(h, name, 256);
                reinterpret_cast<Json*>(context)->push_back(
                    {{"hwnd", reinterpret_cast<uintptr_t>(h)}, {"class", Utf8(name)}});
                return TRUE;
            };
            EnumThreadWindows(GetCurrentThreadId(), collect, reinterpret_cast<LPARAM>(&result));
            return result;
        };
        uiChecks["native_windows_before"] = windowInventory();
        const auto gdiBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        const auto userBefore = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        for (int i = 0; i < 20; ++i)
            ShowFFmpegCandidates(window, list, settings, {}, -1);
        settleDialogs();
        const auto gdiAfter = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        const auto userAfter = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        uiChecks["native_windows_after"] = windowInventory();
        uiChecks["dialog_cycles"] = 20;
        uiChecks["dialog_warmup_cycles"] = 20;
        uiChecks["gdi_before_after"] = {gdiBefore, gdiAfter};
        uiChecks["user_before_after"] = {userBefore, userAfter};
        uiChecks["dialog_resources_stable"] = gdiBefore == gdiAfter && userBefore == userAfter;
        auto fallback = [&] {
            return !busy && IsWindowVisible(H(Install)) && IsWindowEnabled(H(Install)) &&
                   IsWindowVisible(H(Manual)) && IsWindowVisible(H(RescanButton)) &&
                   IsWindowVisible(H(DeepSearchButton));
        };
        // Use the real completion handler; injected errors avoid network traffic.
        for (const auto id : {TextId::Cancelled, TextId::DownloadConnect, TextId::FFmpegHashChanged}) {
            busy = true;
            cancelled = id == TextId::Cancelled;
            auto event = std::make_unique<Event>(Kind::Failure);
            event->error = Message(id, {"Injected UI regression failure"});
            OnEvent(std::move(event));
            if (!fallback())
                throw std::runtime_error("Failure/cancel hid FFmpeg recovery actions");
        }
        uiChecks["cancel_download_hash_failure_recovery"] = true;
        // Exercise the actual approval dialog, choosing No without touching trust.
        auto approval = std::make_unique<Event>(Kind::Approval);
        approval->identity.ffmpeg = sample.ffmpeg;
        approval->identity.ffprobe = sample.ffprobe;
        const auto thread = GetCurrentThreadId();
        std::jthread reject([thread](std::stop_token stop) {
            while (!stop.stop_requested()) {
                bool sent = false;
                EnumThreadWindows(
                    thread,
                    [](HWND h, LPARAM context) -> BOOL {
                        wchar_t name[32]{};
                        GetClassNameW(h, name, 32);
                        if (wcscmp(name, L"#32770") == 0 && GetDlgItem(h, IDNO)) {
                            PostMessageW(h, WM_COMMAND, IDNO, 0);
                            *reinterpret_cast<bool*>(context) = true;
                            return FALSE;
                        }
                        return TRUE;
                    },
                    reinterpret_cast<LPARAM>(&sent));
                if (sent)
                    return;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        });
        cancelled = false;
        OnEvent(std::move(approval));
        reject.request_stop();
        reject.join();
        uiChecks["approval_refusal_recovery"] = fallback() && stage.id == TextId::FFmpegApprovalCancelled;
        // Start from stale counters and verify the next Quick request clears them.
        discovery.directories = 999;
        discovery.candidates = 999;
        discoveryOptions.roots = {uiDirectory / L"empty-discovery-fixture"};
        fs::create_directories(discoveryOptions.roots.front());
        Detect(DiscoveryMode::Quick);
        uiChecks["fresh_search_state"] = discovery.directories == 0 && discovery.candidates == 0;
        Command(Cancel);
        const auto deadline = GetTickCount64() + 6000;
        while (busy && GetTickCount64() < deadline) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        uiChecks["actual_search_cancel_recovery"] = fallback();
        if (busy)
            throw std::runtime_error("UI discovery cancellation deadline failed");
        exercisingFlow = false;
        tools = savedTools;
        candidates = savedCandidates;
        Stage(tools ? TextId::Ready : TextId::ToolsMissing);
        Buttons();
        Layout();
        const bool visibility = missingScenario
                                    ? IsWindowVisible(H(Install)) && IsWindowVisible(H(Manual))
                                    : tools && !IsWindowVisible(H(Install)) && !IsWindowVisible(H(Manual));
        bool checks = true;
        for (const auto& value : uiChecks)
            if (value.is_boolean() && !value.get<bool>())
                checks = false;
        Finish(settingsExercised && visibility && checks,
               "Native Settings, candidate selection, approval refusal, failure/cancel recovery and resource "
               "checks");
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
        case WM_PRINTCLIENT:
            self->PaintPanel(reinterpret_cast<HDC>(w));
            return 0;
        case FocusRevealMessage:
            self->scroll += ScrollDeltaToReveal(h, reinterpret_cast<HWND>(l), self->dpi);
            self->LayoutPanel();
            return 0;
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
            return self->style.Color(m, w, l,
                                     GetDlgCtrlID(reinterpret_cast<HWND>(l)) != FooterLeft &&
                                         GetDlgCtrlID(reinterpret_cast<HWND>(l)) != FooterRight);
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
            case WM_PRINTCLIENT:
                self->Paint(reinterpret_cast<HDC>(w));
                return 0;
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
            case WM_SETTINGCHANGE:
            case WM_SYSCOLORCHANGE:
                self->exposureIndex = static_cast<int>(SendMessageW(self->H(Exposure), CB_GETCURSEL, 0, 0));
                self->ApplyAppearance();
                return 0;
            case WM_DRAWITEM:
                self->Draw(*reinterpret_cast<DRAWITEMSTRUCT*>(l));
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
                    std::min(self->S(560), static_cast<int>(monitor.rcWork.right - monitor.rcWork.left)),
                    std::min(self->S(360), static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top))};
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
                std::vector<fs::path> paths;
                for (UINT i = 0; i < count; ++i) {
                    const auto size = DragQueryFileW(drop, i, nullptr, 0);
                    std::wstring name(size + 1, L'\0');
                    DragQueryFileW(drop, i, name.data(), size + 1);
                    name.resize(size);
                    paths.emplace_back(name);
                }
                DragFinish(drop);
                self->SelectInputs(std::move(paths));
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
            try {
                self->logger.Write(e.what());
            } catch (...) {
            }
            if (m == WM_CREATE)
                return -1;
            self->Failure(e);
            if (self->uiTest || self->smoke)
                self->Finish(false, e.what());
        } catch (const std::exception& e) {
            try {
                self->logger.Write(e.what());
            } catch (...) {
            }
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
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);
    LocalFree(argv);
    if (!args.empty() && args[0] == L"--internal-discover")
        return RunDiscoveryHelper(args);
    MainWindow app;
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
    } else if (args.size() >= 2 && (args[0] == L"--ui-test" || args[0] == L"--layout-test")) {
        app.layoutOnly = args[0] == L"--layout-test";
        app.missingScenario = app.layoutOnly;
        app.uiTest = true;
        app.uiDirectory = args[1];
        fs::create_directories(app.uiDirectory);
        option = 2;
    }
    for (; option < args.size(); ++option) {
        if (args[option] == L"--tone")
            app.settings.tone.enabled = true;
        else if (args[option] == L"--queue-next" && app.smoke && option + 1 < args.size())
            app.smokeSecond = args[++option];
        else if (args[option] == L"--input-chroma-location" && option + 1 < args.size()) {
            app.explicitInputChroma = Utf8(args[++option]);
            if (app.explicitInputChroma != "left" && app.explicitInputChroma != "center")
                throw AppError(TextId::InputChroma);
        } else if (args[option] == L"--close-search-test" && app.uiTest)
            app.closeSearchTest = true;
        else if (args[option] == L"--missing-ffmpeg" && app.uiTest)
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
