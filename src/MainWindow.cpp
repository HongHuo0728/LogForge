#include "logforge/Transcode.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <iomanip>
#include <memory>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <sstream>
#include <thread>
#include <uxtheme.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace logforge {
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT EventMessage = WM_APP + 1;
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
    DropZone
};
enum class Kind { Progress, Detect, Probe, Convert, Install, Failure };
struct Event {
    Kind kind;
    JobProgress progress;
    std::optional<FFmpegInstallation> tools;
    std::optional<MediaInfo> media;
    std::string error;
};
std::wstring ControlText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, 0);
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(n);
    return s;
}
bool SaveWindowSnapshot(HWND window, const fs::path& destination) {
    // A test-only capture of this application's own window, including child controls.
    RECT rect{};
    GetClientRect(window, &rect);
    HDC screen = GetDC(window);
    if (!screen)
        return false;
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, rect.right - rect.left, rect.bottom - rect.top);
    HGDIOBJ previous = SelectObject(memory, bitmap);
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    DwmFlush();
    BOOL painted = PrintWindow(window, memory, 3);
    SelectObject(memory, previous);
    DeleteDC(memory);
    ReleaseDC(window, screen);
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmap> image;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    HRESULT hr =
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr))
        hr = factory->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapIgnoreAlpha, &image);
    if (SUCCEEDED(hr))
        hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr))
        hr = stream->InitializeFromFilename(destination.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr))
        hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr))
        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr))
        hr = encoder->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr))
        hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr))
        hr = frame->WriteSource(image.Get(), nullptr);
    if (SUCCEEDED(hr))
        hr = frame->Commit();
    if (SUCCEEDED(hr))
        hr = encoder->Commit();
    DeleteObject(bitmap);
    return painted && SUCCEEDED(hr);
}
std::optional<fs::path> SelectFile(HWND owner, bool save, bool executable, const fs::path& defaultPath = {}) {
    ComPtr<IFileDialog> dialog;
    HRESULT hr =
        save ? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))
             : CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr))
        throw std::runtime_error("无法打开 Windows 文件选择器。");
    DWORD flags = 0;
    dialog->GetOptions(&flags);
    dialog->SetOptions(flags | FOS_FORCEFILESYSTEM | (save ? FOS_PATHMUSTEXIST : FOS_FILEMUSTEXIST));
    const COMDLG_FILTERSPEC movie[] = {{L"QuickTime / 视频文件", L"*.mov;*.mp4;*.mxf"},
                                       {L"所有文件", L"*.*"}};
    const COMDLG_FILTERSPEC exe[] = {{L"FFmpeg executable", L"ffmpeg.exe"}};
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
    dialog->SetTitle(executable ? L"选择 ffmpeg.exe（同目录必须有 ffprobe.exe）"
                     : save     ? L"保存 Apple Log ProRes MOV"
                                : L"打开 BT.2020 HLG ProRes 视频");
    hr = dialog->Show(owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return {};
    if (FAILED(hr))
        throw std::runtime_error("Windows 文件选择器失败。");
    ComPtr<IShellItem> item;
    dialog->GetResult(&item);
    PWSTR name = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)))
        throw std::runtime_error("无法读取文件路径。");
    fs::path result(name);
    CoTaskMemFree(name);
    return result;
}
} // namespace
class MainWindow {
  public:
    HWND window{};
    Logger logger;
    std::jthread worker;
    std::atomic_bool cancelled = false;
    std::optional<FFmpegInstallation> tools;
    std::optional<MediaInfo> source;
    fs::path selected;
    bool busy = false, closing = false, smoke = false, smokeStarted = false;
    fs::path smokeInput, smokeOutput;
    int smokeResult = 1;
    HFONT font{}, titleFont{}, smallFont{};
    HBRUSH background = CreateSolidBrush(RGB(244, 247, 250)), white = CreateSolidBrush(RGB(255, 255, 255));
    int dpi = 96;
    ~MainWindow() {
        cancelled = true;
        if (worker.joinable())
            worker.join();
        if (font)
            DeleteObject(font);
        if (titleFont)
            DeleteObject(titleFont);
        if (smallFont)
            DeleteObject(smallFont);
        DeleteObject(background);
        DeleteObject(white);
    }
    int S(int v) const {
        return MulDiv(v, dpi, 96);
    }
    HWND ControlH(int id) const {
        return GetDlgItem(window, id);
    }
    void Text(int id, const std::wstring& value) {
        SetWindowTextW(ControlH(id), value.c_str());
    }
    void Post(Event e) {
        auto p = std::make_unique<Event>(std::move(e));
        if (PostMessageW(window, EventMessage, 0, reinterpret_cast<LPARAM>(p.get())))
            p.release();
    }
    template <class F> void Start(F fn) {
        if (busy)
            return;
        if (worker.joinable())
            worker.join();
        busy = true;
        cancelled = false;
        Buttons();
        worker = std::jthread([this, fn = std::move(fn)] {
            try {
                fn();
            } catch (const std::exception& e) {
                logger.Write(std::string("ERROR: ") + e.what());
                Post(Event{Kind::Failure, {}, {}, {}, e.what()});
            } catch (...) {
                Post(Event{Kind::Failure, {}, {}, {}, "Unexpected error. See the local log."});
            }
        });
    }
    void Buttons() {
        for (int id : {Open, Manual, Install, ChooseOutput, OutputEdit})
            EnableWindow(ControlH(id), !busy);
        EnableWindow(ControlH(Cancel), busy);
        EnableWindow(ControlH(Convert), !busy && tools && source && source->UnsupportedReasons().empty());
        ShowWindow(ControlH(Install), tools ? SW_HIDE : SW_SHOW);
    }
    void Detect() {
        Text(StageText, L"正在检测 FFmpeg 并运行编码能力测试…");
        Start([this] {
            auto found = FFmpegManager(logger).Detect(cancelled);
            Event e{Kind::Detect};
            e.tools = std::move(found);
            Post(std::move(e));
        });
    }
    void ProbeInput(const fs::path& p) {
        if (busy)
            return;
        selected = p;
        source.reset();
        Text(DropZone, p.filename().wstring());
        Text(SourceText, L"正在读取视频参数…");
        Buttons();
        if (!tools) {
            Text(SourceText, L"请先安装或选择 FFmpeg，然后重新打开视频。");
            return;
        }
        Text(OutputEdit, (p.parent_path() / (p.stem().wstring() + L"_AppleLog.mov")).wstring());
        Start([this, p] {
            auto m = Probe(tools->ffprobe, p, &cancelled);
            logger.Write("Selected input: " + m.raw.dump());
            Event e{Kind::Probe};
            e.media = std::move(m);
            Post(std::move(e));
        });
    }
    void BeginConvert() {
        if (!source || !tools)
            return;
        auto path = fs::path(ControlText(ControlH(OutputEdit)));
        if (smoke)
            path = smokeOutput;
        Start([this, path] {
            TranscodeJob::Run(*tools, *source, path, logger, cancelled, [this](const JobProgress& p) {
                Event e{Kind::Progress};
                e.progress = p;
                Post(std::move(e));
            });
            Post(Event{Kind::Convert});
        });
    }
    HWND Add(int id, const wchar_t* klass, const wchar_t* text, DWORD style = 0) {
        HWND h = CreateWindowExW(klass == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, klass, text,
                                 WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, window,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr),
                                 nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return h;
    }
    void Create() {
        dpi = static_cast<int>(GetDpiForWindow(window));
        Fonts();
        Add(DropZone, L"STATIC", L"拖入一个 HLG ProRes 视频\r\n或使用右侧“打开文件”",
            SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX);
        Add(Open, L"BUTTON", L"打开文件…", WS_TABSTOP | BS_PUSHBUTTON);
        Add(SourceText, L"STATIC",
            L"等待输入\r\n支持 ProRes 422 / 422 HQ · 10-bit · BT.2020 · HLG\r\nV1 "
            L"仅接受经过时间戳检查的固定帧率素材。",
            SS_NOPREFIX);
        Add(OutputEdit, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL);
        Add(ChooseOutput, L"BUTTON", L"选择位置…", WS_TABSTOP);
        Add(StageText, L"STATIC", L"正在启动…", SS_NOPREFIX);
        Add(ProgressBar, PROGRESS_CLASSW, L"", PBS_SMOOTH);
        SetWindowTheme(ControlH(ProgressBar), L"", L"");
        SendMessageW(ControlH(ProgressBar), PBM_SETRANGE32, 0, 1000);
        SendMessageW(ControlH(ProgressBar), PBM_SETBARCOLOR, 0, RGB(0, 127, 126));
        Add(Convert, L"BUTTON", L"转换为 Apple Log", WS_TABSTOP | BS_DEFPUSHBUTTON);
        Add(Cancel, L"BUTTON", L"取消转换", WS_TABSTOP);
        Add(ToolText, L"STATIC", L"FFmpeg: 检测中…", SS_NOPREFIX);
        Add(Install, L"BUTTON", L"一键安装 FFmpeg", WS_TABSTOP);
        Add(Manual, L"BUTTON", L"手动选择 FFmpeg", WS_TABSTOP);
        Add(Logs, L"BUTTON", L"查看日志", WS_TABSTOP);
        DragAcceptFiles(window, TRUE);
        Layout();
        Buttons();
        Detect();
    }
    void Fonts() {
        if (font)
            DeleteObject(font);
        if (titleFont)
            DeleteObject(titleFont);
        if (smallFont)
            DeleteObject(smallFont);
        font =
            CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        titleFont = CreateFontW(-S(28), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                L"Segoe UI");
        smallFont =
            CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    }
    void Layout() {
        RECT r{};
        GetClientRect(window, &r);
        int w = MulDiv(r.right, 96, dpi);
        auto place = [&](int id, int x, int y, int cw, int ch) {
            MoveWindow(ControlH(id), S(x), S(y), S(cw), S(ch), TRUE);
        };
        place(DropZone, 44, 106, w - 258, 68);
        place(Open, w - 192, 120, 140, 40);
        place(SourceText, 44, 230, w - 88, 102);
        place(OutputEdit, 44, 461, w - 246, 34);
        place(ChooseOutput, w - 186, 460, 142, 36);
        place(ProgressBar, 44, 551, w - 88, 13);
        place(StageText, 44, 577, w - 385, 44);
        place(Convert, w - 326, 577, 180, 42);
        place(Cancel, w - 134, 577, 90, 42);
        place(ToolText, 32, 656, w - 460, 44);
        place(Install, w - 438, 654, 154, 36);
        place(Manual, w - 274, 654, 156, 36);
        place(Logs, w - 108, 654, 82, 36);
        InvalidateRect(window, nullptr, TRUE);
    }
    void Paint() {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(window, &ps);
        RECT client{};
        GetClientRect(window, &client);
        FillRect(dc, &client, background);
        SetBkMode(dc, TRANSPARENT);
        int w = MulDiv(client.right, 96, dpi);
        auto label = [&](const wchar_t* t, int x, int y, int cw, int ch, HFONT f, COLORREF color) {
            SelectObject(dc, f);
            SetTextColor(dc, color);
            RECT r{S(x), S(y), S(x + cw), S(y + ch)};
            DrawTextW(dc, t, -1, &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        };
        label(L"LogForge", 28, 19, 210, 40, titleFont, RGB(18, 40, 56));
        label(L"HLG → Apple Log  /  0.1.0", 240, 32, 480, 30, font, RGB(80, 99, 113));
        auto card = [&](int y, int h) {
            HGDIOBJ oldBrush = SelectObject(dc, white);
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(220, 228, 234));
            HGDIOBJ oldPen = SelectObject(dc, pen);
            RoundRect(dc, S(24), S(y), S(w - 24), S(y + h), S(12), S(12));
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(pen);
        };
        card(84, 110);
        card(210, 135);
        card(361, 150);
        card(527, 108);
        label(L"SOURCE COLOR", 44, 211, 250, 22, smallFont, RGB(0, 116, 114));
        label(L"OUTPUT", 44, 373, 160, 22, smallFont, RGB(0, 116, 114));
        label(L"Apple Log  ·  BT.2020  ·  ProRes 422 HQ  ·  10-bit 4:2:2  ·  MOV", 44, 396, w - 88, 24, font,
              RGB(18, 40, 56));
        label(L"曝光约定：75% HLG → 90% 反射率。调色软件中请手动指定 Apple Log / Rec.2020。", 44, 427, w - 88,
              27, smallFont, RGB(85, 101, 114));
        label(L"重新编码已有信号；不会恢复过曝、死黑或 ISP 已丢失的信息。所有处理均在本机完成。", 32, 713,
              w - 64, 30, smallFont, RGB(87, 102, 115));
        EndPaint(window, &ps);
    }
    void FinishSmoke(bool passed, const std::string& detail) {
        smokeResult = passed ? 0 : 1;
        auto png = smokeOutput;
        png += L".png";
        const bool captured = SaveWindowSnapshot(window, png);
        Json result{{"passed", passed},
                    {"detail", detail},
                    {"window_created", IsWindow(window) != FALSE},
                    {"drop_input_loaded", source.has_value()},
                    {"snapshot_saved", captured},
                    {"convert_enabled", IsWindowEnabled(ControlH(Convert)) != FALSE},
                    {"cancel_enabled", IsWindowEnabled(ControlH(Cancel)) != FALSE},
                    {"output", PathText(smokeOutput)}};
        auto p = smokeOutput;
        p += L".gui-test.json";
        std::ofstream f(p);
        f << result.dump(2);
        f.close();
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
    void OnEvent(std::unique_ptr<Event> e) {
        if (e->kind == Kind::Progress) {
            auto p = e->progress;
            std::wostringstream s;
            s << p.stage;
            if (p.fraction >= 0) {
                SendMessageW(ControlH(ProgressBar), PBM_SETPOS, static_cast<WPARAM>(p.fraction * 1000), 0);
                s << L"  " << std::fixed << std::setprecision(1) << p.fraction * 100 << L"%";
                if (p.frame)
                    s << L" · " << p.frame << L" frames";
                if (p.speed > 0 && p.seconds > 2 && source) {
                    double remaining = (source->videoDuration - p.seconds) / p.speed;
                    if (remaining > 0 && remaining < 86400)
                        s << L" · 约 " << static_cast<int>(remaining) << L" s";
                }
            }
            Text(StageText, s.str());
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
            Text(StageText, cancelled ? L"已取消" : L"操作失败 · 查看详细日志");
            Buttons();
            if (smoke) {
                FinishSmoke(false, e->error);
                return;
            }
            if (!cancelled) {
                auto msg = Wide(e->error) + L"\r\n\r\n点击“查看日志”可查看详细信息。";
                MessageBoxW(window, msg.c_str(), L"LogForge", MB_OK | MB_ICONWARNING);
            }
            return;
        }
        if (e->kind == Kind::Detect || e->kind == Kind::Install) {
            tools = std::move(e->tools);
            Text(ToolText, tools ? L"FFmpeg 已就绪\r\nProRes HQ · 10-bit · 浮点处理已验证"
                                 : L"未检测到 FFmpeg\r\n可一键安装（约 105 MB，GPLv3）");
            Text(StageText, tools ? L"就绪 · 请选择 HLG ProRes 视频" : L"先安装或手动选择 FFmpeg");
            Buttons();
            if (smoke && !tools) {
                FinishSmoke(false, "FFmpeg not available for GUI smoke test.");
                return;
            }
            if (smoke && tools) {
                auto name = smokeInput.wstring();
                HGLOBAL block = GlobalAlloc(GHND, sizeof(DROPFILES) + (name.size() + 2) * sizeof(wchar_t));
                if (!block)
                    throw std::runtime_error("Cannot create GUI drag-drop test payload.");
                auto drop = static_cast<DROPFILES*>(GlobalLock(block));
                drop->pFiles = sizeof(DROPFILES);
                drop->fWide = TRUE;
                memcpy(reinterpret_cast<unsigned char*>(drop) + sizeof(DROPFILES), name.c_str(),
                       (name.size() + 1) * sizeof(wchar_t));
                GlobalUnlock(block);
                SendMessageW(window, WM_DROPFILES, reinterpret_cast<WPARAM>(block), 0);
                return;
            }
            if (!selected.empty() && tools)
                ProbeInput(selected);
            return;
        }
        if (e->kind == Kind::Probe) {
            source = std::move(e->media);
            Text(SourceText, source->Summary());
            auto errors = source->UnsupportedReasons();
            Text(StageText,
                 errors.empty() ? L"素材参数符合要求 · 转换前将检查完整时间戳" : Wide(errors.front()));
            Buttons();
            if (smoke && !smokeStarted) {
                smokeStarted = true;
                Text(OutputEdit, smokeOutput.wstring());
                if (errors.empty())
                    BeginConvert();
                else
                    FinishSmoke(false, errors.front());
            }
            return;
        }
        if (e->kind == Kind::Convert) {
            Text(StageText, L"完成 · 格式、音频和时长验证通过\r\n请在编辑器中指定 Apple Log / Rec.2020");
            SendMessageW(ControlH(ProgressBar), PBM_SETPOS, 1000, 0);
            Buttons();
            if (smoke) {
                // Let native controls finish their visual state transition before the QA snapshot.
                SetTimer(window, 9001, 250, nullptr);
            }
            return;
        }
    }
    void Command(int id) {
        if (id == Logs) {
            ShellExecuteW(window, L"open", logger.Path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }
        if (id == Cancel) {
            cancelled = true;
            Text(StageText, L"正在取消并关闭子进程…");
            return;
        }
        if (busy)
            return;
        if (id == Open) {
            if (auto p = SelectFile(window, false, false))
                ProbeInput(*p);
        } else if (id == ChooseOutput) {
            if (auto p = SelectFile(window, true, false, fs::path(ControlText(ControlH(OutputEdit)))))
                Text(OutputEdit, p->wstring());
        } else if (id == Convert)
            BeginConvert();
        else if (id == Manual) {
            if (auto p = SelectFile(window, false, true)) {
                Start([this, p = *p] {
                    FFmpegManager m(logger);
                    auto checked = m.Check(p, cancelled);
                    m.SaveManual(p);
                    Event e{Kind::Detect};
                    e.tools = std::move(checked);
                    Post(std::move(e));
                });
            }
        } else if (id == Install) {
            Start([this] {
                GyanReleaseProvider provider;
                auto installed = FFmpegDownloader::Install(
                    provider, logger, cancelled,
                    [this, last = uint64_t{0}](uint64_t n, uint64_t total,
                                               const std::wstring& phase) mutable {
                        if (total && n != total && n > last && n - last < 262144)
                            return;
                        last = n;
                        Event e{Kind::Progress};
                        e.progress.stage = phase;
                        e.progress.fraction =
                            total ? static_cast<double>(n) / static_cast<double>(total) : -1;
                        Post(std::move(e));
                    });
                Event e{Kind::Install};
                e.tools = std::move(installed);
                Post(std::move(e));
            });
        }
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(l);
            self = static_cast<MainWindow*>(cs->lpCreateParams);
            self->window = h;
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self)
            return DefWindowProcW(h, msg, w, l);
        try {
            switch (msg) {
            case WM_CREATE:
                self->Create();
                return 0;
            case WM_PAINT:
                self->Paint();
                return 0;
            case WM_SIZE:
                self->Layout();
                return 0;
            case WM_GETMINMAXINFO: {
                auto m = reinterpret_cast<MINMAXINFO*>(l);
                m->ptMinTrackSize = {self->S(900), self->S(805)};
                return 0;
            }
            case WM_DPICHANGED: {
                self->dpi = HIWORD(w);
                self->Fonts();
                EnumChildWindows(
                    h,
                    [](HWND child, LPARAM f) -> BOOL {
                        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(f), TRUE);
                        return TRUE;
                    },
                    reinterpret_cast<LPARAM>(self->font));
                auto r = reinterpret_cast<RECT*>(l);
                SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                self->Layout();
                return 0;
            }
            case WM_CTLCOLORSTATIC: {
                auto dc = reinterpret_cast<HDC>(w);
                SetTextColor(dc, RGB(33, 51, 66));
                int id = GetDlgCtrlID(reinterpret_cast<HWND>(l));
                bool card = id == DropZone || id == SourceText || id == StageText;
                SetBkColor(dc, card ? RGB(255, 255, 255) : RGB(244, 247, 250));
                return reinterpret_cast<LRESULT>(card ? self->white : self->background);
            }
            case WM_COMMAND:
                if (HIWORD(w) == BN_CLICKED)
                    self->Command(LOWORD(w));
                return 0;
            case WM_TIMER:
                if (w == 9001 && self->smoke) {
                    KillTimer(h, 9001);
                    self->FinishSmoke(
                        true, "Native window, drag-drop, asynchronous probe and conversion completed.");
                }
                return 0;
            case WM_DROPFILES: {
                auto drop = reinterpret_cast<HDROP>(w);
                UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                std::wstring p;
                if (count == 1) {
                    UINT n = DragQueryFileW(drop, 0, nullptr, 0);
                    p.resize(n + 1);
                    DragQueryFileW(drop, 0, p.data(), n + 1);
                    p.resize(n);
                }
                DragFinish(drop);
                if (count == 1 && !self->busy)
                    self->ProbeInput(p);
                else if (count > 1)
                    MessageBoxW(h, L"V1 每次处理一个视频。", L"LogForge", MB_OK);
                return 0;
            }
            case EventMessage:
                self->OnEvent(std::unique_ptr<Event>(reinterpret_cast<Event*>(l)));
                return 0;
            case WM_CLOSE:
                if (self->busy) {
                    self->closing = true;
                    self->cancelled = true;
                    self->Text(StageText, L"正在停止后台任务…");
                    return 0;
                }
                DestroyWindow(h);
                return 0;
            case WM_DESTROY:
                PostQuitMessage(self->smoke ? self->smokeResult : 0);
                return 0;
            }
        } catch (const std::exception& e) {
            if (self->smoke)
                self->FinishSmoke(false, e.what());
            else
                MessageBoxW(h, Wide(e.what()).c_str(), L"LogForge", MB_OK | MB_ICONERROR);
        }
        return DefWindowProcW(h, msg, w, l);
    }
};
int RunGui(HINSTANCE instance, const std::vector<std::wstring>& args) {
    MainWindow app;
    if (args.size() == 3 && args[0] == L"--smoke-test") {
        app.smoke = true;
        app.smokeInput = args[1];
        app.smokeOutput = args[2];
    }
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWindow::Proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = L"LogForge.MainWindow";
    RegisterClassExW(&wc);
    int dpi = static_cast<int>(GetDpiForSystem());
    HWND window = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, L"LogForge · HLG → Apple Log",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(1000, dpi, 96),
                                  MulDiv(825, dpi, 96), nullptr, nullptr, instance, &app);
    if (!window)
        throw std::runtime_error("Cannot create LogForge window.");
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    // Drain any progress messages queued during shutdown without leaking payloads.
    const int exitCode = static_cast<int>(msg.wParam);
    while (PeekMessageW(&msg, window, EventMessage, EventMessage, PM_REMOVE))
        delete reinterpret_cast<Event*>(msg.lParam);
    return exitCode;
}
} // namespace logforge
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&controls);
    int result = 1;
    try {
        int count{};
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &count);
        std::vector<std::wstring> args;
        for (int i = 1; i < count; ++i)
            args.emplace_back(argv[i]);
        LocalFree(argv);
        result = logforge::RunGui(instance, args);
    } catch (const std::exception& e) {
        MessageBoxW(nullptr, logforge::Wide(e.what()).c_str(), L"LogForge 启动失败", MB_OK | MB_ICONERROR);
    }
    CoUninitialize();
    return result;
}
