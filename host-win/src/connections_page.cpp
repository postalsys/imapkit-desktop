// The Connections page: the IMAP and SMTP sessions on top, the protocol log of the selected session (or of all of
// them) below. Each list has its actions in a header row right above it
#include "app.h"

#include <shlwapi.h>

#include <algorithm>
#include <deque>

namespace {

enum ControlId {
    IDC_SESSIONS = 300,
    IDC_LOG,
    IDC_SESSIONS_LABEL,
    IDC_DISCONNECT,
    IDC_INJECT,
    IDC_CLEAR_CLOSED,
    IDC_LOG_LABEL,
    IDC_FILTER_LABEL,
    IDC_FILTER,
    IDC_COPY,
    IDC_SAVE,
    IDC_CLEAR_LOG,
    // menu commands
    IDM_COPY = 350,
    IDM_COPY_DATA,
    IDM_SELECT_ALL,
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

// who a line of the log comes from: the server, the client, or a change made in this app (session "app")
enum class Source { Server, Client, App };

struct LogLine {
    // seconds since local midnight
    int time;
    std::wstring session;
    Source source;
    std::wstring text;
};

const wchar_t* SourceLabel(Source source) {
    return source == Source::Client ? L"C:" : source == Source::App ? L"UI:" : L"S:";
}

// local seconds since midnight of an ISO time
int LocalSeconds(const std::string& iso) {
    std::wstring local = FormatTime(iso, true);
    int hours = 0, minutes = 0, seconds = 0;
    swscanf_s(local.c_str(), L"%d:%d:%d", &hours, &minutes, &seconds);
    return hours * 3600 + minutes * 60 + seconds;
}

std::wstring TimeText(int seconds) {
    wchar_t text[16];
    swprintf_s(text, L"%02d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60);
    return text;
}

class ConnectionsPage : public Page {
public:
    void OnEvent(const std::string& name, const json& data) override {
        if (name == "session") {
            OnSession(data);
        } else if (name == "trace") {
            OnTrace(data);
        } else if (name == "status") {
            UpdateEmptyText(data);
        }
    }

protected:
    void OnCreate() override {
        sessionsLabel_ = AddControl(L"STATIC", L"Sessions", SS_LEFT | SS_CENTERIMAGE, IDC_SESSIONS_LABEL);
        SendMessageW(sessionsLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(BoldFont()), TRUE);
        disconnect_ = AddControl(L"BUTTON", L"&Disconnect", WS_TABSTOP, IDC_DISCONNECT);
        inject_ = AddControl(L"BUTTON", L"Send &Raw Data...", WS_TABSTOP, IDC_INJECT);
        clearClosed_ = AddControl(L"BUTTON", L"Clear Cl&osed", WS_TABSTOP, IDC_CLEAR_CLOSED);

        sessions_ = AddControl(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, IDC_SESSIONS,
                               WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(sessions_, LVS_EX_FULLROWSELECT);
        AddColumns(sessions_, {{L"Session", 90},
                               {L"Client", 140},
                               {L"User", 110},
                               {L"State", 110},
                               {L"Mailbox", 120},
                               {L"TLS", 40},
                               {L"Connected", 70}});
        // the first row stands for every session, it is the default filter of the log
        InsertBlankRow(0);
        ListView_SetItemText(sessions_, 0, 0, const_cast<wchar_t*>(L"All sessions"));
        ListView_SetItemState(sessions_, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);

        logLabel_ = AddControl(L"STATIC", L"Protocol log of all sessions", SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS, IDC_LOG_LABEL);
        SendMessageW(logLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(BoldFont()), TRUE);
        filterLabel_ = AddControl(L"STATIC", L"&Filter:", SS_RIGHT | SS_CENTERIMAGE, IDC_FILTER_LABEL);
        filter_ = AddControl(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, IDC_FILTER, WS_EX_CLIENTEDGE);
        copy_ = AddControl(L"BUTTON", L"&Copy", WS_TABSTOP, IDC_COPY);
        save_ = AddControl(L"BUTTON", L"&Save...", WS_TABSTOP, IDC_SAVE);
        clearLog_ = AddControl(L"BUTTON", L"C&lear", WS_TABSTOP, IDC_CLEAR_LOG);

        log_ = AddControl(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS, IDC_LOG, WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(log_, LVS_EX_FULLROWSELECT);
        SendMessageW(log_, WM_SETFONT, reinterpret_cast<WPARAM>(MonoFont()), TRUE);
        AddColumns(log_, {{L"Time", 70}, {L"Session", 70}, {L"", 36}, {L"Data", 900}});
        UpdateEmptyText(json::object());
        UpdateButtons();

        splitters_.push_back({false, {}, [this](int y) { listHeight_ = y - listTop_; }});
    }

    void OnSize(int width, int height) override {
        if (width == 0 || height == 0) {
            // created before the main window gave it a size, the split is set on the first real size
            return;
        }
        int pad = Scale(6);
        int bar = Scale(25);
        int button = Scale(110);

        // sessions: a header row with its actions, then the list
        int y = pad;
        MoveWindow(sessionsLabel_, pad, y, Scale(200), bar, TRUE);
        MoveWindow(clearClosed_, width - pad - button, y, button, bar, TRUE);
        MoveWindow(inject_, width - 2 * (pad + button), y, button, bar, TRUE);
        MoveWindow(disconnect_, width - 3 * (pad + button), y, button, bar, TRUE);
        y += bar + pad;
        listTop_ = y;
        if (listHeight_ == 0) {
            listHeight_ = (height - y) * 30 / 100;
        }
        listHeight_ = std::clamp(listHeight_, Scale(60), std::max(Scale(60), height - y - Scale(160)));
        MoveWindow(sessions_, pad, y, width - 2 * pad, listHeight_, TRUE);
        StretchColumn(sessions_, 6);
        y += listHeight_;
        splitters_[0].area = {0, y, width, y + 2 * pad};
        y += 2 * pad;

        // log: a header row with the filter and its actions, then the log
        int narrow = Scale(70);
        int filterWidth = Scale(180);
        int right = width - pad;
        MoveWindow(clearLog_, right - narrow, y, narrow, bar, TRUE);
        MoveWindow(save_, right - 2 * narrow - pad, y, narrow, bar, TRUE);
        MoveWindow(copy_, right - 3 * narrow - 2 * pad, y, narrow, bar, TRUE);
        int filterRight = right - 3 * narrow - 4 * pad;
        MoveWindow(filter_, filterRight - filterWidth, y + Scale(2), filterWidth, Scale(21), TRUE);
        MoveWindow(filterLabel_, filterRight - filterWidth - Scale(46), y, Scale(42), bar, TRUE);
        MoveWindow(logLabel_, pad, y, std::max(0, filterRight - filterWidth - Scale(56) - pad), bar, TRUE);
        y += bar + pad;
        MoveWindow(log_, pad, y, width - 2 * pad, height - y - pad, TRUE);
        StretchColumn(log_, 3, 900);
    }

    void OnCommand(int id, int code, HWND) override {
        if (id == IDC_FILTER && code == EN_CHANGE) {
            SetTimer(hwnd_, TIMER_FILTER, 200, nullptr);
            return;
        }
        switch (id) {
            case IDC_DISCONNECT:
                if (!sessionFilter_.empty()) {
                    TheSidecar().Call("disconnect", {{"id", sessionFilter_}});
                }
                break;
            case IDC_INJECT:
                Inject();
                break;
            case IDC_CLEAR_CLOSED:
                std::erase_if(rows_, [](const SessionRow& row) { return row.closed; });
                RebuildSessions();
                break;
            case IDC_CLEAR_LOG:
                lines_.clear();
                RebuildLog();
                break;
            case IDC_COPY:
                // the selected lines, or every line shown
                CopyToClipboard(hwnd_, LogText(ListView_GetSelectedCount(log_) ? SelectedRows() : AllRows(), true));
                break;
            case IDM_COPY:
            case IDM_COPY_DATA:
                CopyToClipboard(hwnd_, LogText(SelectedRows(), id == IDM_COPY));
                break;
            case IDM_SELECT_ALL:
                ListView_SetItemState(log_, -1, LVIS_SELECTED, LVIS_SELECTED);
                break;
            case IDC_SAVE:
                SaveLog();
                break;
            case IDM_COPY_SESSION_LOG:
                CopyToClipboard(hwnd_, LogText(AllRows(), true));
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
                        if (index < visible_.size()) {
                            // client commands in blue, like the C: lines of a protocol log, and changes made here in green
                            Source source = lines_[visible_[index]].source;
                            if (source == Source::Client) {
                                draw->clrText = RGB(0, 0, 160);
                            } else if (source == Source::App) {
                                draw->clrText = RGB(0, 110, 0);
                            }
                        }
                    }
                    return CDRF_DODEFAULT;
                }
            }
        }
        if (header->hwndFrom == sessions_) {
            if (header->code == NM_CUSTOMDRAW) {
                auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(header);
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
                    return CDRF_NOTIFYITEMDRAW;
                }
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    int row = static_cast<int>(draw->nmcd.dwItemSpec);
                    if (row == 0) {
                        SelectObject(draw->nmcd.hdc, BoldFont());
                        return CDRF_NEWFONT;
                    }
                    // closed sessions stay for their log, grayed out
                    if (row - 1 < static_cast<int>(rows_.size()) && rows_[row - 1].closed) {
                        draw->clrText = GetSysColor(COLOR_GRAYTEXT);
                    }
                }
                return CDRF_DODEFAULT;
            }
            if (header->code == LVN_ITEMCHANGED) {
                auto* change = reinterpret_cast<NMLISTVIEW*>(header);
                if ((change->uChanged & LVIF_STATE) && ((change->uNewState ^ change->uOldState) & LVIS_SELECTED)) {
                    const SessionRow* row = RowAt(ListView_GetNextItem(sessions_, -1, LVNI_SELECTED));
                    SetSessionFilter(row ? row->id : "");
                }
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
    // the session of a row of the session list, the first row is All sessions
    const SessionRow* RowAt(int row) const { return row >= 1 && row - 1 < static_cast<int>(rows_.size()) ? &rows_[row - 1] : nullptr; }

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
        AppendMenuW(menu, MF_STRING | (selected ? 0 : MF_GRAYED), IDM_COPY_DATA, L"Copy &Data Only");
        AppendMenuW(menu, MF_STRING, IDM_SELECT_ALL, L"Select &All\tCtrl+A");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (visible_.empty() ? MF_GRAYED : 0), IDC_SAVE, L"&Save Log...");
        AppendMenuW(menu, MF_STRING, IDC_CLEAR_LOG, L"C&lear Log");
        TrackContextMenu(menu, hwnd_, log_, at, FocusedRect(log_));
    }

    void ShowSessionMenu(POINT at) {
        const SessionRow* row = SelectedSession();
        bool open = row && !row->closed;
        bool imap = row && row->info.value("kind", "") == "imap";
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (open ? 0 : MF_GRAYED), IDC_DISCONNECT, L"&Disconnect");
        AppendMenuW(menu, MF_STRING | (open && imap ? 0 : MF_GRAYED), IDC_INJECT, L"Send &Raw Data...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (visible_.empty() ? MF_GRAYED : 0), IDM_COPY_SESSION_LOG, L"&Copy Log");
        TrackContextMenu(menu, hwnd_, sessions_, at, FocusedRect(sessions_));
    }

    const SessionRow* SelectedSession() {
        auto found = std::find_if(rows_.begin(), rows_.end(), [this](const SessionRow& row) { return row.id == sessionFilter_; });
        return found == rows_.end() ? nullptr : &*found;
    }

    // the session actions apply to the selected session, Send Raw Data to IMAP sessions only
    void UpdateButtons() {
        const SessionRow* row = SelectedSession();
        bool open = row && !row->closed;
        EnableWindow(disconnect_, open);
        EnableWindow(inject_, open && row->info.value("kind", "") == "imap");
        EnableWindow(clearClosed_, std::any_of(rows_.begin(), rows_.end(), [](const SessionRow& entry) { return entry.closed; }));
    }

    // what to do while the log is empty: connect a client to the server
    void UpdateEmptyText(const json& status) {
        std::wstring text = L"No traffic yet.";
        if (status.value("running", false)) {
            std::wstring host = JsonText(status, "host");
            if (host == L"127.0.0.1" || host == L"0.0.0.0") {
                host = L"localhost";
            }
            text += L" Point an email client to " + host + L", IMAP port " + std::to_wstring(status.value("imapPort", 0));
            if (status.value("smtpPort", 0)) {
                text += L" or SMTP port " + std::to_wstring(status.value("smtpPort", 0));
            }
            text += L". The Server page has the complete client settings.";
        } else if (status.contains("running")) {
            text += L" The server is stopped, start it on the Server page.";
        }
        SetEmptyText(log_, text);
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

    // the rows as text: "time session C: data" per line, or the protocol data alone
    std::wstring LogText(const std::vector<int>& rows, bool withPrefix) {
        std::wstring text;
        for (int row : rows) {
            const LogLine& line = lines_[visible_[row]];
            if (withPrefix) {
                text += TimeText(line.time) + L"  " + line.session + L"  " + SourceLabel(line.source) + L" ";
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
        if (index + 1 >= ListView_GetItemCount(sessions_)) {
            InsertBlankRow(index + 1);
            // a vertical scroll bar may have come with the row
            StretchColumn(sessions_, 6);
        }
        UpdateSessionRow(index);
        UpdateSessionCount();
        UpdateButtons();
    }

    void UpdateSessionRow(int index) {
        const SessionRow& row = rows_[index];
        const json& info = row.info;
        std::wstring kind = JsonText(info, "kind");
        std::transform(kind.begin(), kind.end(), kind.begin(), towupper);
        std::wstring texts[] = {kind + L" " + std::to_wstring(info.value("number", 0)),
                                JsonText(info, "remoteAddress"),
                                JsonText(info, "user"),
                                row.closed ? L"Closed" : JsonText(info, "state"),
                                JsonText(info, "mailbox"),
                                info.value("secure", false) ? L"Yes" : L"No",
                                FormatTime(JsonString(info, "started"), true)};
        for (int column = 0; column < 7; column++) {
            ListView_SetItemText(sessions_, index + 1, column, texts[column].data());
        }
    }

    void RebuildSessions() {
        // everything but the All sessions row
        while (ListView_GetItemCount(sessions_) > 1) {
            ListView_DeleteItem(sessions_, 1);
        }
        for (int i = 0; i < static_cast<int>(rows_.size()); i++) {
            InsertBlankRow(i + 1);
            UpdateSessionRow(i);
        }
        ListView_SetItemState(sessions_, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        SetSessionFilter("");
        UpdateSessionCount();
    }

    void UpdateSessionCount() {
        auto open = std::count_if(rows_.begin(), rows_.end(), [](const SessionRow& row) { return !row.closed; });
        SetStatus(STATUS_SESSIONS, L"Connected sessions: " + std::to_wstring(open));
        SetWindowTextW(sessionsLabel_, (L"Sessions (" + std::to_wstring(open) + L" connected)").c_str());
    }

    bool Matches(const LogLine& line) {
        // a case-insensitive search that copies nothing, the filter runs over every line of the log
        return (sessionFilterWide_.empty() || line.session == sessionFilterWide_) &&
               (textFilter_.empty() || StrStrIW(line.text.c_str(), textFilter_.c_str()) != nullptr);
    }

    void OnTrace(const json& data) {
        std::wstring session = JsonText(data, "id");
        std::string direction = JsonString(data, "direction");
        Source source = direction == "in" ? Source::Client : direction == "ui" ? Source::App : Source::Server;
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
            lines_.push_back({time, session, source, text.substr(start, end - start)});
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
        std::wstring label = id.empty() ? L"Protocol log of all sessions" : L"Protocol log of " + Widen(id);
        SetWindowTextW(logLabel_, label.c_str());
        UpdateButtons();
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
                wcsncpy_s(item.pszText, item.cchTextMax, TimeText(line.time).c_str(), _TRUNCATE);
                break;
            case 1:
                wcsncpy_s(item.pszText, item.cchTextMax, line.session.c_str(), _TRUNCATE);
                break;
            case 2:
                wcsncpy_s(item.pszText, item.cchTextMax, SourceLabel(line.source), _TRUNCATE);
                break;
            default:
                wcsncpy_s(item.pszText, item.cchTextMax, line.text.c_str(), _TRUNCATE);
        }
    }

    HWND sessionsLabel_ = nullptr, disconnect_ = nullptr, inject_ = nullptr, clearClosed_ = nullptr, sessions_ = nullptr;
    HWND logLabel_ = nullptr, filterLabel_ = nullptr, filter_ = nullptr, copy_ = nullptr, save_ = nullptr, clearLog_ = nullptr, log_ = nullptr;
    int listHeight_ = 0, listTop_ = 0;
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
