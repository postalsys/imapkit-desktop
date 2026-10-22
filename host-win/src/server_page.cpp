// The Server page. A status banner says whether the server runs and where, with Start/Stop and Apply. The left
// column has what most tests need: where the server listens, the test accounts, and the settings to type into an
// email client. The right column has the advanced tools of ImapKit on tabs: IMAP extensions, quirks, fault scripts
// and the REST API. Mailbox import, export and reset are in the File menu
#include "app.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <set>

namespace {

enum ControlId {
    IDC_TOGGLE = 200,
    IDC_APPLY,
    IDC_HOST,
    IDC_IMAP_PORT,
    IDC_IMAPS_ON,
    IDC_IMAPS_PORT,
    IDC_SMTP_ON,
    IDC_SMTP_PORT,
    IDC_USERS,
    IDC_USER_ADD,
    IDC_USER_EDIT,
    IDC_USER_REMOVE,
    IDC_CLIENT_SETTINGS,
    IDC_COPY_SETTINGS,
    IDC_ADVANCED,
    IDC_PLUGINS,
    IDC_PLUGIN_DEFAULTS,
    IDC_QUIRKS,
    IDC_PLUGIN_HELP,
    IDC_QUIRK_HELP,
    IDC_SCRIPT_RULES,
    IDC_SCRIPT_LOAD,
    IDC_SCRIPT_CLEAR,
    IDC_SCRIPT_DOCS,
    IDC_REST_ENABLED,
    IDC_REST_PORT,
    IDC_REST_TOKEN,
    IDC_REST_URL,
    IDC_REST_COPY,
    IDC_REST_DOCS,
    IDC_ID_ENABLED,
    // the edit of each field of kIdFields, in order
    IDC_ID_FIELD = 260,
    // the edit of each field of kLimits, in order
    IDC_LIMIT_FIELD = 280,
    IDC_QUOTA_SOFT = 290,
    IDC_SMTP_REQUIRE_AUTH,
    IDC_SMTP_MAX_SIZE,
    IDC_SMTP_REJECT,
};

enum AdvancedTab { TAB_EXTENSIONS, TAB_ID, TAB_LIMITS, TAB_QUIRKS, TAB_SCRIPTS, TAB_SMTP, TAB_REST, TAB_COUNT };

// the fields of the ID response that RFC 2971 section 3.3 defines
const char* kIdFields[] = {"name", "version", "os", "os-version", "vendor", "support-url", "address", "date", "command", "arguments", "environment"};
constexpr int kIdFieldCount = static_cast<int>(std::size(kIdFields));

// the fields of the Limits tab: the key in config.limits, and the extensions that make the limit work
struct Limit {
    const char* key;
    const wchar_t* label;
    const wchar_t* unit;
    std::initializer_list<const char*> plugins;
};
const Limit kLimits[] = {
    {"appendLimit", L"Largest message:", L"bytes", {"APPENDLIMIT"}},
    {"quotaStorage", L"Quota, storage:", L"KB", {"QUOTA"}},
    {"quotaMessages", L"Quota, messages:", L"messages", {"QUOTA"}},
    {"messageLimit", L"Messages per command:", L"default 1000", {"MESSAGELIMIT", "SAVELIMIT"}},
    {"maxLiteralSize", L"Largest literal:", L"bytes, default 67108864", {}},
};
constexpr int kLimitCount = static_cast<int>(std::size(kLimits));
// the label and field widths of the rows of the Limits and SMTP tabs, at 96 DPI
constexpr int kRowLabel = 130;
constexpr int kRowEdit = 110;

const wchar_t* kJsonFilter = L"JSON files (*.json)\0*.json\0All files (*.*)\0*.*\0";
const wchar_t* kHostItems[] = {L"127.0.0.1 (this computer only)", L"0.0.0.0 (all network interfaces)"};

struct User {
    std::wstring name;
    std::wstring password;
};

// the host name an email client uses for an address the server listens on
std::wstring ClientHost(const std::wstring& address) {
    if (address.empty() || address == L"127.0.0.1" || address == L"::1" || address == L"localhost") {
        return L"localhost";
    }
    if (address == L"0.0.0.0" || address == L"::") {
        wchar_t name[256];
        DWORD size = 256;
        return GetComputerNameExW(ComputerNameDnsHostname, name, &size) ? name : L"this computer";
    }
    return address;
}

class ServerPage : public Page {
public:
    // the group boxes draw only their frame, the page paints under them
    bool ClipChildren() const override { return false; }

    void OnEvent(const std::string& name, const json& data) override {
        if (name == "ready") {
            TheSidecar().Call("hello", json::object(), [this](const json& result, const std::string& error) {
                if (error.empty()) {
                    plugins_ = result.value("plugins", json::array());
                    quirks_ = result.value("quirks", json::array());
                    defaults_ = result.value("defaults", json::object());
                    LoadConfig(result["config"]);
                    ShowState(result["status"]);
                }
            });
        } else if (name == "status") {
            ShowState(data);
        }
    }

protected:
    void OnCreate() override {
        // the banner is painted, its buttons sit on it
        toggle_ = AddControl(L"BUTTON", L"&Start", WS_TABSTOP | BS_DEFPUSHBUTTON, IDC_TOGGLE);
        apply_ = AddControl(L"BUTTON", L"&Apply Changes", WS_TABSTOP, IDC_APPLY);
        EnableWindow(apply_, FALSE);

        connectionGroup_ = AddControl(L"BUTTON", L"Connection", BS_GROUPBOX, 0);
        hostLabel_ = AddControl(L"STATIC", L"&Listen on:", SS_LEFT, 0);
        host_ = AddControl(WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL, IDC_HOST);
        for (const wchar_t* item : kHostItems) {
            ComboBox_AddString(host_, item);
        }
        imapLabel_ = AddControl(L"STATIC", L"&IMAP port (STARTTLS):", SS_LEFT, 0);
        imapPort_ = AddControl(L"EDIT", L"", WS_TABSTOP | ES_NUMBER, IDC_IMAP_PORT, WS_EX_CLIENTEDGE);
        imapsOn_ = AddControl(L"BUTTON", L"IMAP&S port (SSL/TLS):", WS_TABSTOP | BS_AUTOCHECKBOX, IDC_IMAPS_ON);
        imapsPort_ = AddControl(L"EDIT", L"", WS_TABSTOP | ES_NUMBER, IDC_IMAPS_PORT, WS_EX_CLIENTEDGE);
        smtpOn_ = AddControl(L"BUTTON", L"S&MTP port:", WS_TABSTOP | BS_AUTOCHECKBOX, IDC_SMTP_ON);
        smtpPort_ = AddControl(L"EDIT", L"", WS_TABSTOP | ES_NUMBER, IDC_SMTP_PORT, WS_EX_CLIENTEDGE);

        usersGroup_ = AddControl(L"BUTTON", L"Test accounts", BS_GROUPBOX, 0);
        usersList_ = AddControl(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, IDC_USERS, WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(usersList_, LVS_EX_FULLROWSELECT);
        AddColumns(usersList_, {{L"User name", 140}, {L"Password", 130}});
        userAdd_ = AddControl(L"BUTTON", L"A&dd...", WS_TABSTOP, IDC_USER_ADD);
        userEdit_ = AddControl(L"BUTTON", L"&Edit...", WS_TABSTOP, IDC_USER_EDIT);
        userRemove_ = AddControl(L"BUTTON", L"&Remove", WS_TABSTOP, IDC_USER_REMOVE);

        clientGroup_ = AddControl(L"BUTTON", L"Email client settings", BS_GROUPBOX, 0);
        clientSettings_ = AddControl(L"EDIT", L"", WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, IDC_CLIENT_SETTINGS,
                                     WS_EX_CLIENTEDGE);
        copySettings_ = AddControl(L"BUTTON", L"&Copy", WS_TABSTOP, IDC_COPY_SETTINGS);

        advanced_ = AddControl(WC_TABCONTROLW, L"", WS_TABSTOP | WS_CLIPSIBLINGS, IDC_ADVANCED);
        const wchar_t* tabNames[] = {L"IMAP Extensions", L"Server ID", L"Limits", L"Quirks", L"Fault Scripts", L"SMTP", L"REST API"};
        for (int i = 0; i < TAB_COUNT; i++) {
            TCITEMW item = {};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(tabNames[i]);
            TabCtrl_InsertItem(advanced_, i, &item);
        }

        auto& extensions = tabs_[TAB_EXTENSIONS];
        extensions.push_back(pluginsHint_ = AddControl(L"STATIC", L"The capabilities the server advertises. Turn off what a client must cope without.",
                                                       SS_LEFT, 0));
        extensions.push_back(pluginsList_ = AddCheckList(IDC_PLUGINS));
        extensions.push_back(pluginHelp_ = AddControl(L"STATIC", L"Select an extension to see what it does.", SS_LEFT, IDC_PLUGIN_HELP, WS_EX_STATICEDGE));
        extensions.push_back(pluginDefaults_ = AddControl(L"BUTTON", L"Restore &Defaults", WS_TABSTOP, IDC_PLUGIN_DEFAULTS));

        auto& id = tabs_[TAB_ID];
        id.push_back(idHint_ = AddControl(L"STATIC",
                                          L"What the server answers to the ID command of a client (RFC 2971). Clients may change "
                                          L"what they do for a server they know.",
                                          SS_LEFT, 0));
        id.push_back(idEnabled_ = AddControl(L"BUTTON", L"Answer ID with these &values (empty fields are left out), not with ID NIL",
                                             WS_TABSTOP | BS_AUTOCHECKBOX, IDC_ID_ENABLED));
        for (int i = 0; i < kIdFieldCount; i++) {
            id.push_back(idLabels_[i] = AddControl(L"STATIC", (Widen(kIdFields[i]) + L":").c_str(), SS_LEFT, 0));
            id.push_back(idEdits_[i] = AddControl(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, IDC_ID_FIELD + i, WS_EX_CLIENTEDGE));
        }

        auto& limits = tabs_[TAB_LIMITS];
        limits.push_back(limitsHint_ = AddControl(L"STATIC",
                                                  L"Limits make the server refuse what goes over them, to test how a client copes. "
                                                  L"An empty field sets no limit. Most need their extension turned on.",
                                                  SS_LEFT, 0));
        for (int i = 0; i < kLimitCount; i++) {
            limits.push_back(limitLabels_[i] = AddControl(L"STATIC", kLimits[i].label, SS_LEFT, 0));
            limits.push_back(limitEdits_[i] = AddControl(L"EDIT", L"", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, IDC_LIMIT_FIELD + i, WS_EX_CLIENTEDGE));
            limits.push_back(limitNotes_[i] = AddControl(L"STATIC", kLimits[i].unit, SS_LEFT | SS_ENDELLIPSIS, 0));
        }
        limits.push_back(quotaSoft_ = AddControl(L"BUTTON", L"Quota only &warns (soft limit, an untagged NO [OVERQUOTA])", WS_TABSTOP | BS_AUTOCHECKBOX,
                                                 IDC_QUOTA_SOFT));

        auto& smtp = tabs_[TAB_SMTP];
        smtp.push_back(smtpHint_ = AddControl(L"STATIC", L"Rules of the SMTP server, to test how a client handles refused mail.", SS_LEFT, 0));
        smtp.push_back(smtpRequireAuth_ = AddControl(L"BUTTON", L"Require a &login to send mail (530 without, so no mail counts as incoming)",
                                                     WS_TABSTOP | BS_AUTOCHECKBOX, IDC_SMTP_REQUIRE_AUTH));
        smtp.push_back(smtpMaxSizeLabel_ = AddControl(L"STATIC", L"Largest message:", SS_LEFT, 0));
        smtp.push_back(smtpMaxSize_ = AddControl(L"EDIT", L"", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, IDC_SMTP_MAX_SIZE, WS_EX_CLIENTEDGE));
        smtp.push_back(smtpMaxSizeNote_ = AddControl(L"STATIC", L"bytes, advertised with SIZE, larger mail gets 552. Empty: any size", SS_LEFT | SS_ENDELLIPSIS, 0));
        smtp.push_back(smtpRejectLabel_ = AddControl(L"STATIC",
                                                     L"Rejected recipients, one per line: the address (* matches anything), then the reply code "
                                                     L"(550 when left out) and its text, e.g. *@blocked.test 550 No such user",
                                                     SS_LEFT, 0));
        smtp.push_back(smtpReject_ = AddControl(L"EDIT", L"", WS_TABSTOP | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL,
                                                IDC_SMTP_REJECT, WS_EX_CLIENTEDGE));
        SendMessageW(smtpReject_, WM_SETFONT, reinterpret_cast<WPARAM>(MonoFont()), TRUE);

        auto& quirks = tabs_[TAB_QUIRKS];
        quirks.push_back(quirksHint_ = AddControl(L"STATIC", L"Make the server behave like a known real server, to test how a client copes with it.",
                                                  SS_LEFT, 0));
        quirks.push_back(quirksList_ = AddCheckList(IDC_QUIRKS));
        quirks.push_back(quirkHelp_ = AddControl(L"STATIC", L"Select a quirk to see what it does.", SS_LEFT, IDC_QUIRK_HELP, WS_EX_STATICEDGE));

        auto& scripts = tabs_[TAB_SCRIPTS];
        scripts.push_back(scriptsHint_ = AddControl(L"STATIC",
                                                    L"Script rules make the server misbehave on purpose: refuse or delay a command, cut a response short, "
                                                    L"drop the connection. Load them from a JSON file.",
                                                    SS_LEFT, 0));
        scripts.push_back(scriptRules_ = AddControl(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL,
                                                    IDC_SCRIPT_RULES, WS_EX_CLIENTEDGE));
        AddColumns(scriptRules_, {{L"", 900}});
        SendMessageW(scriptRules_, WM_SETFONT, reinterpret_cast<WPARAM>(MonoFont()), TRUE);
        SetEmptyText(scriptRules_, L"No script rules, the server follows the protocol.");
        scripts.push_back(scriptLoad_ = AddControl(L"BUTTON", L"L&oad...", WS_TABSTOP, IDC_SCRIPT_LOAD));
        scripts.push_back(scriptClear_ = AddControl(L"BUTTON", L"Clea&r", WS_TABSTOP, IDC_SCRIPT_CLEAR));
        scripts.push_back(scriptDocs_ = AddControl(L"BUTTON", L"Rule &Reference", WS_TABSTOP, IDC_SCRIPT_DOCS));

        auto& rest = tabs_[TAB_REST];
        rest.push_back(restHint_ = AddControl(L"STATIC",
                                              L"The control API over HTTP, for test suites in any language: add messages, change flags, "
                                              L"disconnect sessions and follow events while a client runs.",
                                              SS_LEFT, 0));
        rest.push_back(restEnabled_ = AddControl(L"BUTTON", L"E&nable the REST API", WS_TABSTOP | BS_AUTOCHECKBOX, IDC_REST_ENABLED));
        rest.push_back(restPortLabel_ = AddControl(L"STATIC", L"Port:", SS_LEFT, 0));
        rest.push_back(restPort_ = AddControl(L"EDIT", L"", WS_TABSTOP | ES_NUMBER, IDC_REST_PORT, WS_EX_CLIENTEDGE));
        rest.push_back(restTokenLabel_ = AddControl(L"STATIC", L"Token (optional):", SS_LEFT, 0));
        rest.push_back(restToken_ = AddControl(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, IDC_REST_TOKEN, WS_EX_CLIENTEDGE));
        rest.push_back(restUrlLabel_ = AddControl(L"STATIC", L"Address:", SS_LEFT, 0));
        rest.push_back(restUrl_ = AddControl(L"EDIT", L"", ES_READONLY | ES_AUTOHSCROLL, IDC_REST_URL, WS_EX_CLIENTEDGE));
        rest.push_back(restCopy_ = AddControl(L"BUTTON", L"Cop&y", WS_TABSTOP, IDC_REST_COPY));
        rest.push_back(restDocs_ = AddControl(L"BUTTON", L"API &Reference", WS_TABSTOP, IDC_REST_DOCS));

        // the tab control under the controls of its tabs
        SetWindowPos(advanced_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        ShowTab(TAB_EXTENSIONS);
        UpdateEnabled();
    }

    void OnSize(int width, int height) override {
        if (width == 0 || height == 0) {
            return;
        }
        int pad = Scale(8);
        int row = Scale(26);
        int caption = Scale(18);
        int button = Scale(100);

        banner_ = {pad, pad, width - pad, pad + Scale(52)};
        MoveWindow(toggle_, banner_.right - pad - button, banner_.top + Scale(14), button, Scale(25), TRUE);
        MoveWindow(apply_, banner_.right - 2 * (pad + button), banner_.top + Scale(14), button, Scale(25), TRUE);

        int top = banner_.bottom + pad;
        int left = pad;
        int columnWidth = Scale(340);
        int labelWidth = Scale(150);
        int fieldLeft = left + pad + labelWidth;
        int fieldWidth = columnWidth - labelWidth - 2 * pad;

        MoveWindow(connectionGroup_, left, top, columnWidth, caption + 4 * row + pad, TRUE);
        int y = top + caption;
        MoveWindow(hostLabel_, left + pad, y + Scale(4), labelWidth, Scale(16), TRUE);
        MoveWindow(host_, fieldLeft, y, fieldWidth, Scale(200), TRUE);
        y += row;
        MoveWindow(imapLabel_, left + pad, y + Scale(4), labelWidth, Scale(16), TRUE);
        MoveWindow(imapPort_, fieldLeft, y, Scale(70), Scale(21), TRUE);
        y += row;
        MoveWindow(imapsOn_, left + pad, y + Scale(2), labelWidth, Scale(18), TRUE);
        MoveWindow(imapsPort_, fieldLeft, y, Scale(70), Scale(21), TRUE);
        y += row;
        MoveWindow(smtpOn_, left + pad, y + Scale(2), labelWidth, Scale(18), TRUE);
        MoveWindow(smtpPort_, fieldLeft, y, Scale(70), Scale(21), TRUE);
        top += caption + 4 * row + 2 * pad;

        int listHeight = Scale(64);
        int usersHeight = caption + listHeight + Scale(32) + pad;
        MoveWindow(usersGroup_, left, top, columnWidth, usersHeight, TRUE);
        MoveWindow(usersList_, left + pad, top + caption, columnWidth - 2 * pad, listHeight, TRUE);
        StretchColumn(usersList_, 1);
        int narrow = Scale(80);
        int buttonTop = top + caption + listHeight + Scale(6);
        MoveWindow(userAdd_, left + pad, buttonTop, narrow, Scale(23), TRUE);
        MoveWindow(userEdit_, left + pad + narrow + Scale(4), buttonTop, narrow, Scale(23), TRUE);
        MoveWindow(userRemove_, left + pad + 2 * (narrow + Scale(4)), buttonTop, narrow, Scale(23), TRUE);
        top += usersHeight + pad;

        int clientHeight = std::max(Scale(120), height - top - pad);
        MoveWindow(clientGroup_, left, top, columnWidth, clientHeight, TRUE);
        MoveWindow(clientSettings_, left + pad, top + caption, columnWidth - 2 * pad, clientHeight - caption - Scale(36), TRUE);
        MoveWindow(copySettings_, left + columnWidth - pad - narrow, top + clientHeight - Scale(31), narrow, Scale(23), TRUE);

        RECT tabs = {left + columnWidth + pad, banner_.bottom + pad, width - pad, height - pad};
        MoveWindow(advanced_, tabs.left, tabs.top, tabs.right - tabs.left, tabs.bottom - tabs.top, TRUE);
        RECT area = tabs;
        TabCtrl_AdjustRect(advanced_, FALSE, &area);
        InflateRect(&area, -Scale(6), -Scale(6));
        LayoutTabs(area);
        InvalidateRect(hwnd_, &banner_, TRUE);
    }

    void OnCommand(int id, int code, HWND) override {
        // edits of the settings, they apply on the next start
        bool idField = id >= IDC_ID_FIELD && id < IDC_ID_FIELD + kIdFieldCount;
        bool limitField = id >= IDC_LIMIT_FIELD && id < IDC_LIMIT_FIELD + kLimitCount;
        if (code == EN_CHANGE &&
            (idField || limitField || id == IDC_SMTP_MAX_SIZE || id == IDC_SMTP_REJECT || id == IDC_IMAP_PORT || id == IDC_IMAPS_PORT || id == IDC_SMTP_PORT || id == IDC_REST_PORT || id == IDC_REST_TOKEN)) {
            SettingsChanged();
            return;
        }
        if (id == IDC_HOST && (code == CBN_EDITCHANGE || code == CBN_SELCHANGE)) {
            // after a selection the combo box has the new text only after this notification
            PostMessageW(hwnd_, WM_APP, 0, 0);
            return;
        }
        if (code != BN_CLICKED) {
            return;
        }
        switch (id) {
            case IDC_TOGGLE:
                if (running_) {
                    TheSidecar().Call("stop");
                } else {
                    Start();
                }
                break;
            case IDC_APPLY:
                Start();
                break;
            case IDC_IMAPS_ON:
            case IDC_SMTP_ON:
            case IDC_REST_ENABLED:
            case IDC_ID_ENABLED:
            case IDC_QUOTA_SOFT:
            case IDC_SMTP_REQUIRE_AUTH:
                UpdateEnabled();
                SettingsChanged();
                break;
            case IDC_USER_ADD:
                EditUser(-1);
                break;
            case IDC_USER_EDIT:
                EditUser(ListView_GetNextItem(usersList_, -1, LVNI_SELECTED));
                break;
            case IDC_USER_REMOVE: {
                int selected = ListView_GetNextItem(usersList_, -1, LVNI_SELECTED);
                if (selected >= 0 && users_.size() > 1) {
                    users_.erase(users_.begin() + selected);
                    FillUsers();
                    SettingsChanged();
                }
                break;
            }
            case IDC_COPY_SETTINGS:
                CopyToClipboard(hwnd_, GetWindowString(clientSettings_));
                SetStatus(STATUS_INFO, L"Email client settings copied");
                break;
            case IDC_PLUGIN_DEFAULTS:
                FillCheckList(pluginsList_, plugins_, NameSet(defaults_.value("plugins", json::array())));
                SettingsChanged();
                break;
            case IDC_SCRIPT_LOAD:
                LoadScript();
                break;
            case IDC_SCRIPT_CLEAR:
                script_ = json::array();
                ShowScript();
                SettingsChanged();
                break;
            case IDC_SCRIPT_DOCS:
                ShellExecuteW(nullptr, L"open", L"https://imapkit.com/docs/faults/scripted-faults", nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case IDC_REST_COPY:
                CopyToClipboard(hwnd_, GetWindowString(restUrl_));
                SetStatus(STATUS_INFO, L"REST API address copied");
                break;
            case IDC_REST_DOCS:
                ShellExecuteW(nullptr, L"open", L"https://imapkit.com/docs/rest-api/overview", nullptr, nullptr, SW_SHOWNORMAL);
                break;
        }
    }

    LRESULT OnNotify(NMHDR* header) override {
        if (header->hwndFrom == advanced_ && header->code == TCN_SELCHANGE) {
            ShowTab(TabCtrl_GetCurSel(advanced_));
            return 0;
        }
        if (header->code == NM_DBLCLK && header->hwndFrom == usersList_) {
            EditUser(ListView_GetNextItem(usersList_, -1, LVNI_SELECTED));
            return 0;
        }
        if (header->code != LVN_ITEMCHANGED || (header->hwndFrom != pluginsList_ && header->hwndFrom != quirksList_)) {
            return 0;
        }
        auto* change = reinterpret_cast<NMLISTVIEW*>(header);
        bool plugin = header->hwndFrom == pluginsList_;
        // a checkbox is the state image of the item
        if ((change->uChanged & LVIF_STATE) && ((change->uNewState ^ change->uOldState) & LVIS_STATEIMAGEMASK)) {
            SettingsChanged();
        }
        // the help text of the extension or quirk, they describe themselves with different keys
        const json& items = plugin ? plugins_ : quirks_;
        if ((change->uNewState & LVIS_SELECTED) && change->iItem >= 0 && change->iItem < static_cast<int>(items.size())) {
            const json& item = items[change->iItem];
            SetWindowTextW(plugin ? pluginHelp_ : quirkHelp_, (JsonText(item, "name") + L": " + JsonText(item, plugin ? "help" : "description")).c_str());
        }
        return 0;
    }

    LRESULT OnMessage(UINT message, WPARAM, LPARAM, bool& handled) override {
        if (message == WM_APP) {
            SettingsChanged();
            return 0;
        }
        if (message == WM_PAINT) {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(hwnd_, &paint);
            PaintBanner(dc);
            EndPaint(hwnd_, &paint);
            return 0;
        }
        handled = false;
        return 0;
    }

private:
    HWND AddCheckList(int id) {
        HWND list = AddControl(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER, id,
                               WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        AddColumns(list, {{L"", 160}});
        return list;
    }

    void ShowTab(int index) {
        for (int i = 0; i < TAB_COUNT; i++) {
            for (HWND control : tabs_[i]) {
                ShowWindow(control, i == index ? SW_SHOW : SW_HIDE);
            }
        }
    }

    // limits: hint, a row per limit with its unit and whether its extension is on, the soft quota switch
    void LayoutLimitsTab(const RECT& area) {
        int width = area.right - area.left;
        int pad = Scale(6);
        int label = Scale(kRowLabel);
        int field = Scale(kRowEdit);
        int hint = Scale(30);
        MoveWindow(limitsHint_, area.left, area.top, width, hint, TRUE);
        for (int i = 0; i < kLimitCount; i++) {
            int y = area.top + hint + pad + i * Scale(27);
            MoveWindow(limitLabels_[i], area.left, y + Scale(3), label, Scale(16), TRUE);
            MoveWindow(limitEdits_[i], area.left + label, y, field, Scale(21), TRUE);
            MoveWindow(limitNotes_[i], area.left + label + field + pad, y + Scale(3), std::max(0, width - label - field - pad), Scale(16), TRUE);
        }
        MoveWindow(quotaSoft_, area.left, area.top + hint + pad + kLimitCount * Scale(27) + Scale(4), width, Scale(20), TRUE);
    }

    // SMTP: hint, login switch, size, rejected recipients filling the rest
    void LayoutSmtpTab(const RECT& area) {
        int width = area.right - area.left;
        int pad = Scale(6);
        int label = Scale(kRowLabel);
        int field = Scale(kRowEdit);
        int y = area.top;
        MoveWindow(smtpHint_, area.left, y, width, Scale(20), TRUE);
        y += Scale(20) + pad;
        MoveWindow(smtpRequireAuth_, area.left, y, width, Scale(20), TRUE);
        y += Scale(28);
        MoveWindow(smtpMaxSizeLabel_, area.left, y + Scale(3), label, Scale(16), TRUE);
        MoveWindow(smtpMaxSize_, area.left + label, y, field, Scale(21), TRUE);
        MoveWindow(smtpMaxSizeNote_, area.left + label + field + pad, y + Scale(3), std::max(0, width - label - field - pad), Scale(16), TRUE);
        y += Scale(32);
        MoveWindow(smtpRejectLabel_, area.left, y, width, Scale(30), TRUE);
        y += Scale(32);
        MoveWindow(smtpReject_, area.left, y, width, std::max(Scale(40), static_cast<int>(area.bottom) - y), TRUE);
    }

    void LayoutTabs(const RECT& area) {
        int width = area.right - area.left;
        int pad = Scale(6);
        int button = Scale(110);
        int hint = Scale(30);
        int helpHeight = Scale(64);

        // extensions: hint, list, description, defaults
        MoveWindow(pluginsHint_, area.left, area.top, width, hint, TRUE);
        int listBottom = area.bottom - helpHeight - Scale(25) - 2 * pad;
        MoveWindow(pluginsList_, area.left, area.top + hint, width, listBottom - area.top - hint, TRUE);
        MoveWindow(pluginHelp_, area.left, listBottom + pad, width, helpHeight, TRUE);
        MoveWindow(pluginDefaults_, area.right - button, area.bottom - Scale(25), button, Scale(25), TRUE);
        ListView_SetColumnWidth(pluginsList_, 0, width - Scale(24));

        // server ID: hint, the switch, then the fields in two columns when there is room
        MoveWindow(idHint_, area.left, area.top, width, hint, TRUE);
        MoveWindow(idEnabled_, area.left, area.top + hint + pad, width, Scale(20), TRUE);
        int columns = width >= Scale(520) ? 2 : 1;
        int rows = (kIdFieldCount + columns - 1) / columns;
        int columnWidth = (width - (columns - 1) * 3 * pad) / columns;
        int idLabelWidth = Scale(80);
        int fieldsTop = area.top + hint + pad + Scale(30);
        for (int i = 0; i < kIdFieldCount; i++) {
            int x = area.left + (i / rows) * (columnWidth + 3 * pad);
            int y = fieldsTop + (i % rows) * Scale(27);
            MoveWindow(idLabels_[i], x, y + Scale(3), idLabelWidth, Scale(16), TRUE);
            MoveWindow(idEdits_[i], x + idLabelWidth, y, columnWidth - idLabelWidth, Scale(21), TRUE);
        }

        LayoutLimitsTab(area);
        LayoutSmtpTab(area);

        // quirks: hint, list, description
        MoveWindow(quirksHint_, area.left, area.top, width, hint, TRUE);
        MoveWindow(quirksList_, area.left, area.top + hint, width, area.bottom - helpHeight - pad - area.top - hint, TRUE);
        MoveWindow(quirkHelp_, area.left, area.bottom - helpHeight, width, helpHeight, TRUE);
        ListView_SetColumnWidth(quirksList_, 0, width - Scale(24));

        // fault scripts: hint, rules, buttons
        int scriptsHint = Scale(44);
        MoveWindow(scriptsHint_, area.left, area.top, width, scriptsHint, TRUE);
        MoveWindow(scriptRules_, area.left, area.top + scriptsHint, width, area.bottom - Scale(25) - pad - area.top - scriptsHint, TRUE);
        MoveWindow(scriptLoad_, area.left, area.bottom - Scale(25), Scale(80), Scale(25), TRUE);
        MoveWindow(scriptClear_, area.left + Scale(80) + pad, area.bottom - Scale(25), Scale(80), Scale(25), TRUE);
        MoveWindow(scriptDocs_, area.right - button, area.bottom - Scale(25), button, Scale(25), TRUE);

        // REST API: hint, enable, port, token, address, reference
        int restHint = Scale(44);
        int y = area.top;
        MoveWindow(restHint_, area.left, y, width, restHint, TRUE);
        y += restHint + pad;
        MoveWindow(restEnabled_, area.left, y, width, Scale(20), TRUE);
        y += Scale(28);
        int labelWidth = Scale(110);
        MoveWindow(restPortLabel_, area.left, y + Scale(3), labelWidth, Scale(16), TRUE);
        MoveWindow(restPort_, area.left + labelWidth, y, Scale(70), Scale(21), TRUE);
        y += Scale(28);
        MoveWindow(restTokenLabel_, area.left, y + Scale(3), labelWidth, Scale(16), TRUE);
        MoveWindow(restToken_, area.left + labelWidth, y, std::min(Scale(240), width - labelWidth), Scale(21), TRUE);
        y += Scale(28);
        MoveWindow(restUrlLabel_, area.left, y + Scale(3), labelWidth, Scale(16), TRUE);
        int urlWidth = std::min(Scale(240), width - labelWidth - Scale(70) - pad);
        MoveWindow(restUrl_, area.left + labelWidth, y, urlWidth, Scale(21), TRUE);
        MoveWindow(restCopy_, area.left + labelWidth + urlWidth + pad, y - Scale(1), Scale(70), Scale(23), TRUE);
        MoveWindow(restDocs_, area.right - button, area.bottom - Scale(25), button, Scale(25), TRUE);
    }

    // the state of the server in the banner: a light, what it is, and where it listens
    void PaintBanner(HDC dc) {
        DrawEtchedFrame(dc, banner_);
        int pad = Scale(12);
        int light = Scale(14);
        int lightTop = (banner_.top + banner_.bottom - light) / 2;
        HBRUSH brush = CreateSolidBrush(bannerColor_);
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(DC_PEN));
        SetDCPenColor(dc, RGB(64, 64, 64));
        Ellipse(dc, banner_.left + pad, lightTop, banner_.left + pad + light, lightTop + light);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(brush);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        int textLeft = banner_.left + 2 * pad + light;
        RECT title = {textLeft, banner_.top + Scale(8), banner_.right - Scale(230), banner_.top + Scale(26)};
        static HFONT titleFont = MakeFont(L"Tahoma", 10, FW_BOLD);
        HGDIOBJ oldFont = SelectObject(dc, titleFont);
        DrawTextW(dc, bannerTitle_.c_str(), -1, &title, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        RECT detail = {textLeft, banner_.top + Scale(27), banner_.right - Scale(230), banner_.bottom - Scale(6)};
        SelectObject(dc, UiFont());
        DrawTextW(dc, bannerDetail_.c_str(), -1, &detail, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, oldFont);
    }

    void FillCheckList(HWND list, const json& items, const std::set<std::string>& checked) {
        loading_ = true;
        ListView_DeleteAllItems(list);
        int index = 0;
        for (const auto& entry : items) {
            std::wstring name = JsonText(entry, "name");
            LVITEMW item = {};
            item.mask = LVIF_TEXT;
            item.iItem = index;
            item.pszText = name.data();
            ListView_InsertItem(list, &item);
            ListView_SetCheckState(list, index, checked.count(entry.value("name", "")) > 0);
            index++;
        }
        loading_ = false;
    }

    json CheckedNames(HWND list, const json& items) {
        json names = json::array();
        for (int i = 0; i < static_cast<int>(items.size()); i++) {
            if (ListView_GetCheckState(list, i)) {
                names.push_back(items[i]["name"]);
            }
        }
        return names;
    }

    static std::set<std::string> NameSet(const json& names) {
        std::set<std::string> set;
        for (const auto& name : names) {
            if (name.is_string()) {
                set.insert(name.get<std::string>());
            }
        }
        return set;
    }

    void FillUsers() {
        ListView_DeleteAllItems(usersList_);
        for (int i = 0; i < static_cast<int>(users_.size()); i++) {
            LVITEMW item = {};
            item.mask = LVIF_TEXT;
            item.iItem = i;
            item.pszText = users_[i].name.data();
            ListView_InsertItem(usersList_, &item);
            ListView_SetItemText(usersList_, i, 1, users_[i].password.data());
        }
        // a vertical scroll bar may have come or gone with the rows
        StretchColumn(usersList_, 1);
        EnableWindow(userRemove_, users_.size() > 1);
    }

    void EditUser(int index) {
        bool adding = index < 0;
        if (!adding && index >= static_cast<int>(users_.size())) {
            return;
        }
        std::vector<PromptField> fields = {{L"User name:", adding ? L"" : users_[index].name}, {L"Password:", adding ? L"" : users_[index].password}};
        if (!PromptDialog(hwnd_, adding ? L"Add test account" : L"Edit test account", fields) || fields[0].value.empty()) {
            return;
        }
        User user = {fields[0].value, fields[1].value};
        if (adding) {
            users_.push_back(user);
        } else {
            users_[index] = user;
        }
        FillUsers();
        SettingsChanged();
    }

    void LoadScript() {
        std::wstring file = OpenFileDialog(hwnd_, L"Load script rules", kJsonFilter);
        if (file.empty()) {
            return;
        }
        TheSidecar().Call("readScript", {{"file", Narrow(file)}}, [this](const json& rules, const std::string& error) {
            if (!error.empty()) {
                MessageBoxW(hwnd_, Widen(error).c_str(), L"Script rules", MB_ICONERROR);
                return;
            }
            script_ = rules;
            ShowScript();
            SettingsChanged();
        });
    }

    void ShowScript() {
        ListView_DeleteAllItems(scriptRules_);
        int index = 0;
        for (const auto& rule : script_.is_array() ? script_ : json::array()) {
            std::wstring text = Widen(rule.dump());
            LVITEMW item = {};
            item.mask = LVIF_TEXT;
            item.iItem = index++;
            item.pszText = text.data();
            ListView_InsertItem(scriptRules_, &item);
        }
        EnableWindow(scriptClear_, index > 0);
    }

    void UpdateEnabled() {
        EnableWindow(imapsPort_, Button_GetCheck(imapsOn_) == BST_CHECKED);
        EnableWindow(smtpPort_, Button_GetCheck(smtpOn_) == BST_CHECKED);
        bool rest = Button_GetCheck(restEnabled_) == BST_CHECKED;
        for (HWND control : {restPortLabel_, restPort_, restTokenLabel_, restToken_}) {
            EnableWindow(control, rest);
        }
        bool id = Button_GetCheck(idEnabled_) == BST_CHECKED;
        for (int i = 0; i < kIdFieldCount; i++) {
            EnableWindow(idLabels_[i], id);
            EnableWindow(idEdits_[i], id);
        }
    }

    // a port field of a listener that can be turned off: the port when on, 0 when off
    static void LoadPort(HWND check, HWND edit, int port, int fallback) {
        Button_SetCheck(check, port ? BST_CHECKED : BST_UNCHECKED);
        SetWindowTextW(edit, std::to_wstring(port ? port : fallback).c_str());
    }

    void LoadConfig(const json& config) {
        loading_ = true;
        std::wstring host = JsonText(config, "host");
        int hostItem = host == L"127.0.0.1" ? 0 : host == L"0.0.0.0" ? 1 : -1;
        ComboBox_SetCurSel(host_, hostItem);
        if (hostItem < 0) {
            SetWindowTextW(host_, host.c_str());
        }
        SetWindowTextW(imapPort_, std::to_wstring(config.value("imapPort", 0)).c_str());
        LoadPort(imapsOn_, imapsPort_, config.value("imapsPort", 0), defaults_.value("imapsPort", 0));
        LoadPort(smtpOn_, smtpPort_, config.value("smtpPort", 0), defaults_.value("smtpPort", 0));

        json rest = config.value("rest", json::object());
        json restDefaults = defaults_.value("rest", json::object());
        Button_SetCheck(restEnabled_, rest.value("enabled", false) ? BST_CHECKED : BST_UNCHECKED);
        SetWindowTextW(restPort_, std::to_wstring(rest.value("port", restDefaults.value("port", 0))).c_str());
        SetWindowTextW(restToken_, JsonText(rest, "token").c_str());
        restHost_ = rest.value("host", restDefaults.value("host", std::string("127.0.0.1")));

        users_.clear();
        for (const auto& user : config.value("users", json::array())) {
            users_.push_back({JsonText(user, "username"), JsonText(user, "password")});
        }
        FillUsers();
        script_ = config.value("script", json::array());
        ShowScript();
        LoadLimits(config.value("limits", json::object()));
        LoadSmtp(config.value("smtp", json::object()));
        json id = config.value("id", json::object());
        Button_SetCheck(idEnabled_, id.value("enabled", false) ? BST_CHECKED : BST_UNCHECKED);
        idValues_ = id.value("values", json::object());
        for (int i = 0; i < kIdFieldCount; i++) {
            SetWindowTextW(idEdits_[i], JsonText(idValues_, kIdFields[i]).c_str());
        }
        FillCheckList(pluginsList_, plugins_, NameSet(config.value("plugins", json::array())));
        FillCheckList(quirksList_, quirks_, NameSet(config.value("quirks", json::array())));
        UpdateEnabled();
        loading_ = false;
        applied_ = ReadConfig();
        SettingsChanged();
    }

    json ReadConfig() {
        auto number = [](HWND edit) { return _wtoi(GetWindowString(edit).c_str()); };
        auto optional = [&](HWND check, HWND edit) { return Button_GetCheck(check) == BST_CHECKED ? number(edit) : 0; };
        // "127.0.0.1 (this computer only)" is 127.0.0.1
        std::wstring host = GetWindowString(host_);
        host = host.substr(0, host.find(L' '));
        json users = json::array();
        for (const User& user : users_) {
            users.push_back({{"username", Narrow(user.name)}, {"password", Narrow(user.password)}});
        }
        return {
            {"host", Narrow(host)},
            {"imapPort", number(imapPort_)},
            {"imapsPort", optional(imapsOn_, imapsPort_)},
            {"smtpPort", optional(smtpOn_, smtpPort_)},
            {"rest",
             {{"enabled", Button_GetCheck(restEnabled_) == BST_CHECKED},
              {"port", number(restPort_)},
              {"host", restHost_},
              {"token", Narrow(GetWindowString(restToken_))}}},
            {"users", users},
            {"plugins", CheckedNames(pluginsList_, plugins_)},
            {"quirks", CheckedNames(quirksList_, quirks_)},
            {"script", script_},
            {"id", {{"enabled", Button_GetCheck(idEnabled_) == BST_CHECKED}, {"values", IdValues()}}},
            {"limits", ReadLimits()},
            {"smtp", ReadSmtp()},
        };
    }

    // a number field that may stay empty: its number, or null for none
    static json OptionalNumber(HWND edit) {
        std::wstring text = GetWindowString(edit);
        return text.empty() ? json(nullptr) : json(_wtoi64(text.c_str()));
    }

    static void SetOptionalNumber(HWND edit, const json& value) {
        SetWindowTextW(edit, value.is_number_integer() ? std::to_wstring(value.get<long long>()).c_str() : L"");
    }

    void LoadLimits(const json& limits) {
        for (int i = 0; i < kLimitCount; i++) {
            SetOptionalNumber(limitEdits_[i], limits.value(kLimits[i].key, json(nullptr)));
        }
        Button_SetCheck(quotaSoft_, limits.value("quotaSoft", false) ? BST_CHECKED : BST_UNCHECKED);
    }

    json ReadLimits() {
        json limits = {{"quotaSoft", Button_GetCheck(quotaSoft_) == BST_CHECKED}};
        for (int i = 0; i < kLimitCount; i++) {
            limits[kLimits[i].key] = OptionalNumber(limitEdits_[i]);
        }
        return limits;
    }

    void LoadSmtp(const json& smtp) {
        Button_SetCheck(smtpRequireAuth_, smtp.value("requireAuth", false) ? BST_CHECKED : BST_UNCHECKED);
        SetOptionalNumber(smtpMaxSize_, smtp.value("maxSize", json(nullptr)));
        std::wstring rules;
        for (const auto& line : smtp.value("rejectRecipients", json::array())) {
            rules += Widen(line.is_string() ? line.get<std::string>() : "") + L"\r\n";
        }
        SetWindowTextW(smtpReject_, rules.c_str());
    }

    // the rejected recipients are kept as lines, the sidecar parses them (and refuses a broken one on start)
    json ReadSmtp() {
        json lines = json::array();
        std::wstring text = GetWindowString(smtpReject_);
        size_t start = 0;
        while (start <= text.size()) {
            size_t end = std::min(text.find(L'\n', start), text.size());
            std::wstring line = text.substr(start, end - start);
            line.erase(line.find_last_not_of(L" \t\r") + 1);
            if (!line.empty()) {
                lines.push_back(Narrow(line));
            }
            start = end + 1;
        }
        return {{"requireAuth", Button_GetCheck(smtpRequireAuth_) == BST_CHECKED}, {"maxSize", OptionalNumber(smtpMaxSize_)}, {"rejectRecipients", lines}};
    }

    // the unit of each limit, and whether the extension it needs is turned on
    void UpdateLimitNotes() {
        std::set<std::string> on = NameSet(CheckedNames(pluginsList_, plugins_));
        for (int i = 0; i < kLimitCount; i++) {
            std::wstring note = kLimits[i].unit;
            std::wstring names;
            bool enabled = false;
            for (const char* plugin : kLimits[i].plugins) {
                names += (names.empty() ? L"" : L" or ") + Widen(plugin);
                enabled = enabled || on.count(plugin);
            }
            if (!names.empty()) {
                note += enabled ? L"  (" + names + L" is on)" : L"  (needs " + names + L", turn it on in IMAP Extensions)";
            }
            SetWindowTextW(limitNotes_[i], note.c_str());
        }
    }

    // the values of the ID fields, empty ones left out. Fields the form has no edit for (from config.json) are kept
    json IdValues() {
        json values = idValues_.is_object() ? idValues_ : json::object();
        for (int i = 0; i < kIdFieldCount; i++) {
            std::string value = Narrow(GetWindowString(idEdits_[i]));
            if (value.empty()) {
                values.erase(kIdFields[i]);
            } else {
                values[kIdFields[i]] = value;
            }
        }
        return values;
    }

    // settings that differ from the ones the server runs with wait for Apply
    void SettingsChanged() {
        if (loading_) {
            return;
        }
        changed_ = ReadConfig() != applied_;
        EnableWindow(apply_, running_ && changed_);
        UpdateLimitNotes();
        UpdateBanner();
        UpdateClientSettings();
    }

    void Start() {
        json config = ReadConfig();
        bannerTitle_ = running_ ? L"Restarting..." : L"Starting...";
        InvalidateRect(hwnd_, &banner_, TRUE);
        TheSidecar().Call("start", {{"config", config}}, [this, config](const json&, const std::string& error) {
            if (error.empty()) {
                applied_ = config;
            }
            SettingsChanged();
        });
    }

    void ShowState(const json& status) {
        status_ = status;
        running_ = status.value("running", false);
        SetWindowTextW(toggle_, running_ ? L"&Stop" : L"&Start");
        SettingsChanged();
        if (status.contains("restUrl")) {
            SetWindowTextW(restUrl_, JsonText(status, "restUrl").c_str());
        } else {
            SetWindowTextW(restUrl_, L"(when the REST API runs)");
        }
    }

    void UpdateBanner() {
        std::wstring error = JsonText(status_, "error");
        if (running_) {
            bannerColor_ = RGB(40, 180, 70);
            bannerTitle_ = L"Running";
            std::wstring host = JsonText(status_, "host");
            bannerDetail_ = L"IMAP " + host + L":" + std::to_wstring(status_.value("imapPort", 0));
            if (status_.value("imapsPort", 0)) {
                bannerDetail_ += L"  ·  IMAPS " + std::to_wstring(status_.value("imapsPort", 0));
            }
            if (status_.value("smtpPort", 0)) {
                bannerDetail_ += L"  ·  SMTP " + std::to_wstring(status_.value("smtpPort", 0));
            }
            if (status_.value("restPort", 0)) {
                bannerDetail_ += L"  ·  REST " + std::to_wstring(status_.value("restPort", 0));
            }
            if (changed_) {
                bannerTitle_ = L"Running, settings changed";
                bannerDetail_ = L"Apply the changes to restart the server with them. Connected clients are disconnected.";
            }
        } else if (!error.empty()) {
            bannerColor_ = RGB(210, 50, 40);
            bannerTitle_ = L"Could not start";
            bannerDetail_ = error;
        } else {
            bannerColor_ = RGB(160, 160, 160);
            bannerTitle_ = L"Stopped";
            bannerDetail_ = L"Start the server to accept IMAP and SMTP connections.";
        }
        InvalidateRect(hwnd_, &banner_, TRUE);
    }

    // what to type into an email client, from the ports the server runs on (or will run on)
    void UpdateClientSettings() {
        json config = ReadConfig();
        bool live = running_ && !changed_;
        std::wstring host = ClientHost(live ? JsonText(status_, "host") : Widen(config.value("host", "")));
        int imap = live ? status_.value("imapPort", 0) : config.value("imapPort", 0);
        int imaps = live ? status_.value("imapsPort", 0) : config.value("imapsPort", 0);
        int smtp = live ? status_.value("smtpPort", 0) : config.value("smtpPort", 0);
        std::wstring text;
        if (!running_) {
            text += L"(the server is stopped)\r\n\r\n";
        }
        text += L"Incoming mail (IMAP)\r\n  Server: " + host + L"\r\n  Port: " + std::to_wstring(imap) + L", security STARTTLS";
        if (imaps) {
            text += L"\r\n  or port " + std::to_wstring(imaps) + L", security SSL/TLS";
        }
        if (smtp) {
            text += L"\r\n\r\nOutgoing mail (SMTP)\r\n  Server: " + host + L"\r\n  Port: " + std::to_wstring(smtp) +
                    L", STARTTLS, any user name and password";
        }
        if (!users_.empty()) {
            text += L"\r\n\r\nUser name: " + users_[0].name + L"\r\nPassword: " + users_[0].password;
        }
        text += L"\r\n\r\nThe certificate is self-signed for localhost, the email client has to accept it.";
        SetWindowTextW(clientSettings_, text.c_str());
    }

    HWND toggle_ = nullptr, apply_ = nullptr;
    HWND connectionGroup_ = nullptr, hostLabel_ = nullptr, host_ = nullptr, imapLabel_ = nullptr, imapPort_ = nullptr;
    HWND imapsOn_ = nullptr, imapsPort_ = nullptr, smtpOn_ = nullptr, smtpPort_ = nullptr;
    HWND usersGroup_ = nullptr, usersList_ = nullptr, userAdd_ = nullptr, userEdit_ = nullptr, userRemove_ = nullptr;
    HWND clientGroup_ = nullptr, clientSettings_ = nullptr, copySettings_ = nullptr;
    HWND advanced_ = nullptr;
    HWND pluginsHint_ = nullptr, pluginsList_ = nullptr, pluginHelp_ = nullptr, pluginDefaults_ = nullptr;
    HWND quirksHint_ = nullptr, quirksList_ = nullptr, quirkHelp_ = nullptr;
    HWND scriptsHint_ = nullptr, scriptRules_ = nullptr, scriptLoad_ = nullptr, scriptClear_ = nullptr, scriptDocs_ = nullptr;
    HWND restHint_ = nullptr, restEnabled_ = nullptr, restPortLabel_ = nullptr, restPort_ = nullptr, restTokenLabel_ = nullptr;
    HWND restToken_ = nullptr, restUrlLabel_ = nullptr, restUrl_ = nullptr, restCopy_ = nullptr, restDocs_ = nullptr;
    HWND idHint_ = nullptr, idEnabled_ = nullptr, idLabels_[kIdFieldCount] = {}, idEdits_[kIdFieldCount] = {};
    HWND limitsHint_ = nullptr, limitLabels_[kLimitCount] = {}, limitEdits_[kLimitCount] = {}, limitNotes_[kLimitCount] = {}, quotaSoft_ = nullptr;
    HWND smtpHint_ = nullptr, smtpRequireAuth_ = nullptr, smtpMaxSizeLabel_ = nullptr, smtpMaxSize_ = nullptr, smtpMaxSizeNote_ = nullptr;
    HWND smtpRejectLabel_ = nullptr, smtpReject_ = nullptr;
    std::vector<HWND> tabs_[TAB_COUNT];

    RECT banner_ = {};
    std::wstring bannerTitle_ = L"Starting...";
    std::wstring bannerDetail_;
    COLORREF bannerColor_ = RGB(160, 160, 160);

    json plugins_ = json::array();
    json quirks_ = json::array();
    json defaults_ = json::object();
    json script_ = json::array();
    json idValues_ = json::object();
    json status_ = json::object();
    // the settings the server runs with, edits that differ wait for Apply
    json applied_ = json::object();
    std::vector<User> users_;
    std::string restHost_ = "127.0.0.1";
    bool running_ = false;
    bool changed_ = false;
    bool loading_ = false;
};

}  // namespace

Page* CreateServerPage() {
    return new ServerPage();
}
