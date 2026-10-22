// The Connections page: IMAP and SMTP sessions, and the protocol log of the selected session (or of all of them)
#include "app.h"

#include <shlwapi.h>

#include <algorithm>
#include <deque>

namespace {

enum ControlId {
    IDC_SESSIONS = 300,
    IDC_LOG,
    IDC_DISCONNECT,
    IDC_REMOVE_CLOSED,
    IDC_CLEAR_LOG,
    IDC_LOG_LABEL,
    IDC_FILTER_LABEL,
    IDC_FILTER,
    // menu commands
    IDM_COPY = 350,
    IDM_COPY_DATA,
    IDM_SELECT_ALL,
    IDM_SAVE_LOG,
    IDM_CLEAR_LOG,
    IDM_DISCONNECT,
    IDM_INJECT,
    IDM_COPY_SESSION_LOG,
};

enum TimerId {
    // new log lines are shown in batches, a busy client sends many trace events per second
    TIMER_LOG = 1,
    // the filter applies a moment after the last keystroke
    TIMER_FILTER,
};

// the log keeps this many lines, older ones go first
constexpr size_t kMaxLogLines = 200000;

struct SessionRow {
    std::string id;
    json info;
    bool closed = false;
};

struct LogLine {
    // seconds since local midnight
    int time;
    std::wstring session;
    bool fromClient;
    std::wstring text;
};

// local seconds since midnight of an ISO time
int LocalSeconds(const std::string& iso) {
    std::wstring local = FormatTime(iso, true);
    int hours = 0, minutes = 0, seconds = 0;
    swscanf_s(local.c_str(), L"%d:%d:%d", &hours, &minutes, &seconds);
    return hours * 3600 + minutes * 60 + seconds;
}

class ConnectionsPage : public Page {
public:
    void OnEvent(const std::string& name, const json& data) override {
        if (name == "session") {
            OnSession(data);
        } else if (name == "trace") {
            OnTrace(data);
        }
    }

protected:
    void OnCreate() override {
        sessions_ = AddControl(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, IDC_SESSIONS,
                               WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(sessions_, LVS_EX_FULLROWSELECT);
        AddColumns(sessions_, {{L"Session", 70},
                               {L"Protocol", 60},
                               {L"Client", 140},
                               {L"User", 110},
                               {L"State", 110},
                               {L"Mailbox", 120},
                               {L"TLS", 40},
                               {L"Connected", 110}});

        disconnect_ = AddControl(L"BUTTON", L"&Disconnect", WS_TABSTOP, IDC_DISCONNECT);
        removeClosed_ = AddControl(L"BUTTON", L"&Remove closed", WS_TABSTOP, IDC_REMOVE_CLOSED);
        clearLog_ = AddControl(L"BUTTON", L"&Clear log", WS_TABSTOP, IDC_CLEAR_LOG);
        logLabel_ = AddControl(L"STATIC", L"Protocol log of all sessions", SS_LEFT | SS_CENTERIMAGE, IDC_LOG_LABEL);
        filterLabel_ = AddControl(L"STATIC", L"&Filter:", SS_RIGHT | SS_CENTERIMAGE, IDC_FILTER_LABEL);
        filter_ = AddControl(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, IDC_FILTER, WS_EX_CLIENTEDGE);

        log_ = AddControl(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS, IDC_LOG, WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(log_, LVS_EX_FULLROWSELECT);
        SendMessageW(log_, WM_SETFONT, reinterpret_cast<WPARAM>(MonoFont()), TRUE);
        AddColumns(log_, {{L"Time", 70}, {L"Session", 70}, {L"", 30}, {L"Data", 900}});
        EnableWindow(disconnect_, FALSE);

        splitters_.push_back({false, {}, [this](int y) { listHeight_ = std::max(Scale(60), y - Scale(6)); }});
    }

    void OnSize(int width, int height) override {
        if (width == 0 || height == 0) {
            // created before the main window gave it a size, the split is set on the first real size
            return;
        }
        int pad = Scale(6);
        if (listHeight_ == 0) {
            listHeight_ = (height - 2 * pad) * 35 / 100;
        }
        listHeight_ = std::clamp(listHeight_, Scale(60), std::max(Scale(60), height - Scale(160)));
        MoveWindow(sessions_, pad, pad, width - 2 * pad, listHeight_, TRUE);
        int y = pad + listHeight_ + pad;
        splitters_[0].area = {0, pad + listHeight_, width, y};
        int buttonWidth = Scale(100);
        MoveWindow(disconnect_, pad, y, buttonWidth, Scale(24), TRUE);
        MoveWindow(removeClosed_, pad + buttonWidth + pad, y, buttonWidth, Scale(24), TRUE);
        MoveWindow(clearLog_, pad + 2 * (buttonWidth + pad), y, buttonWidth, Scale(24), TRUE);
        int filterWidth = Scale(200);
        int labelLeft = pad + 3 * (buttonWidth + pad) + pad;
        MoveWindow(filter_, width - pad - filterWidth, y + Scale(1), filterWidth, Scale(22), TRUE);
        MoveWindow(filterLabel_, width - pad - filterWidth - Scale(50), y, Scale(46), Scale(24), TRUE);
        MoveWindow(logLabel_, labelLeft, y, width - labelLeft - filterWidth - Scale(60), Scale(24), TRUE);
        y += Scale(24) + pad;
        MoveWindow(log_, pad, y, width - 2 * pad, height - y - pad, TRUE);
    }

    void OnCommand(int id, int code, HWND) override {
        if (id == IDC_FILTER && code == EN_CHANGE) {
            SetTimer(hwnd_, TIMER_FILTER, 200, nullptr);
            return;
        }
        switch (id) {
            case IDC_DISCONNECT:
            case IDM_DISCONNECT:
                if (!sessionFilter_.empty()) {
                    TheSidecar().Call("disconnect", {{"id", sessionFilter_}});
                }
                break;
            case IDC_REMOVE_CLOSED:
                std::erase_if(rows_, [](const SessionRow& row) { return row.closed; });
                RebuildSessions();
                break;
            case IDC_CLEAR_LOG:
            case IDM_CLEAR_LOG:
                lines_.clear();
                RebuildLog();
                break;
            case IDM_COPY:
            case IDM_COPY_DATA:
                CopyToClipboard(hwnd_, LogText(SelectedRows(), id == IDM_COPY));
                break;
            case IDM_SELECT_ALL:
                ListView_SetItemState(log_, -1, LVIS_SELECTED, LVIS_SELECTED);
                break;
            case IDM_SAVE_LOG:
                SaveLog();
                break;
            case IDM_COPY_SESSION_LOG:
                CopyToClipboard(hwnd_, LogText(AllRows(), true));
                break;
            case IDM_INJECT:
                Inject();
                break;
        }
    }

    LRESULT OnNotify(NMHDR* header) override {
        if (header->hwndFrom == log_) {
            switch (header->code) {
                case LVN_GETDISPINFOW:
                    FillLogItem(reinterpret_cast<NMLVDISPINFOW*>(header)->item);
                    return 0;
                case LVN_KEYDOWN: {
                    // Ctrl+C and Ctrl+A, as in a text editor
                    WORD key = reinterpret_cast<NMLVKEYDOWN*>(header)->wVKey;
                    if (GetKeyState(VK_CONTROL) < 0 && key == 'C') {
                        OnCommand(IDM_COPY, 0, nullptr);
                    } else if (GetKeyState(VK_CONTROL) < 0 && key == 'A') {
                        OnCommand(IDM_SELECT_ALL, 0, nullptr);
                    }
                    return 0;
                }
                case NM_CUSTOMDRAW: {
                    auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(header);
                    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
                        return CDRF_NOTIFYITEMDRAW;
                    }
                    if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                        size_t index = draw->nmcd.dwItemSpec;
                        if (index < visible_.size() && lines_[visible_[index]].fromClient) {
                            // client commands in blue, like the C: lines of a protocol log
                            draw->clrText = RGB(0, 0, 160);
                        }
                    }
                    return CDRF_DODEFAULT;
                }
            }
        }
        if (header->hwndFrom == sessions_ && header->code == LVN_ITEMCHANGED) {
            auto* change = reinterpret_cast<NMLISTVIEW*>(header);
            if ((change->uChanged & LVIF_STATE) && ((change->uNewState ^ change->uOldState) & LVIS_SELECTED)) {
                int selected = ListView_GetNextItem(sessions_, -1, LVNI_SELECTED);
                SetSessionFilter(selected >= 0 && selected < static_cast<int>(rows_.size()) ? rows_[selected].id : "");
            }
        }
        return 0;
    }

    LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam, bool& handled) override {
        if (message == WM_TIMER) {
            KillTimer(hwnd_, wParam);
            if (wParam == TIMER_LOG) {
                ShowNewLines();
            } else if (wParam == TIMER_FILTER) {
                textFilter_ = GetWindowString(filter_);
                RebuildLog();
            }
            return 0;
        }
        if (message != WM_CONTEXTMENU) {
            handled = false;
            return 0;
        }
        HWND control = reinterpret_cast<HWND>(wParam);
        POINT at = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (control == log_) {
            ShowLogMenu(at);
        } else if (control == sessions_) {
            ShowSessionMenu(at);
        } else {
            handled = false;
        }
        return 0;
    }

private:
    // the focused row of a list, where a menu opened with the menu key goes
    static RECT FocusedRect(HWND list) {
        RECT item = {};
        int focused = ListView_GetNextItem(list, -1, LVNI_FOCUSED);
        if (focused >= 0) {
            ListView_GetItemRect(list, focused, &item, LVIR_LABEL);
        }
        return item;
    }

    void ShowLogMenu(POINT at) {
        bool selected = ListView_GetSelectedCount(log_) > 0;
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (selected ? 0 : MF_GRAYED), IDM_COPY, L"&Copy\tCtrl+C");
        AppendMenuW(menu, MF_STRING | (selected ? 0 : MF_GRAYED), IDM_COPY_DATA, L"Copy &data only");
        AppendMenuW(menu, MF_STRING, IDM_SELECT_ALL, L"Select &all\tCtrl+A");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (visible_.empty() ? MF_GRAYED : 0), IDM_SAVE_LOG, L"&Save log as...");
        AppendMenuW(menu, MF_STRING, IDM_CLEAR_LOG, L"C&lear log");
        TrackContextMenu(menu, hwnd_, log_, at, FocusedRect(log_));
    }

    void ShowSessionMenu(POINT at) {
        const SessionRow* row = SelectedSession();
        bool open = row && !row->closed;
        bool imap = row && row->info.value("kind", "") == "imap";
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (open ? 0 : MF_GRAYED), IDM_DISCONNECT, L"&Disconnect");
        AppendMenuW(menu, MF_STRING | (open && imap ? 0 : MF_GRAYED), IDM_INJECT, L"Send &raw data...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (row ? 0 : MF_GRAYED), IDM_COPY_SESSION_LOG, L"&Copy session log");
        TrackContextMenu(menu, hwnd_, sessions_, at, FocusedRect(sessions_));
    }

    const SessionRow* SelectedSession() {
        auto found = std::find_if(rows_.begin(), rows_.end(), [this](const SessionRow& row) { return row.id == sessionFilter_; });
        return found == rows_.end() ? nullptr : &*found;
    }

    void UpdateDisconnect() {
        const SessionRow* row = SelectedSession();
        EnableWindow(disconnect_, row && !row->closed);
    }

    // writes output to the selected IMAP session as it is, e.g. an ALERT between commands
    void Inject() {
        std::vector<PromptField> fields = {{L"Data:", L"* OK [ALERT] Server maintenance in 5 minutes"}};
        if (sessionFilter_.empty() || !PromptDialog(hwnd_, L"Send raw data to " + Widen(sessionFilter_), fields, 460)) {
            return;
        }
        TheSidecar().Call("inject", {{"id", sessionFilter_}, {"data", Narrow(fields[0].value)}}, ErrorReporter(hwnd_, L"Send raw data"));
    }

    std::vector<int> SelectedRows() {
        std::vector<int> rows;
        int index = -1;
        while ((index = ListView_GetNextItem(log_, index, LVNI_SELECTED)) >= 0) {
            rows.push_back(index);
        }
        return rows;
    }

    std::vector<int> AllRows() {
        std::vector<int> rows(visible_.size());
        for (size_t i = 0; i < rows.size(); i++) {
            rows[i] = static_cast<int>(i);
        }
        return rows;
    }

    static std::wstring TimeText(int seconds) {
        wchar_t text[16];
        swprintf_s(text, L"%02d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60);
        return text;
    }

    // the rows as text: "time session C: data" per line, or the protocol data alone
    std::wstring LogText(const std::vector<int>& rows, bool withPrefix) {
        std::wstring text;
        for (int row : rows) {
            const LogLine& line = lines_[visible_[row]];
            if (withPrefix) {
                text += TimeText(line.time) + L"  " + line.session + (line.fromClient ? L"  C: " : L"  S: ");
            }
            text += line.text + L"\r\n";
        }
        return text;
    }

    void SaveLog() {
        std::wstring file = SaveFileDialog(hwnd_, L"Save protocol log", L"Text files (*.txt)\0*.txt\0Log files (*.log)\0*.log\0", L"txt",
                                           sessionFilter_.empty() ? L"protocol-log" : Widen(sessionFilter_) + L"-log");
        if (!file.empty() && !WriteTextFile(file, LogText(AllRows(), true))) {
            MessageBoxW(hwnd_, (L"Could not write " + file).c_str(), L"Save log", MB_ICONERROR);
        }
    }

    void InsertBlankRow(int index) {
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = index;
        item.pszText = const_cast<wchar_t*>(L"");
        ListView_InsertItem(sessions_, &item);
    }

    void OnSession(const json& data) {
        std::string id = JsonString(data, "id");
        auto row = std::find_if(rows_.begin(), rows_.end(), [&](const SessionRow& entry) { return entry.id == id; });
        if (row == rows_.end()) {
            rows_.push_back({id});
            row = rows_.end() - 1;
        }
        row->info = data;
        row->closed = JsonString(data, "type") == "close";
        int index = static_cast<int>(row - rows_.begin());
        if (index >= ListView_GetItemCount(sessions_)) {
            InsertBlankRow(index);
        }
        UpdateSessionRow(index);
        UpdateSessionCount();
    }

    void UpdateSessionRow(int index) {
        const SessionRow& row = rows_[index];
        const json& info = row.info;
        std::wstring kind = JsonText(info, "kind");
        std::transform(kind.begin(), kind.end(), kind.begin(), towupper);
        std::wstring texts[] = {std::to_wstring(info.value("number", 0)),
                                kind,
                                JsonText(info, "remoteAddress"),
                                JsonText(info, "user"),
                                row.closed ? L"Closed" : JsonText(info, "state"),
                                JsonText(info, "mailbox"),
                                info.value("secure", false) ? L"Yes" : L"No",
                                FormatTime(JsonString(info, "started"))};
        for (int column = 0; column < 8; column++) {
            ListView_SetItemText(sessions_, index, column, texts[column].data());
        }
        if (row.id == sessionFilter_) {
            UpdateDisconnect();
        }
    }

    void RebuildSessions() {
        ListView_DeleteAllItems(sessions_);
        for (int i = 0; i < static_cast<int>(rows_.size()); i++) {
            InsertBlankRow(i);
            UpdateSessionRow(i);
        }
        SetSessionFilter("");
        UpdateSessionCount();
    }

    void UpdateSessionCount() {
        auto open = std::count_if(rows_.begin(), rows_.end(), [](const SessionRow& row) { return !row.closed; });
        SetStatus(STATUS_SESSIONS, L"Connected sessions: " + std::to_wstring(open));
    }

    bool Matches(const LogLine& line) {
        // a case-insensitive search that copies nothing, the filter runs over every line of the log
        return (sessionFilterWide_.empty() || line.session == sessionFilterWide_) &&
               (textFilter_.empty() || StrStrIW(line.text.c_str(), textFilter_.c_str()) != nullptr);
    }

    void OnTrace(const json& data) {
        std::wstring session = JsonText(data, "id");
        bool fromClient = JsonString(data, "direction") == "in";
        int time = LocalSeconds(JsonString(data, "time"));
        std::wstring text = JsonText(data, "text");

        size_t start = 0;
        while (start < text.size()) {
            size_t end = text.find(L'\n', start);
            size_t next = end == std::wstring::npos ? text.size() : end + 1;
            if (end == std::wstring::npos) {
                end = text.size();
            }
            if (end > start && text[end - 1] == L'\r') {
                end--;
            }
            lines_.push_back({time, session, fromClient, text.substr(start, end - start)});
            if (Matches(lines_.back())) {
                visible_.push_back(lines_.size() - 1);
            }
            start = next;
        }
        if (!logTimer_) {
            logTimer_ = true;
            SetTimer(hwnd_, TIMER_LOG, 50, nullptr);
        }
    }

    // shows the lines that came since the last update, scrolled to the end when the end was in view
    void ShowNewLines() {
        logTimer_ = false;
        if (lines_.size() > kMaxLogLines) {
            lines_.erase(lines_.begin(), lines_.begin() + kMaxLogLines / 4);
            RebuildLog();
            return;
        }
        int count = static_cast<int>(visible_.size());
        bool atEnd = ListView_GetTopIndex(log_) + ListView_GetCountPerPage(log_) >= ListView_GetItemCount(log_) - 1;
        ListView_SetItemCountEx(log_, count, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
        if (atEnd && count > 0) {
            ListView_EnsureVisible(log_, count - 1, FALSE);
        }
    }

    void SetSessionFilter(const std::string& id) {
        sessionFilter_ = id;
        sessionFilterWide_ = Widen(id);
        std::wstring label = id.empty() ? L"Protocol log of all sessions" : L"Protocol log of session " + Widen(id);
        SetWindowTextW(logLabel_, label.c_str());
        UpdateDisconnect();
        RebuildLog();
    }

    void RebuildLog() {
        visible_.clear();
        for (size_t i = 0; i < lines_.size(); i++) {
            if (Matches(lines_[i])) {
                visible_.push_back(i);
            }
        }
        // the selection of a virtual list is by position, it would point at other lines now
        ListView_SetItemState(log_, -1, 0, LVIS_SELECTED);
        ListView_SetItemCountEx(log_, static_cast<int>(visible_.size()), 0);
        if (!visible_.empty()) {
            ListView_EnsureVisible(log_, static_cast<int>(visible_.size()) - 1, FALSE);
        }
        InvalidateRect(log_, nullptr, TRUE);
    }

    void FillLogItem(LVITEMW& item) {
        if (!(item.mask & LVIF_TEXT) || item.iItem < 0 || item.iItem >= static_cast<int>(visible_.size())) {
            return;
        }
        const LogLine& line = lines_[visible_[item.iItem]];
        switch (item.iSubItem) {
            case 0:
                swprintf_s(item.pszText, item.cchTextMax, L"%02d:%02d:%02d", line.time / 3600, line.time / 60 % 60, line.time % 60);
                break;
            case 1:
                wcsncpy_s(item.pszText, item.cchTextMax, line.session.c_str(), _TRUNCATE);
                break;
            case 2:
                wcsncpy_s(item.pszText, item.cchTextMax, line.fromClient ? L"C:" : L"S:", _TRUNCATE);
                break;
            default:
                wcsncpy_s(item.pszText, item.cchTextMax, line.text.c_str(), _TRUNCATE);
        }
    }

    HWND sessions_ = nullptr, log_ = nullptr, filter_ = nullptr, filterLabel_ = nullptr;
    HWND disconnect_ = nullptr, removeClosed_ = nullptr, clearLog_ = nullptr, logLabel_ = nullptr;
    int listHeight_ = 0;
    bool logTimer_ = false;
    std::vector<SessionRow> rows_;
    std::deque<LogLine> lines_;
    std::vector<size_t> visible_;
    std::string sessionFilter_;
    std::wstring sessionFilterWide_;
    std::wstring textFilter_;
};

}  // namespace

Page* CreateConnectionsPage() {
    return new ConnectionsPage();
}
