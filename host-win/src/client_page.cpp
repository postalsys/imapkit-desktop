// The Client page: a plaintext client like telnet or openssl s_client. A row with the server to connect to, the
// console with what was sent and received, and a command line below it that sends a line on Enter. The connection
// itself lives in the sidecar (raw-client.js), this page shows its `client` events
#include "app.h"

#include <richedit.h>

#include <algorithm>

namespace {

enum ControlId {
    IDC_PRESET_LABEL = 600,
    IDC_PRESET,
    IDC_HOST_LABEL,
    IDC_HOST,
    IDC_PORT_LABEL,
    IDC_PORT,
    IDC_TLS,
    IDC_CONNECT,
    IDC_STATE,
    IDC_SAVE,
    IDC_CLEAR,
    IDC_CONSOLE,
    IDC_COMMAND_LABEL,
    IDC_COMMAND,
    IDC_SEND,
};

// the console keeps about this many characters, the oldest quarter goes when it is full
constexpr int kMaxConsoleText = 2 * 1024 * 1024;
constexpr size_t kMaxHistory = 100;

const COLORREF kReceivedColor = RGB(0, 0, 0);
const COLORREF kSentColor = RGB(0, 0, 160);
const COLORREF kInfoColor = RGB(110, 110, 110);
const COLORREF kErrorColor = RGB(180, 0, 0);

const wchar_t* kHint =
    L"Connect to a host and port, then type IMAP or SMTP commands into the command line below. Enter sends the line "
    L"with CRLF, Up and Down recall earlier lines.\r\n"
    L"With TLS every certificate is accepted, self-signed ones too, and its details are shown here. In a plaintext "
    L"session, STARTTLS upgrades the connection when the server agrees to it.\r\n\r\n";

enum TimerId {
    // received text is shown in batches, a big response comes in thousands of chunks
    TIMER_FLUSH = 1,
};

class ClientPage : public Page {
public:
    void OnEvent(const std::string& name, const json& data) override {
        if (name == "client") {
            OnClient(data);
        } else if (name == "status") {
            // the settings may have changed, the presets follow them
            TheSidecar().Call("clientPresets", json::object(), [this](const json& result, const std::string& error) {
                if (error.empty()) {
                    SetPresets(result);
                }
            });
        }
    }

    void OnShow() override { SetFocus(connected_ ? command_ : host_); }

    LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM, bool& handled) override {
        if (message == WM_TIMER && wParam == TIMER_FLUSH) {
            KillTimer(hwnd_, TIMER_FLUSH);
            Flush();
            return 0;
        }
        handled = false;
        return 0;
    }

protected:
    void OnCreate() override {
        LoadLibraryW(L"Riched20.dll");

        presetLabel_ = AddControl(L"STATIC", L"&Preset:", SS_RIGHT | SS_CENTERIMAGE, IDC_PRESET_LABEL);
        preset_ = AddControl(WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, IDC_PRESET);
        hostLabel_ = AddControl(L"STATIC", L"&Host:", SS_RIGHT | SS_CENTERIMAGE, IDC_HOST_LABEL);
        host_ = AddControl(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, IDC_HOST, WS_EX_CLIENTEDGE);
        portLabel_ = AddControl(L"STATIC", L"P&ort:", SS_RIGHT | SS_CENTERIMAGE, IDC_PORT_LABEL);
        port_ = AddControl(L"EDIT", L"", WS_TABSTOP | ES_NUMBER, IDC_PORT, WS_EX_CLIENTEDGE);
        SendMessageW(port_, EM_LIMITTEXT, 5, 0);
        tls_ = AddControl(L"BUTTON", L"Use &TLS", WS_TABSTOP | BS_AUTOCHECKBOX, IDC_TLS);
        connect_ = AddControl(L"BUTTON", L"&Connect", WS_TABSTOP, IDC_CONNECT);

        state_ = AddControl(L"STATIC", L"Not connected", SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS, IDC_STATE);
        SendMessageW(state_, WM_SETFONT, reinterpret_cast<WPARAM>(BoldFont()), TRUE);
        save_ = AddControl(L"BUTTON", L"&Save...", WS_TABSTOP, IDC_SAVE);
        clear_ = AddControl(L"BUTTON", L"C&lear", WS_TABSTOP, IDC_CLEAR);

        console_ = AddControl(RICHEDIT_CLASSW, L"", WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL,
                              IDC_CONSOLE, WS_EX_CLIENTEDGE);
        SendMessageW(console_, WM_SETFONT, reinterpret_cast<WPARAM>(MonoFont()), TRUE);
        SendMessageW(console_, EM_SETBKGNDCOLOR, 0, GetSysColor(COLOR_WINDOW));
        SendMessageW(console_, EM_EXLIMITTEXT, 0, kMaxConsoleText + kMaxConsoleText / 2);

        commandLabel_ = AddControl(L"STATIC", L"Co&mmand:", SS_RIGHT | SS_CENTERIMAGE, IDC_COMMAND_LABEL);
        command_ = AddControl(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, IDC_COMMAND, WS_EX_CLIENTEDGE);
        SendMessageW(command_, WM_SETFONT, reinterpret_cast<WPARAM>(MonoFont()), TRUE);
        SetWindowSubclass(command_, CommandProc, 0, reinterpret_cast<DWORD_PTR>(this));
        send_ = AddControl(L"BUTTON", L"Se&nd", WS_TABSTOP, IDC_SEND);

        Append(kHint, kInfoColor);
        UpdateControls();
    }

    void OnSize(int width, int height) override {
        if (width == 0 || height == 0) {
            return;
        }
        int pad = Scale(6);
        int bar = Scale(25);
        int edit = Scale(21);
        int button = Scale(90);
        int narrow = Scale(70);

        // the connection row: the host takes what the other fields leave, up to a sensible width
        int y = pad;
        int x = pad;
        auto place = [&](HWND control, int w, int h, int dy = 0) {
            MoveWindow(control, x, y + dy, w, h, TRUE);
            x += w + pad;
        };
        int fixed = Scale(44) + Scale(130) + Scale(34) + Scale(34) + Scale(55) + Scale(80) + button + 8 * pad;
        int hostWidth = std::clamp(width - fixed, Scale(100), Scale(260));
        place(presetLabel_, Scale(44), bar);
        place(preset_, Scale(130), Scale(200), Scale(2));
        place(hostLabel_, Scale(34), bar);
        place(host_, hostWidth, edit, Scale(2));
        place(portLabel_, Scale(34), bar);
        place(port_, Scale(55), edit, Scale(2));
        place(tls_, Scale(80), bar);
        place(connect_, button, bar);
        y += bar + pad;

        // the console with its header row
        MoveWindow(clear_, width - pad - narrow, y, narrow, bar, TRUE);
        MoveWindow(save_, width - 2 * (pad + narrow), y, narrow, bar, TRUE);
        MoveWindow(state_, pad, y, std::max(0, width - 2 * (pad + narrow) - 2 * pad), bar, TRUE);
        y += bar + pad;
        int bottom = height - pad - bar;
        MoveWindow(console_, pad, y, width - 2 * pad, std::max(0, bottom - pad - y), TRUE);

        // the command line
        int labelWidth = Scale(60);
        MoveWindow(commandLabel_, pad, bottom, labelWidth, bar, TRUE);
        MoveWindow(send_, width - pad - narrow, bottom, narrow, bar, TRUE);
        MoveWindow(command_, pad + labelWidth + pad, bottom + Scale(2), std::max(0, width - labelWidth - narrow - 4 * pad), edit, TRUE);
    }

    void OnCommand(int id, int code, HWND) override {
        if (id == IDC_PRESET && code == CBN_SELCHANGE) {
            ApplyPreset(static_cast<int>(SendMessageW(preset_, CB_GETCURSEL, 0, 0)));
            return;
        }
        // an edited field is no preset any more
        if (((id == IDC_HOST || id == IDC_PORT) && code == EN_CHANGE && !applyingPreset_) || (id == IDC_TLS && code == BN_CLICKED)) {
            SendMessageW(preset_, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
            return;
        }
        switch (id) {
            case IDC_CONNECT:
                ToggleConnection();
                break;
            case IDC_SEND:
                SendCommand();
                break;
            case IDC_CLEAR:
                pending_.clear();
                SetWindowTextW(console_, L"");
                break;
            case IDC_SAVE:
                SaveConsole();
                break;
        }
    }

private:
    // Enter sends the line, Up and Down walk the history, like a shell
    static LRESULT CALLBACK CommandProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
        auto* page = reinterpret_cast<ClientPage*>(data);
        switch (message) {
            case WM_GETDLGCODE: {
                // keeps Enter from the dialog navigation of the main window
                auto* msg = reinterpret_cast<MSG*>(lParam);
                LRESULT code = DefSubclassProc(hwnd, message, wParam, lParam);
                if (msg && msg->message == WM_KEYDOWN && msg->wParam == VK_RETURN) {
                    code |= DLGC_WANTALLKEYS;
                }
                return code;
            }
            case WM_KEYDOWN:
                if (wParam == VK_RETURN) {
                    page->SendCommand();
                    return 0;
                }
                if (wParam == VK_UP || wParam == VK_DOWN) {
                    page->RecallHistory(wParam == VK_UP ? -1 : 1);
                    return 0;
                }
                break;
            case WM_CHAR:
                // the Enter handled above, without the beep of a single line edit
                if (wParam == L'\r') {
                    return 0;
                }
                break;
            case WM_NCDESTROY:
                RemoveWindowSubclass(hwnd, CommandProc, 0);
                break;
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    // fills the preset list with the servers of this app, `[{ name, host, port, tls }]` from the sidecar. The preset in
    // use stays selected and fills the fields again, its port may have changed; fields edited by hand stay as they are
    void SetPresets(const json& presets) {
        int selected = static_cast<int>(SendMessageW(preset_, CB_GETCURSEL, 0, 0));
        std::string name = selected >= 0 && selected < static_cast<int>(presets_.size()) ? JsonString(presets_[selected], "name") : "";
        bool first = presets_.empty() && selected < 0;
        presets_ = presets.is_array() ? presets : json::array();
        SendMessageW(preset_, CB_RESETCONTENT, 0, 0);
        int index = -1;
        for (size_t i = 0; i < presets_.size(); i++) {
            SendMessageW(preset_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(JsonText(presets_[i], "name").c_str()));
            if ((first && i == 0) || (!name.empty() && JsonString(presets_[i], "name") == name)) {
                index = static_cast<int>(i);
            }
        }
        SendMessageW(preset_, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
        if (!connected_ && !connecting_) {
            ApplyPreset(index);
        }
    }

    void ApplyPreset(int index) {
        if (index < 0 || index >= static_cast<int>(presets_.size())) {
            return;
        }
        const json& preset = presets_[index];
        applyingPreset_ = true;
        SetWindowTextW(host_, JsonText(preset, "host").c_str());
        SetWindowTextW(port_, std::to_wstring(preset.value("port", 0)).c_str());
        applyingPreset_ = false;
        Button_SetCheck(tls_, preset.value("tls", false) ? BST_CHECKED : BST_UNCHECKED);
    }

    void ToggleConnection() {
        if (connected_ || connecting_) {
            TheSidecar().Call("clientDisconnect");
            return;
        }
        json params = {{"host", Narrow(GetWindowString(host_))},
                       {"port", _wtoi(GetWindowString(port_).c_str())},
                       {"tls", Button_GetCheck(tls_) == BST_CHECKED}};
        // the answer has only mistakes in the fields, how the connection goes comes as events
        TheSidecar().Call("clientConnect", params, [this](const json&, const std::string& error) {
            if (!error.empty()) {
                Append(Widen(error) + L"\r\n", kErrorColor);
            }
        });
    }

    void SendCommand() {
        if (!connected_) {
            return;
        }
        std::wstring line = GetWindowString(command_);
        if (!line.empty() && (history_.empty() || history_.back() != line)) {
            history_.push_back(line);
            if (history_.size() > kMaxHistory) {
                history_.erase(history_.begin());
            }
        }
        historyIndex_ = history_.size();
        SetWindowTextW(command_, L"");
        TheSidecar().Call("clientSend", {{"data", Narrow(line)}}, [this](const json&, const std::string& error) {
            if (!error.empty()) {
                Append(Widen(error) + L"\r\n", kErrorColor);
            }
        });
    }

    void RecallHistory(int step) {
        if (history_.empty()) {
            return;
        }
        size_t index = historyIndex_;
        if (step < 0 && index > 0) {
            index--;
        } else if (step > 0 && index < history_.size()) {
            index++;
        } else {
            return;
        }
        historyIndex_ = index;
        // past the newest line is an empty line again
        std::wstring text = index < history_.size() ? history_[index] : L"";
        SetWindowTextW(command_, text.c_str());
        SendMessageW(command_, EM_SETSEL, text.size(), text.size());
    }

    void OnClient(const json& data) {
        std::string type = JsonString(data, "type");
        if (type == "state") {
            connected_ = data.value("connected", false);
            connecting_ = data.value("connecting", false);
            secure_ = data.value("secure", false);
            peer_ = JsonText(data, "host") + L":" + std::to_wstring(data.value("port", 0));
            UpdateControls();
            if (connected_ && GetFocus() != command_) {
                SetFocus(command_);
            }
            return;
        }
        std::wstring text = JsonText(data, "text");
        if (type == "info" || type == "error") {
            text += L"\r\n";
        }
        Append(text, type == "data" ? kReceivedColor : type == "sent" ? kSentColor : type == "error" ? kErrorColor : kInfoColor);
    }

    void UpdateControls() {
        bool idle = !connected_ && !connecting_;
        for (HWND control : {preset_, host_, port_, tls_}) {
            EnableWindow(control, idle);
        }
        EnableWindow(command_, connected_);
        EnableWindow(send_, connected_);
        SetWindowTextW(connect_, connected_ ? L"&Disconnect" : connecting_ ? L"&Cancel" : L"&Connect");
        std::wstring state = connecting_  ? L"Connecting to " + peer_ + L"..."
                             : connected_ ? L"Connected to " + peer_ + (secure_ ? L" with TLS" : L", plaintext")
                                          : L"Not connected";
        SetWindowTextW(state_, state.c_str());
    }

    // queues text in a color for the end of the console, the timer shows it
    void Append(const std::wstring& text, COLORREF color) {
        if (text.empty()) {
            return;
        }
        if (!pending_.empty() && pending_.back().color == color) {
            pending_.back().text += text;
        } else {
            pending_.push_back({color, text});
        }
        if (!flushTimer_) {
            flushTimer_ = true;
            SetTimer(hwnd_, TIMER_FLUSH, 30, nullptr);
        }
    }

    // adds the queued text in one go without repainting in between, keeping what the user selected and the scroll
    // position unless the end was in view
    void Flush() {
        flushTimer_ = false;
        if (pending_.empty()) {
            return;
        }
        GETTEXTLENGTHEX lengthQuery = {GTL_NUMCHARS, 1200};
        int length = static_cast<int>(SendMessageW(console_, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&lengthQuery), 0));
        // the end is in view when the last character is above the bottom edge
        RECT client;
        GetClientRect(console_, &client);
        POINTL last = {};
        SendMessageW(console_, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&last), std::max(0, length - 1));
        bool atEnd = last.y < client.bottom;
        CHARRANGE selection;
        SendMessageW(console_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));

        SendMessageW(console_, WM_SETREDRAW, FALSE, 0);
        if (length > kMaxConsoleText) {
            CHARRANGE oldest = {0, length / 4};
            SendMessageW(console_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&oldest));
            SendMessageW(console_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
            selection = {0, 0};
        }
        CHARFORMAT2W format = {};
        format.cbSize = sizeof(format);
        format.dwMask = CFM_COLOR;
        for (const Run& run : pending_) {
            // at the end, which moves with every run (a CRLF counts as one character there)
            LONG endAt = static_cast<LONG>(SendMessageW(console_, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&lengthQuery), 0));
            CHARRANGE end = {endAt, endAt};
            SendMessageW(console_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&end));
            format.crTextColor = run.color;
            SendMessageW(console_, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
            SendMessageW(console_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(run.text.c_str()));
        }
        pending_.clear();

        if (selection.cpMin != selection.cpMax) {
            SendMessageW(console_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
        }
        if (atEnd) {
            SendMessageW(console_, WM_VSCROLL, SB_BOTTOM, 0);
        }
        SendMessageW(console_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(console_, nullptr, TRUE);
    }

    void SaveConsole() {
        Flush();
        GETTEXTLENGTHEX lengthQuery = {GTL_USECRLF | GTL_NUMCHARS, 1200};
        int length = static_cast<int>(SendMessageW(console_, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&lengthQuery), 0));
        std::wstring text(length + 1, L'\0');
        GETTEXTEX query = {};
        query.cb = static_cast<DWORD>(text.size() * sizeof(wchar_t));
        query.flags = GT_USECRLF;
        query.codepage = 1200;
        text.resize(SendMessageW(console_, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&query), reinterpret_cast<LPARAM>(text.data())));
        std::wstring file = SaveFileDialog(hwnd_, L"Save session", L"Text files (*.txt)\0*.txt\0Log files (*.log)\0*.log\0", L"txt", L"client-session");
        if (!file.empty() && !WriteTextFile(file, text)) {
            MessageBoxW(hwnd_, (L"Could not write " + file).c_str(), L"Save session", MB_ICONERROR);
        }
    }

    HWND presetLabel_ = nullptr, preset_ = nullptr, hostLabel_ = nullptr, host_ = nullptr, portLabel_ = nullptr, port_ = nullptr;
    HWND tls_ = nullptr, connect_ = nullptr, state_ = nullptr, save_ = nullptr, clear_ = nullptr, console_ = nullptr;
    HWND commandLabel_ = nullptr, command_ = nullptr, send_ = nullptr;
    // text waiting for the console, runs of one color
    struct Run {
        COLORREF color;
        std::wstring text;
    };
    std::vector<Run> pending_;
    bool flushTimer_ = false;
    json presets_ = json::array();
    bool connected_ = false, connecting_ = false, secure_ = false, applyingPreset_ = false;
    std::wstring peer_;
    std::vector<std::wstring> history_;
    size_t historyIndex_ = 0;
};

}  // namespace

Page* CreateClientPage() {
    return new ClientPage();
}
