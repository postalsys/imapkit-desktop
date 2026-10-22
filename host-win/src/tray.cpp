// The icon of ImapKit Desktop in the notification area: the server keeps running there while the window is closed
#include "app.h"

#include <shellapi.h>

namespace {

NOTIFYICONDATAW g_icon = {};
bool g_added = false;

void CopyText(wchar_t* target, size_t size, const std::wstring& text) {
    wcsncpy_s(target, size, text.c_str(), _TRUNCATE);
}

}  // namespace

void TrayAdd(HWND hwnd, HICON icon, const std::wstring& tip) {
    g_icon.cbSize = sizeof(g_icon);
    g_icon.hWnd = hwnd;
    g_icon.uID = 1;
    g_icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_icon.uCallbackMessage = WM_TRAY;
    g_icon.hIcon = icon;
    CopyText(g_icon.szTip, std::size(g_icon.szTip), tip);
    g_added = Shell_NotifyIconW(NIM_ADD, &g_icon) != FALSE;
}

void TrayRemove() {
    if (g_added) {
        Shell_NotifyIconW(NIM_DELETE, &g_icon);
        g_added = false;
    }
}

void TraySetTip(const std::wstring& tip) {
    CopyText(g_icon.szTip, std::size(g_icon.szTip), tip);
    if (g_added) {
        g_icon.uFlags = NIF_TIP;
        Shell_NotifyIconW(NIM_MODIFY, &g_icon);
    }
}

void TrayBalloon(const std::wstring& title, const std::wstring& text) {
    if (!g_added) {
        return;
    }
    NOTIFYICONDATAW balloon = g_icon;
    balloon.uFlags = NIF_INFO;
    balloon.dwInfoFlags = NIIF_INFO;
    CopyText(balloon.szInfoTitle, std::size(balloon.szInfoTitle), title);
    CopyText(balloon.szInfo, std::size(balloon.szInfo), text);
    Shell_NotifyIconW(NIM_MODIFY, &balloon);
}

int TrayMenu(HWND hwnd, const std::vector<TrayItem>& items) {
    HMENU menu = CreatePopupMenu();
    for (const TrayItem& item : items) {
        if (item.id == 0) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        } else {
            AppendMenuW(menu, MF_STRING, item.id, item.text.c_str());
        }
    }
    // the first item is what a click on the icon does
    SetMenuDefaultItem(menu, 0, TRUE);
    POINT at;
    GetCursorPos(&at);
    // without the foreground the menu would not close when clicking elsewhere
    SetForegroundWindow(hwnd);
    int id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, at.x, at.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    return id;
}
