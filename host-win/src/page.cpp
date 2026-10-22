// String and UI helpers, and the Page base class
#include "app.h"

#include <shlwapi.h>

#include <algorithm>
#include <ctime>

std::wstring Widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

std::string Narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
    return result;
}

std::wstring JsonText(const json& value, const char* key) {
    return Widen(JsonString(value, key));
}

std::string JsonString(const json& value, const char* key) {
    auto found = value.find(key);
    return found != value.end() && found->is_string() ? found->get<std::string>() : std::string();
}

int Scale(int pixels) {
    static int dpi = [] {
        HDC screen = GetDC(nullptr);
        int value = GetDeviceCaps(screen, LOGPIXELSY);
        ReleaseDC(nullptr, screen);
        return value;
    }();
    return MulDiv(pixels, dpi, 96);
}

HFONT MakeFont(const wchar_t* face, int points, int weight) {
    HDC screen = GetDC(nullptr);
    int height = -MulDiv(points, GetDeviceCaps(screen, LOGPIXELSY), 72);
    ReleaseDC(nullptr, screen);
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

HFONT UiFont() {
    static HFONT font = MakeFont(L"Tahoma", 8, FW_NORMAL);
    return font;
}

HFONT BoldFont() {
    static HFONT font = MakeFont(L"Tahoma", 8, FW_BOLD);
    return font;
}

HFONT MonoFont() {
    static HFONT font = MakeFont(L"Courier New", 9, FW_NORMAL);
    return font;
}

std::wstring FormatTime(const std::string& iso, bool withSeconds) {
    // 2026-10-22T11:05:00.000Z
    int year, month, day, hour, minute, second;
    if (sscanf_s(iso.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) {
        return Widen(iso);
    }
    std::tm utc = {};
    utc.tm_year = year - 1900;
    utc.tm_mon = month - 1;
    utc.tm_mday = day;
    utc.tm_hour = hour;
    utc.tm_min = minute;
    utc.tm_sec = second;
    std::time_t stamp = _mkgmtime(&utc);
    std::tm local = {};
    localtime_s(&local, &stamp);
    wchar_t buffer[64];
    if (withSeconds) {
        wcsftime(buffer, 64, L"%H:%M:%S", &local);
    } else {
        wcsftime(buffer, 64, L"%d.%m.%Y %H:%M", &local);
    }
    return buffer;
}

std::wstring FormatSize(double bytes) {
    wchar_t buffer[32];
    if (bytes < 1024) {
        swprintf_s(buffer, L"%.0f B", bytes);
    } else if (bytes < 1024 * 1024) {
        swprintf_s(buffer, L"%.0f KB", bytes / 1024 + 0.5);
    } else {
        swprintf_s(buffer, L"%.1f MB", bytes / 1024 / 1024);
    }
    return buffer;
}

std::wstring GetWindowString(HWND hwnd) {
    int length = GetWindowTextLengthW(hwnd);
    std::wstring text(length + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(length);
    return text;
}

std::wstring ExeDirectory() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    PathRemoveFileSpecW(path);
    return path;
}

void RegisterPlainClass(const wchar_t* name, WNDPROC proc) {
    WNDCLASSW wc = {};
    if (GetClassInfoW(GetModuleHandleW(nullptr), name, &wc)) {
        return;
    }
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = name;
    RegisterClassW(&wc);
}

HWND CreateChild(HWND parent, const wchar_t* className, const wchar_t* text, DWORD style, int id, RECT rect, DWORD exStyle) {
    HWND control = CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | style, rect.left, rect.top, rect.right - rect.left,
                                   rect.bottom - rect.top, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(UiFont()), TRUE);
    return control;
}

LRESULT ButtonFaceColor(HDC dc) {
    SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
    return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
}

void AddColumns(HWND list, std::initializer_list<Column> columns) {
    int index = Header_GetItemCount(ListView_GetHeader(list));
    for (const Column& entry : columns) {
        LVCOLUMNW column = {};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        column.fmt = entry.format;
        column.pszText = const_cast<wchar_t*>(entry.text);
        column.cx = Scale(entry.width);
        ListView_InsertColumn(list, index++, &column);
    }
}

void TrackContextMenu(HMENU menu, HWND owner, HWND control, POINT at, RECT anchor) {
    if (at.x == -1 && at.y == -1) {
        at = {anchor.left, anchor.bottom};
        ClientToScreen(control, &at);
    }
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, at.x, at.y, 0, owner, nullptr);
    // destroys the submenus too
    DestroyMenu(menu);
}

namespace {

struct EmptyText {
    WNDPROC proc;
    std::wstring text;
};
std::map<HWND, EmptyText> g_emptyTexts;

LRESULT CALLBACK EmptyTextProc(HWND list, UINT message, WPARAM wParam, LPARAM lParam) {
    auto found = g_emptyTexts.find(list);
    if (found == g_emptyTexts.end()) {
        return DefWindowProcW(list, message, wParam, lParam);
    }
    WNDPROC proc = found->second.proc;
    if (message == WM_NCDESTROY) {
        g_emptyTexts.erase(found);
        return CallWindowProcW(proc, list, message, wParam, lParam);
    }
    LRESULT result = CallWindowProcW(proc, list, message, wParam, lParam);
    if (message == WM_PAINT && ListView_GetItemCount(list) == 0 && !found->second.text.empty()) {
        RECT area;
        GetClientRect(list, &area);
        RECT header = {};
        if (HWND headerControl = ListView_GetHeader(list)) {
            GetWindowRect(headerControl, &header);
        }
        area.top += (header.bottom - header.top) + Scale(16);
        InflateRect(&area, -Scale(16), 0);
        HDC dc = GetDC(list);
        HGDIOBJ old = SelectObject(dc, UiFont());
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
        DrawTextW(dc, found->second.text.c_str(), -1, &area, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
        ReleaseDC(list, dc);
    }
    return result;
}

}  // namespace

void SetEmptyText(HWND list, const std::wstring& text) {
    auto found = g_emptyTexts.find(list);
    if (found == g_emptyTexts.end()) {
        WNDPROC proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(list, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EmptyTextProc)));
        g_emptyTexts[list] = {proc, text};
    } else if (found->second.text != text) {
        found->second.text = text;
    } else {
        return;
    }
    InvalidateRect(list, nullptr, TRUE);
}

void StretchColumn(HWND list, int column, int minimum) {
    RECT client;
    GetClientRect(list, &client);
    int others = 0;
    int count = Header_GetItemCount(ListView_GetHeader(list));
    for (int i = 0; i < count; i++) {
        if (i != column) {
            others += ListView_GetColumnWidth(list, i);
        }
    }
    ListView_SetColumnWidth(list, column, std::max(Scale(minimum), static_cast<int>(client.right) - others));
}

void DrawEtchedFrame(HDC dc, RECT rect) {
    DrawEdge(dc, &rect, EDGE_ETCHED, BF_RECT);
}

Sidecar::Callback ErrorReporter(HWND owner, std::wstring action) {
    return [owner, action](const json&, const std::string& error) {
        if (!error.empty()) {
            MessageBoxW(owner, Widen(error).c_str(), (action + L" failed").c_str(), MB_ICONERROR);
        }
    };
}

// ---- Page ----

static const wchar_t* kPageClass = L"ImapKitDesktopPage";

HWND Page::Create(HWND parent) {
    RegisterPlainClass(kPageClass, WndProc);
    return CreateWindowExW(WS_EX_CONTROLPARENT, kPageClass, L"", WS_CHILD | (ClipChildren() ? WS_CLIPCHILDREN : 0), 0, 0, 0, 0, parent, nullptr,
                           GetModuleHandleW(nullptr), this);
}

HWND Page::AddControl(const wchar_t* className, const wchar_t* text, DWORD style, int id, DWORD exStyle) {
    return CreateChild(hwnd_, className, text, style, id, {}, exStyle);
}

bool Page::HandleSplitter(UINT message, WPARAM wParam, LPARAM lParam) {
    POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    auto hit = [&](POINT at) {
        for (size_t i = 0; i < splitters_.size(); i++) {
            if (PtInRect(&splitters_[i].area, at)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    switch (message) {
        case WM_SETCURSOR: {
            if (reinterpret_cast<HWND>(wParam) != hwnd_ || LOWORD(lParam) != HTCLIENT) {
                return false;
            }
            POINT cursor;
            GetCursorPos(&cursor);
            ScreenToClient(hwnd_, &cursor);
            int index = dragging_ >= 0 ? dragging_ : hit(cursor);
            if (index < 0) {
                return false;
            }
            SetCursor(LoadCursorW(nullptr, splitters_[index].vertical ? IDC_SIZEWE : IDC_SIZENS));
            return true;
        }
        case WM_LBUTTONDOWN:
            dragging_ = hit(point);
            if (dragging_ >= 0) {
                SetCapture(hwnd_);
            }
            return dragging_ >= 0;
        case WM_MOUSEMOVE:
            if (dragging_ < 0) {
                return false;
            }
            splitters_[dragging_].drag(splitters_[dragging_].vertical ? point.x : point.y);
            {
                RECT client;
                GetClientRect(hwnd_, &client);
                OnSize(client.right, client.bottom);
            }
            return true;
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
            if (dragging_ < 0) {
                return false;
            }
            dragging_ = -1;
            if (message == WM_LBUTTONUP) {
                ReleaseCapture();
            }
            return true;
    }
    return false;
}

LRESULT CALLBACK Page::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    Page* page = reinterpret_cast<Page*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        page = static_cast<Page*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        page->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(page));
    }
    if (!page) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    switch (message) {
        case WM_CREATE:
            page->OnCreate();
            return 0;
        case WM_SIZE:
            page->OnSize(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_COMMAND:
            page->OnCommand(LOWORD(wParam), HIWORD(wParam), reinterpret_cast<HWND>(lParam));
            return 0;
        case WM_NOTIFY:
            return page->OnNotify(reinterpret_cast<NMHDR*>(lParam));
        case WM_SETCURSOR:
        case WM_LBUTTONDOWN:
        case WM_MOUSEMOVE:
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
            if (page->HandleSplitter(message, wParam, lParam)) {
                return message == WM_SETCURSOR ? TRUE : 0;
            }
            [[fallthrough]];
        default: {
            bool handled = true;
            LRESULT result = page->OnMessage(message, wParam, lParam, handled);
            if (handled) {
                return result;
            }
            if (message == WM_CTLCOLORSTATIC) {
                // labels on the grey of the page, not on the window color
                return ButtonFaceColor(reinterpret_cast<HDC>(wParam));
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }
}
