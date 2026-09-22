#include "logforge/Ui.h"
#include <algorithm>
#include <array>
#include <dwmapi.h>
#include <iomanip>
#include <sstream>
#include <uxtheme.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace logforge {
namespace {
void DeleteStyle(UiStyle& s) {
    for (auto object :
         {static_cast<HGDIOBJ>(s.background), static_cast<HGDIOBJ>(s.surface), static_cast<HGDIOBJ>(s.normal),
          static_cast<HGDIOBJ>(s.compact), static_cast<HGDIOBJ>(s.title)})
        if (object)
            DeleteObject(object);
    s.background = s.surface = nullptr;
    s.normal = s.compact = s.title = nullptr;
}
LRESULT CALLBACK ComboProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR context) {
    const auto result = DefSubclassProc(h, m, w, l);
    if (m == WM_PAINT) {
        const auto& style = *reinterpret_cast<UiStyle*>(context);
        COMBOBOXINFO info{sizeof(info)};
        if (GetComboBoxInfo(h, &info)) {
            HDC dc = GetDC(h);
            FillRect(dc, &info.rcButton, style.surface);
            const auto x = (info.rcButton.left + info.rcButton.right) / 2;
            const auto y = (info.rcButton.top + info.rcButton.bottom) / 2;
            HPEN pen = CreatePen(PS_SOLID, style.Scale(2),
                                 IsWindowEnabled(h) ? style.colors.text : style.colors.muted);
            auto old = SelectObject(dc, pen);
            MoveToEx(dc, x - style.Scale(4), y - style.Scale(2), nullptr);
            LineTo(dc, x, y + style.Scale(2));
            LineTo(dc, x + style.Scale(4), y - style.Scale(2));
            SelectObject(dc, old);
            DeleteObject(pen);
            ReleaseDC(h, dc);
        }
    }
    return result;
}
} // namespace
UiStyle::~UiStyle() {
    DeleteStyle(*this);
}
void UiStyle::Apply(Theme next, Language language, int dpi) {
    DeleteStyle(*this);
    theme = next;
    dpi_ = dpi;
    colors = theme == Theme::Dark ? Palette{RGB(17, 21, 27),    RGB(25, 32, 42), RGB(235, 241, 248),
                                            RGB(166, 182, 199), RGB(49, 63, 80), RGB(83, 216, 221)}
                                  : Palette{RGB(243, 247, 250), RGB(255, 255, 255), RGB(20, 39, 55),
                                            RGB(76, 95, 113),   RGB(194, 209, 219), RGB(0, 119, 130)};
    HIGHCONTRASTW contrast{sizeof(contrast)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON))
        colors = {GetSysColor(COLOR_WINDOW),     GetSysColor(COLOR_WINDOW),     GetSysColor(COLOR_WINDOWTEXT),
                  GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_HIGHLIGHT)};
    background = CreateSolidBrush(colors.background);
    surface = CreateSolidBrush(colors.surface);
    const auto family = language == Language::SimplifiedChinese ? L"Microsoft YaHei UI" : L"Segoe UI";
    const auto font = [&](int size, int weight) {
        return CreateFontW(-Scale(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, family);
    };
    normal = font(15, FW_NORMAL);
    compact = font(12, FW_NORMAL);
    title = font(26, FW_SEMIBOLD);
}
void UiStyle::Window(HWND window) const {
    BOOL dark = theme == Theme::Dark;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DwmSetWindowAttribute(window, DWMWA_CAPTION_COLOR, &colors.background, sizeof(COLORREF));
    DwmSetWindowAttribute(window, DWMWA_TEXT_COLOR, &colors.text, sizeof(COLORREF));
    const auto module = GetModuleHandleW(nullptr);
    for (auto [which, size] : {std::pair{ICON_BIG, 48}, std::pair{ICON_SMALL, 16}})
        SendMessageW(window, WM_SETICON, which,
                     reinterpret_cast<LPARAM>(LoadImageW(module, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                                         Scale(size), Scale(size), LR_SHARED)));
    InvalidateRect(window, nullptr, TRUE);
}
void UiStyle::Control(HWND control, bool useSmall) const {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(useSmall ? compact : normal), TRUE);
    wchar_t name[32]{};
    GetClassNameW(control, name, 32);
    if (_wcsicmp(name, L"COMBOBOX") == 0) {
        SetWindowTheme(control, L"", L"");
        SendMessageW(control, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), Scale(25));
        SendMessageW(control, CB_SETITEMHEIGHT, 0, Scale(25));
        SetWindowSubclass(control, ComboProc, 1, reinterpret_cast<DWORD_PTR>(this));
    }
    if (_wcsicmp(name, PROGRESS_CLASSW) == 0) {
        SetWindowTheme(control, L"", L"");
        SendMessageW(control, PBM_SETBKCOLOR, 0, colors.surface);
        SendMessageW(control, PBM_SETBARCOLOR, 0, colors.accent);
    }
    InvalidateRect(control, nullptr, TRUE);
}
void UiStyle::Text(HDC dc, const std::wstring& text, RECT rect, bool muted, bool useSmall, UINT flags) const {
    auto old = SelectObject(dc, useSmall ? compact : normal);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, muted ? colors.muted : colors.text);
    DrawTextW(dc, text.c_str(), -1, &rect, flags);
    SelectObject(dc, old);
}
void UiStyle::Card(HDC dc, RECT rect) const {
    HPEN pen = CreatePen(PS_SOLID, 1, colors.border);
    auto oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, surface);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, Scale(12), Scale(12));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}
LRESULT UiStyle::Color(UINT message, WPARAM w, LPARAM l, bool onSurface) const {
    const auto dc = reinterpret_cast<HDC>(w);
    const bool field = message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX;
    const bool muted = GetPropW(reinterpret_cast<HWND>(l), L"compact") != nullptr;
    SetTextColor(dc, muted ? colors.muted : colors.text);
    SetBkColor(dc, field || onSurface ? colors.surface : colors.background);
    return reinterpret_cast<LRESULT>(field || onSurface ? surface : background);
}
void UiStyle::DrawItem(const DRAWITEMSTRUCT& item) const {
    auto rect = item.rcItem;
    if (item.CtlType == ODT_COMBOBOX) {
        const bool selected = (item.itemState & ODS_SELECTED) && !(item.itemState & ODS_COMBOBOXEDIT);
        HBRUSH fill = CreateSolidBrush(selected ? colors.accent : colors.surface);
        FillRect(item.hDC, &rect, fill);
        DeleteObject(fill);
        auto index = item.itemID == static_cast<UINT>(-1) ? SendMessageW(item.hwndItem, CB_GETCURSEL, 0, 0)
                                                          : item.itemID;
        if (index != CB_ERR) {
            const auto length = SendMessageW(item.hwndItem, CB_GETLBTEXTLEN, index, 0);
            if (length >= 0 && length < 4096) {
                std::wstring text(static_cast<size_t>(length) + 1, 0);
                SendMessageW(item.hwndItem, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(text.data()));
                rect.left += Scale(9);
                rect.right -= Scale(4);
                auto old = SelectObject(item.hDC, normal);
                SetBkMode(item.hDC, TRANSPARENT);
                SetTextColor(item.hDC, selected
                                           ? (theme == Theme::Dark ? RGB(10, 28, 35) : RGB(255, 255, 255))
                                           : colors.text);
                DrawTextW(item.hDC, text.c_str(), -1, &rect,
                          DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
                SelectObject(item.hDC, old);
            }
        }
        return;
    }
    const bool disabled = item.itemState & ODS_DISABLED;
    const bool primary = GetPropW(item.hwndItem, L"primary") || GetPropW(item.hwndItem, L"checked");
    const auto fillColor = primary && !disabled ? colors.accent : colors.surface;
    HBRUSH fill = CreateSolidBrush(fillColor);
    HPEN pen = CreatePen(PS_SOLID, 1, item.itemState & ODS_FOCUS ? colors.accent : colors.border);
    auto oldPen = SelectObject(item.hDC, pen), oldBrush = SelectObject(item.hDC, fill);
    RoundRect(item.hDC, rect.left, rect.top, rect.right, rect.bottom, Scale(8), Scale(8));
    SelectObject(item.hDC, oldBrush);
    SelectObject(item.hDC, oldPen);
    DeleteObject(pen);
    DeleteObject(fill);
    InflateRect(&rect, -Scale(6), 0);
    auto old = SelectObject(item.hDC, normal);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, disabled  ? colors.muted
                           : primary ? (theme == Theme::Dark ? RGB(10, 28, 35) : RGB(255, 255, 255))
                                     : colors.text);
    if (item.itemState & ODS_SELECTED)
        OffsetRect(&rect, 1, 1);
    const auto text = WindowText(item.hwndItem);
    DrawTextW(item.hDC, text.c_str(), -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        InflateRect(&rect, -Scale(3), -Scale(5));
        DrawFocusRect(item.hDC, &rect);
    }
    SelectObject(item.hDC, old);
}
std::wstring WindowText(HWND h) {
    std::wstring text(static_cast<size_t>(GetWindowTextLengthW(h)) + 1, 0);
    GetWindowTextW(h, text.data(), static_cast<int>(text.size()));
    text.resize(wcslen(text.c_str()));
    return text;
}
HWND MakeControl(HWND parent, int id, const wchar_t* klass, const std::wstring& text, DWORD flags,
                 const UiStyle& style, bool compact) {
    auto h = CreateWindowExW(0, klass, text.c_str(), WS_CHILD | WS_VISIBLE | flags, 0, 0, 10, 10, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr),
                             nullptr);
    if (!h)
        throw AppError(Message(TextId::Unexpected, {"Cannot create a Windows control."}));
    if (compact)
        SetPropW(h, L"compact", reinterpret_cast<HANDLE>(1));
    style.Control(h, compact);
    return h;
}
void SetCombo(HWND combo, const std::vector<std::wstring>& choices, int selected) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const auto& text : choices)
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(combo, CB_SETCURSEL, selected, 0);
}
bool SaveWindowSnapshot(HWND window, const fs::path& destination) {
    using Microsoft::WRL::ComPtr;
    RECT rect{};
    GetClientRect(window, &rect);
    HDC screen = GetDC(window);
    if (!screen)
        return false;
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, rect.right, rect.bottom);
    auto old = SelectObject(dc, bitmap);
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    DwmFlush();
    BOOL painted = PrintWindow(window, dc, 3);
    SelectObject(dc, old);
    DeleteDC(dc);
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
} // namespace logforge
