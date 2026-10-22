// Shared declarations of ImapKit Desktop: string and UI helpers, the sidecar client and the page base class
#pragma once

#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>

#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

// posted by the sidecar reader threads, lParam is a std::vector<std::string>* of complete lines that the receiver deletes
constexpr UINT WM_SIDECAR_LINES = WM_APP + 1;
constexpr UINT WM_SIDECAR_STDERR = WM_APP + 2;
// the sidecar process exited, wParam is the exit code
constexpr UINT WM_SIDECAR_EXIT = WM_APP + 3;

// ---- helpers (page.cpp) ----

std::wstring Widen(const std::string& text);
std::string Narrow(const std::wstring& text);
// a json string value as wide text, empty for null or other types
std::wstring JsonText(const json& value, const char* key);
// a json string value, empty for null or other types
std::string JsonString(const json& value, const char* key);
// pixels at 96 DPI to pixels at the DPI of the screen
int Scale(int pixels);
// Tahoma 8pt, the classic dialog font
HFONT UiFont();
HFONT BoldFont();
HFONT MonoFont();
HFONT MakeFont(const wchar_t* face, int points, int weight);
// "2026-10-22T11:05:00.000Z" to local "22.10.2026 14:05"
std::wstring FormatTime(const std::string& iso, bool withSeconds = false);
std::wstring FormatSize(double bytes);
std::wstring GetWindowString(HWND hwnd);
std::wstring ExeDirectory();
// registers a window class with the grey dialog background, once per name
void RegisterPlainClass(const wchar_t* name, WNDPROC proc);
// creates a child control with the UI font
HWND CreateChild(HWND parent, const wchar_t* className, const wchar_t* text, DWORD style, int id, RECT rect = {}, DWORD exStyle = 0);
// the WM_CTLCOLORSTATIC answer that puts labels on the grey of the window
LRESULT ButtonFaceColor(HDC dc);

struct Column {
    const wchar_t* text;
    int width;
    int format = LVCFMT_LEFT;
};
// adds report columns to a list view, widths at 96 DPI
void AddColumns(HWND list, std::initializer_list<Column> columns);
// shows a context menu at the mouse, or under `anchor` (client coordinates of `control`) when the menu key was used,
// and destroys it. The commands go to `owner`
void TrackContextMenu(HMENU menu, HWND owner, HWND control, POINT at, RECT anchor);
// gives a column of a report list view the width the other columns leave, so the rows (and the highlight of the
// selected one) reach the right edge. Call it after the list was sized
void StretchColumn(HWND list, int column, int minimum = 60);
// a gray hint in the middle of a list view while it has no items, e.g. what to do to fill it
void SetEmptyText(HWND list, const std::wstring& text);
// draws a group-box-like etched frame, for areas a page paints itself
void DrawEtchedFrame(HDC dc, RECT rect);

// ---- dialogs and the clipboard (dialog.cpp) ----

struct PromptField {
    std::wstring label;
    std::wstring value;
    bool multiline = false;
};

// a modal form with a labeled edit box per field, true and the edited values in `fields` when OK was pressed
bool PromptDialog(HWND owner, const std::wstring& title, std::vector<PromptField>& fields, int width = 380);
// the chosen file, empty when cancelled. `filter` is a GetOpenFileName filter ("Name\0*.ext\0")
std::wstring OpenFileDialog(HWND owner, const wchar_t* title, const wchar_t* filter);
// the chosen files, several can be selected
std::vector<std::wstring> OpenFilesDialog(HWND owner, const wchar_t* title, const wchar_t* filter);
// a folder chosen in the folder dialog, empty when cancelled
std::wstring FolderDialog(HWND owner, const wchar_t* title);
std::wstring SaveFileDialog(HWND owner, const wchar_t* title, const wchar_t* filter, const wchar_t* extension, const std::wstring& name);
void CopyToClipboard(HWND owner, const std::wstring& text);
// writes text as UTF-8, false on failure
bool WriteTextFile(const std::wstring& file, const std::wstring& text);

// ---- the sidecar process (sidecar.cpp) ----

class Sidecar {
public:
    using Callback = std::function<void(const json& result, const std::string& error)>;
    using EventHandler = std::function<void(const std::string& name, const json& data)>;

    // starts node with the sidecar script, its output is posted to `notify`
    bool Start(HWND notify, std::wstring& error);
    void Stop();
    // sends a request, the callback runs on the UI thread with the result or an error message
    void Call(const std::string& method, const json& params = json::object(), Callback callback = {});
    // a line of the sidecar output, called on the UI thread
    void HandleLine(const std::string& line);

    EventHandler onEvent;

private:
    void ReadLoop(HANDLE pipe, UINT message);

    HWND notify_ = nullptr;
    HANDLE process_ = nullptr;
    HANDLE job_ = nullptr;
    HANDLE stdinWrite_ = nullptr;
    std::thread stdoutThread_;
    std::thread stderrThread_;
    std::thread waitThread_;
    int nextId_ = 0;
    std::map<int, Callback> pending_;
};

Sidecar& TheSidecar();
// a callback that shows the error of a failed request in a message box titled "<action> failed"
Sidecar::Callback ErrorReporter(HWND owner, std::wstring action);
// the parts of the status bar of the main window (main.cpp)
enum StatusPart { STATUS_SERVER = 0, STATUS_SESSIONS = 1, STATUS_INFO = 2 };
void SetStatus(StatusPart part, const std::wstring& text);

// ---- pages (page.cpp) ----

// A view of the main window: a child window that holds its controls and gets the sidecar events
class Page {
public:
    virtual ~Page() = default;

    HWND Create(HWND parent);
    HWND hwnd() const { return hwnd_; }

    virtual void OnEvent(const std::string& /*name*/, const json& /*data*/) {}
    // false for a page with group boxes: they draw only their frame, so the page has to paint the background under
    // them, which WS_CLIPCHILDREN would skip and leave the pixels of the page shown before
    virtual bool ClipChildren() const { return true; }
    // the page became visible
    virtual void OnShow() {}

protected:
    virtual void OnCreate() = 0;
    virtual void OnSize(int width, int height) = 0;
    virtual void OnCommand(int /*id*/, int /*code*/, HWND /*control*/) {}
    virtual LRESULT OnNotify(NMHDR* /*header*/) { return 0; }
    // other messages, set handled to keep them from DefWindowProc
    virtual LRESULT OnMessage(UINT /*message*/, WPARAM /*wParam*/, LPARAM /*lParam*/, bool& handled) {
        handled = false;
        return 0;
    }

    // creates a child control of the page with the UI font
    HWND AddControl(const wchar_t* className, const wchar_t* text, DWORD style, int id, DWORD exStyle = 0);

    // A gap between two panes that the mouse drags. The page sets `area` (client coordinates) in OnSize, `drag` gets
    // the new x (vertical splitter) or y and moves the panes
    struct Splitter {
        bool vertical;
        RECT area;
        std::function<void(int)> drag;
    };
    std::vector<Splitter> splitters_;

    HWND hwnd_ = nullptr;

private:
    bool HandleSplitter(UINT message, WPARAM wParam, LPARAM lParam);

    int dragging_ = -1;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
};

Page* CreateServerPage();
Page* CreateConnectionsPage();
Page* CreateMessagesPage();
