// ImapKit Desktop: the main window with the Server, Connections and Messages pages
#include "app.h"

#include <objbase.h>

#define WIDEN2(text) L##text
#define WIDEN(text) WIDEN2(text)
#define APP_VERSION_W WIDEN(APP_VERSION)

namespace {

enum MenuId {
    IDM_EXIT = 100,
    IDM_VIEW_SERVER,
    IDM_VIEW_CONNECTIONS,
    IDM_VIEW_MESSAGES,
    IDM_ABOUT,
};

const wchar_t* kMainClass = L"ImapKitDesktopMain";
const wchar_t* kTitle = L"ImapKit Desktop";

HWND g_main = nullptr;
HWND g_tabs = nullptr;
HWND g_status = nullptr;
Page* g_pages[3] = {};
int g_current = -1;

void ShowPage(int index) {
    if (index == g_current) {
        return;
    }
    for (int i = 0; i < 3; i++) {
        ShowWindow(g_pages[i]->hwnd(), i == index ? SW_SHOW : SW_HIDE);
    }
    TabCtrl_SetCurSel(g_tabs, index);
    g_current = index;
    g_pages[index]->OnShow();
}

void Layout() {
    RECT client;
    GetClientRect(g_main, &client);
    SendMessageW(g_status, WM_SIZE, 0, 0);
    RECT statusRect;
    GetWindowRect(g_status, &statusRect);
    int statusHeight = statusRect.bottom - statusRect.top;

    int margin = Scale(4);
    int width = client.right - 2 * margin;
    int height = client.bottom - statusHeight - 2 * margin;
    MoveWindow(g_tabs, margin, margin, width, height, TRUE);

    RECT display = {margin, margin, margin + width, margin + height};
    TabCtrl_AdjustRect(g_tabs, FALSE, &display);
    for (Page* page : g_pages) {
        MoveWindow(page->hwnd(), display.left, display.top, display.right - display.left, display.bottom - display.top, TRUE);
    }

    int parts[3] = {Scale(330), Scale(480), -1};
    SendMessageW(g_status, SB_SETPARTS, 3, reinterpret_cast<LPARAM>(parts));
}

HMENU BuildMenu() {
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDM_EXIT, L"E&xit");
    HMENU view = CreatePopupMenu();
    AppendMenuW(view, MF_STRING, IDM_VIEW_SERVER, L"&Server\tCtrl+1");
    AppendMenuW(view, MF_STRING, IDM_VIEW_CONNECTIONS, L"&Connections\tCtrl+2");
    AppendMenuW(view, MF_STRING, IDM_VIEW_MESSAGES, L"&Messages\tCtrl+3");
    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, IDM_ABOUT, L"&About ImapKit Desktop");
    HMENU bar = CreateMenu();
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&View");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"&Help");
    return bar;
}

void OnCreate(HWND hwnd) {
    g_main = hwnd;
    g_status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hwnd, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g_status, WM_SETFONT, reinterpret_cast<WPARAM>(UiFont()), TRUE);

    g_tabs = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, 0, 0, 0, 0, hwnd,
                             nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g_tabs, WM_SETFONT, reinterpret_cast<WPARAM>(UiFont()), TRUE);
    const wchar_t* names[3] = {L"Server", L"Connections", L"Messages"};
    for (int i = 0; i < 3; i++) {
        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(names[i]);
        TabCtrl_InsertItem(g_tabs, i, &item);
    }

    g_pages[0] = CreateServerPage();
    g_pages[1] = CreateConnectionsPage();
    g_pages[2] = CreateMessagesPage();
    for (Page* page : g_pages) {
        page->Create(hwnd);
        // above the tab control, which draws the frame around them
        SetWindowPos(page->hwnd(), HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }

    TheSidecar().onEvent = [](const std::string& name, const json& data) {
        if (name == "status") {
            if (data.value("running", false)) {
                wchar_t text[200];
                swprintf_s(text, L"Running: IMAP %d, IMAPS %d, SMTP %d", data.value("imapPort", 0), data.value("imapsPort", 0),
                           data.value("smtpPort", 0));
                SetStatus(STATUS_SERVER, text);
            } else {
                std::wstring error = JsonText(data, "error");
                SetStatus(STATUS_SERVER, error.empty() ? L"Stopped" : L"Stopped: " + error);
            }
        } else if (name == "log" && data.value("level", "") == "error") {
            SetStatus(STATUS_INFO, JsonText(data, "message"));
        }
        for (Page* page : g_pages) {
            page->OnEvent(name, data);
        }
    };

    std::wstring error;
    if (!TheSidecar().Start(hwnd, error)) {
        MessageBoxW(hwnd, error.c_str(), kTitle, MB_ICONERROR);
        SetStatus(STATUS_SERVER, L"Sidecar not running");
    }

    Layout();
    ShowPage(0);
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            OnCreate(hwnd);
            return 0;
        case WM_SIZE:
            Layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize = {Scale(760), Scale(660)};
            return 0;
        }
        case WM_NOTIFY: {
            auto* header = reinterpret_cast<NMHDR*>(lParam);
            if (header->hwndFrom == g_tabs && header->code == TCN_SELCHANGE) {
                ShowPage(TabCtrl_GetCurSel(g_tabs));
            }
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_EXIT:
                    SendMessageW(hwnd, WM_CLOSE, 0, 0);
                    break;
                case IDM_VIEW_SERVER:
                case IDM_VIEW_CONNECTIONS:
                case IDM_VIEW_MESSAGES:
                    ShowPage(LOWORD(wParam) - IDM_VIEW_SERVER);
                    break;
                case IDM_ABOUT:
                    MessageBoxW(hwnd,
                                L"ImapKit Desktop " APP_VERSION_W L"\n\nA mock IMAP and SMTP server for testing email clients, "
                                L"powered by ImapKit (imapkit.com).",
                                L"About ImapKit Desktop", MB_ICONINFORMATION);
                    break;
            }
            return 0;
        case WM_SIDECAR_LINES:
        case WM_SIDECAR_STDERR: {
            std::unique_ptr<std::vector<std::string>> lines(reinterpret_cast<std::vector<std::string>*>(lParam));
            for (const std::string& line : *lines) {
                if (message == WM_SIDECAR_LINES) {
                    TheSidecar().HandleLine(line);
                } else {
                    OutputDebugStringW((L"sidecar: " + Widen(line) + L"\n").c_str());
                }
            }
            return 0;
        }
        case WM_SIDECAR_EXIT:
            SetStatus(STATUS_SERVER, L"Sidecar exited with code " + std::to_wstring(wParam));
            return 0;
        case WM_CLOSE:
            SetStatus(STATUS_INFO, L"Saving...");
            TheSidecar().Stop();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

void SetStatus(StatusPart part, const std::wstring& text) {
    if (g_status) {
        SendMessageW(g_status, SB_SETTEXTW, part, reinterpret_cast<LPARAM>(text.c_str()));
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    // WebView2 needs a single threaded apartment
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_WIN95_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    // 101 is the icon of app.rc
    wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                             GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = kMainClass;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, kMainClass, kTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                                CW_USEDEFAULT, Scale(1120), Scale(760), nullptr, BuildMenu(), instance, nullptr);
    ShowWindow(hwnd, show);

    ACCEL keys[] = {
        {FCONTROL | FVIRTKEY, '1', IDM_VIEW_SERVER},
        {FCONTROL | FVIRTKEY, '2', IDM_VIEW_CONNECTIONS},
        {FCONTROL | FVIRTKEY, '3', IDM_VIEW_MESSAGES},
    };
    HACCEL accelerators = CreateAcceleratorTableW(keys, 3);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (TranslateAcceleratorW(hwnd, accelerators, &msg)) {
            continue;
        }
        if (IsDialogMessageW(hwnd, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}
