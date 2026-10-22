// The Server page: listeners, the REST API, test accounts, script rules, the mailbox files, and the IMAP extensions
// and quirk presets of ImapKit
#include "app.h"

#include <windowsx.h>

#include <algorithm>
#include <set>

namespace {

enum ControlId {
    IDC_HOST = 200,
    IDC_IMAP_PORT,
    IDC_IMAPS_PORT,
    IDC_SMTP_PORT,
    IDC_REST_ENABLED,
    IDC_REST_PORT,
    IDC_REST_TOKEN,
    IDC_USERS,
    IDC_USER_ADD,
    IDC_USER_EDIT,
    IDC_USER_REMOVE,
    IDC_SCRIPT_LABEL,
    IDC_SCRIPT_LOAD,
    IDC_SCRIPT_CLEAR,
    IDC_PLUGINS,
    IDC_QUIRKS,
    IDC_HELP_TEXT,
    IDC_START,
    IDC_STOP,
    IDC_EXPORT,
    IDC_IMPORT,
    IDC_RESET,
    IDC_STATE,
};

const wchar_t* kJsonFilter = L"JSON files (*.json)\0*.json\0All files (*.*)\0*.*\0";

struct User {
    std::wstring name;
    std::wstring password;
};

class ServerPage : public Page {
public:
    void OnEvent(const std::string& name, const json& data) override {
        if (name == "ready") {
            TheSidecar().Call("hello", json::object(), [this](const json& result, const std::string& error) {
                if (error.empty()) {
                    plugins_ = result["plugins"];
                    quirks_ = result.value("quirks", json::array());
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
        listenersGroup_ = AddControl(L"BUTTON", L"Listeners", BS_GROUPBOX, 0);
        restGroup_ = AddControl(L"BUTTON", L"REST API", BS_GROUPBOX, 0);
        usersGroup_ = AddControl(L"BUTTON", L"Test accounts", BS_GROUPBOX, 0);
        extensionsGroup_ = AddControl(L"BUTTON", L"IMAP extensions", BS_GROUPBOX, 0);
        quirksGroup_ = AddControl(L"BUTTON", L"Quirk presets (behave like a known server)", BS_GROUPBOX, 0);

        AddRow(L"Address:", IDC_HOST, ES_AUTOHSCROLL);
        AddRow(L"IMAP port (STARTTLS):", IDC_IMAP_PORT, ES_NUMBER);
        AddRow(L"IMAPS port (TLS, 0 = off):", IDC_IMAPS_PORT, ES_NUMBER);
        AddRow(L"SMTP port (0 = off):", IDC_SMTP_PORT, ES_NUMBER);
        restEnabled_ = AddControl(L"BUTTON", L"&Enable the REST API (control API over HTTP)", WS_TABSTOP | BS_AUTOCHECKBOX, IDC_REST_ENABLED);
        AddRow(L"REST port:", IDC_REST_PORT, ES_NUMBER);
        AddRow(L"Token (optional):", IDC_REST_TOKEN, ES_AUTOHSCROLL);

        users_list_ = AddControl(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, IDC_USERS, WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(users_list_, LVS_EX_FULLROWSELECT);
        AddColumns(users_list_, {{L"User name", 150}, {L"Password", 130}});
        userAdd_ = AddControl(L"BUTTON", L"&Add...", WS_TABSTOP, IDC_USER_ADD);
        userEdit_ = AddControl(L"BUTTON", L"E&dit...", WS_TABSTOP, IDC_USER_EDIT);
        userRemove_ = AddControl(L"BUTTON", L"Re&move", WS_TABSTOP, IDC_USER_REMOVE);

        scriptLabel_ = AddControl(L"STATIC", L"Script rules: none", SS_LEFT | SS_CENTERIMAGE, IDC_SCRIPT_LABEL);
        scriptLoad_ = AddControl(L"BUTTON", L"&Load...", WS_TABSTOP, IDC_SCRIPT_LOAD);
        scriptClear_ = AddControl(L"BUTTON", L"Clea&r", WS_TABSTOP, IDC_SCRIPT_CLEAR);

        start_ = AddControl(L"BUTTON", L"&Start", WS_TABSTOP | BS_DEFPUSHBUTTON, IDC_START);
        stop_ = AddControl(L"BUTTON", L"S&top", WS_TABSTOP, IDC_STOP);
        export_ = AddControl(L"BUTTON", L"E&xport mailboxes...", WS_TABSTOP, IDC_EXPORT);
        import_ = AddControl(L"BUTTON", L"&Import mailboxes...", WS_TABSTOP, IDC_IMPORT);
        reset_ = AddControl(L"BUTTON", L"Reset mailbo&xes...", WS_TABSTOP, IDC_RESET);
        state_ = AddControl(L"STATIC", L"Starting the sidecar...", SS_LEFT, IDC_STATE);

        pluginsList_ = AddCheckList(IDC_PLUGINS);
        quirksList_ = AddCheckList(IDC_QUIRKS);
        help_ = AddControl(L"STATIC", L"Select an extension or a quirk to see what it does. Changes take effect when the server starts.", SS_LEFT,
                           IDC_HELP_TEXT, WS_EX_STATICEDGE);
        EnableWindow(stop_, FALSE);
    }

    void OnSize(int width, int height) override {
        int pad = Scale(8);
        int row = Scale(24);
        int caption = Scale(18);
        int labelWidth = Scale(150);
        int editWidth = Scale(160);
        int groupWidth = labelWidth + editWidth + 3 * pad;
        int left = pad;
        int y = pad;

        auto placeRow = [&](int index, int top) {
            MoveWindow(labels_[index], left + pad, top + Scale(3), labelWidth, Scale(16), TRUE);
            MoveWindow(edits_[index], left + pad + labelWidth, top, editWidth, Scale(21), TRUE);
        };

        MoveWindow(listenersGroup_, left, y, groupWidth, caption + 4 * row + pad, TRUE);
        for (int i = 0; i < 4; i++) {
            placeRow(i, y + caption + i * row);
        }
        y += caption + 4 * row + 2 * pad;

        MoveWindow(restGroup_, left, y, groupWidth, caption + 3 * row + pad, TRUE);
        MoveWindow(restEnabled_, left + pad, y + caption, groupWidth - 2 * pad, Scale(20), TRUE);
        placeRow(4, y + caption + row);
        placeRow(5, y + caption + 2 * row);
        y += caption + 3 * row + 2 * pad;

        int listHeight = Scale(76);
        MoveWindow(usersGroup_, left, y, groupWidth, caption + listHeight + row + 2 * pad, TRUE);
        MoveWindow(users_list_, left + pad, y + caption, groupWidth - 2 * pad, listHeight, TRUE);
        int buttonTop = y + caption + listHeight + Scale(5);
        int small = Scale(70);
        MoveWindow(userAdd_, left + pad, buttonTop, small, Scale(23), TRUE);
        MoveWindow(userEdit_, left + pad + small + Scale(4), buttonTop, small, Scale(23), TRUE);
        MoveWindow(userRemove_, left + pad + 2 * (small + Scale(4)), buttonTop, small, Scale(23), TRUE);
        y += caption + listHeight + row + 3 * pad;

        MoveWindow(scriptLabel_, left, y, groupWidth - 2 * small - Scale(8), Scale(23), TRUE);
        MoveWindow(scriptLoad_, left + groupWidth - 2 * small - Scale(4), y, small, Scale(23), TRUE);
        MoveWindow(scriptClear_, left + groupWidth - small, y, small, Scale(23), TRUE);
        y += Scale(23) + pad;

        int button = Scale(102);
        MoveWindow(start_, left, y, button, Scale(25), TRUE);
        MoveWindow(stop_, left + button + Scale(5), y, button, Scale(25), TRUE);
        y += Scale(25) + Scale(5);
        MoveWindow(export_, left, y, button, Scale(25), TRUE);
        MoveWindow(import_, left + button + Scale(5), y, button, Scale(25), TRUE);
        MoveWindow(reset_, left + 2 * (button + Scale(5)), y, button, Scale(25), TRUE);
        y += Scale(25) + pad;
        MoveWindow(state_, left, y, groupWidth, std::max(Scale(16), height - y - pad), TRUE);

        int right = left + groupWidth + pad;
        int columnWidth = width - right - pad;
        int helpHeight = Scale(64);
        int available = height - 3 * pad - helpHeight;
        int quirksHeight = Scale(150);
        int extensionsHeight = available - quirksHeight - pad;
        MoveWindow(extensionsGroup_, right, pad, columnWidth, extensionsHeight, TRUE);
        MoveWindow(pluginsList_, right + pad, pad + caption, columnWidth - 2 * pad, extensionsHeight - caption - pad, TRUE);
        int quirksTop = pad + extensionsHeight + pad;
        MoveWindow(quirksGroup_, right, quirksTop, columnWidth, quirksHeight, TRUE);
        MoveWindow(quirksList_, right + pad, quirksTop + caption, columnWidth - 2 * pad, quirksHeight - caption - pad, TRUE);
        MoveWindow(help_, right, height - pad - helpHeight, columnWidth, helpHeight, TRUE);
        ListView_SetColumnWidth(pluginsList_, 0, columnWidth - 2 * pad - Scale(24));
        ListView_SetColumnWidth(quirksList_, 0, columnWidth - 2 * pad - Scale(24));
    }

    void OnCommand(int id, int code, HWND) override {
        if (code != BN_CLICKED) {
            return;
        }
        switch (id) {
            case IDC_START:
                Start();
                break;
            case IDC_STOP:
                TheSidecar().Call("stop");
                break;
            case IDC_RESET:
                if (MessageBoxW(hwnd_, L"Remove every message and folder and start over with the default mailboxes?", L"Reset mailboxes",
                                MB_ICONWARNING | MB_OKCANCEL) == IDOK) {
                    TheSidecar().Call("reset");
                }
                break;
            case IDC_EXPORT: {
                std::wstring file = SaveFileDialog(hwnd_, L"Export mailboxes", kJsonFilter, L"json", L"mailboxes");
                if (!file.empty()) {
                    TheSidecar().Call("exportStorage", {{"file", Narrow(file)}}, ErrorReporter(hwnd_, L"Export"));
                }
                break;
            }
            case IDC_IMPORT: {
                std::wstring file = OpenFileDialog(hwnd_, L"Import mailboxes", kJsonFilter);
                if (!file.empty() && MessageBoxW(hwnd_, L"Replace every mailbox with the ones in the file?", L"Import mailboxes",
                                                 MB_ICONWARNING | MB_OKCANCEL) == IDOK) {
                    TheSidecar().Call("importStorage", {{"file", Narrow(file)}}, ErrorReporter(hwnd_, L"Import"));
                }
                break;
            }
            case IDC_USER_ADD:
                EditUser(-1);
                break;
            case IDC_USER_EDIT:
                EditUser(ListView_GetNextItem(users_list_, -1, LVNI_SELECTED));
                break;
            case IDC_USER_REMOVE: {
                int selected = ListView_GetNextItem(users_list_, -1, LVNI_SELECTED);
                if (selected >= 0 && users_.size() > 1) {
                    users_.erase(users_.begin() + selected);
                    FillUsers();
                }
                break;
            }
            case IDC_SCRIPT_LOAD: {
                std::wstring file = OpenFileDialog(hwnd_, L"Load script rules", kJsonFilter);
                if (!file.empty()) {
                    TheSidecar().Call("readScript", {{"file", Narrow(file)}}, [this](const json& rules, const std::string& error) {
                        if (!error.empty()) {
                            MessageBoxW(hwnd_, Widen(error).c_str(), L"Script rules", MB_ICONERROR);
                            return;
                        }
                        script_ = rules;
                        ShowScript();
                    });
                }
                break;
            }
            case IDC_SCRIPT_CLEAR:
                script_ = json::array();
                ShowScript();
                break;
        }
    }

    LRESULT OnNotify(NMHDR* header) override {
        if (header->code == NM_DBLCLK && header->hwndFrom == users_list_) {
            EditUser(ListView_GetNextItem(users_list_, -1, LVNI_SELECTED));
        }
        if (header->code != LVN_ITEMCHANGED) {
            return 0;
        }
        auto* change = reinterpret_cast<NMLISTVIEW*>(header);
        if (!(change->uNewState & LVIS_SELECTED) || change->iItem < 0) {
            return 0;
        }
        // the help text of the extension or quirk, they describe themselves with different keys
        bool plugin = header->hwndFrom == pluginsList_;
        const json& items = plugin ? plugins_ : quirks_;
        if ((plugin || header->hwndFrom == quirksList_) && change->iItem < static_cast<int>(items.size())) {
            const json& item = items[change->iItem];
            SetWindowTextW(help_, (JsonText(item, "name") + L": " + JsonText(item, plugin ? "help" : "description")).c_str());
        }
        return 0;
    }

private:
    void AddRow(const wchar_t* label, int id, DWORD style) {
        labels_.push_back(AddControl(L"STATIC", label, SS_LEFT, 0));
        edits_.push_back(AddControl(L"EDIT", L"", WS_TABSTOP | style, id, WS_EX_CLIENTEDGE));
    }

    HWND AddCheckList(int id) {
        HWND list = AddControl(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER, id,
                               WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        AddColumns(list, {{L"", 160}});
        return list;
    }

    void FillCheckList(HWND list, const json& items, const std::set<std::string>& checked) {
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
        ListView_DeleteAllItems(users_list_);
        for (int i = 0; i < static_cast<int>(users_.size()); i++) {
            LVITEMW item = {};
            item.mask = LVIF_TEXT;
            item.iItem = i;
            item.pszText = users_[i].name.data();
            ListView_InsertItem(users_list_, &item);
            ListView_SetItemText(users_list_, i, 1, users_[i].password.data());
        }
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
    }

    void ShowScript() {
        size_t count = script_.is_array() ? script_.size() : 0;
        SetWindowTextW(scriptLabel_, count ? (L"Script rules: " + std::to_wstring(count) + L" loaded").c_str() : L"Script rules: none");
        EnableWindow(scriptClear_, count > 0);
    }

    void LoadConfig(const json& config) {
        SetWindowTextW(edits_[0], JsonText(config, "host").c_str());
        SetWindowTextW(edits_[1], std::to_wstring(config.value("imapPort", 0)).c_str());
        SetWindowTextW(edits_[2], std::to_wstring(config.value("imapsPort", 0)).c_str());
        SetWindowTextW(edits_[3], std::to_wstring(config.value("smtpPort", 0)).c_str());
        json rest = config.value("rest", json::object());
        Button_SetCheck(restEnabled_, rest.value("enabled", false) ? BST_CHECKED : BST_UNCHECKED);
        SetWindowTextW(edits_[4], std::to_wstring(rest.value("port", 0)).c_str());
        SetWindowTextW(edits_[5], JsonText(rest, "token").c_str());
        restHost_ = rest.value("host", std::string("127.0.0.1"));

        users_.clear();
        for (const auto& user : config.value("users", json::array())) {
            users_.push_back({JsonText(user, "username"), JsonText(user, "password")});
        }
        FillUsers();
        script_ = config.value("script", json::array());
        ShowScript();
        FillCheckList(pluginsList_, plugins_, NameSet(config.value("plugins", json::array())));
        FillCheckList(quirksList_, quirks_, NameSet(config.value("quirks", json::array())));
    }

    json ReadConfig() {
        auto number = [this](int index) { return _wtoi(GetWindowString(edits_[index]).c_str()); };
        json users = json::array();
        for (const User& user : users_) {
            users.push_back({{"username", Narrow(user.name)}, {"password", Narrow(user.password)}});
        }
        return {
            {"host", Narrow(GetWindowString(edits_[0]))},
            {"imapPort", number(1)},
            {"imapsPort", number(2)},
            {"smtpPort", number(3)},
            {"rest",
             {{"enabled", Button_GetCheck(restEnabled_) == BST_CHECKED},
              {"port", number(4)},
              {"host", restHost_},
              {"token", Narrow(GetWindowString(edits_[5]))}}},
            {"users", users},
            {"plugins", CheckedNames(pluginsList_, plugins_)},
            {"quirks", CheckedNames(quirksList_, quirks_)},
            {"script", script_},
        };
    }

    void Start() {
        SetWindowTextW(state_, L"Starting...");
        TheSidecar().Call("start", {{"config", ReadConfig()}}, [this](const json&, const std::string& error) {
            if (!error.empty()) {
                SetWindowTextW(state_, (L"Could not start: " + Widen(error)).c_str());
            }
        });
    }

    void ShowState(const json& status) {
        bool running = status.value("running", false);
        EnableWindow(start_, !running);
        EnableWindow(stop_, running);
        std::wstring error = JsonText(status, "error");
        if (running) {
            std::wstring host = JsonText(status, "host");
            std::wstring text = L"Running.\nIMAP " + host + L":" + std::to_wstring(status.value("imapPort", 0));
            if (status.value("imapsPort", 0)) {
                text += L"   IMAPS " + host + L":" + std::to_wstring(status.value("imapsPort", 0));
            }
            if (status.value("smtpPort", 0)) {
                text += L"\nSMTP " + host + L":" + std::to_wstring(status.value("smtpPort", 0));
            }
            if (status.contains("restUrl")) {
                text += L"   REST " + JsonText(status, "restUrl");
            }
            SetWindowTextW(state_, text.c_str());
        } else if (!error.empty()) {
            SetWindowTextW(state_, (L"Stopped: " + error).c_str());
        } else {
            SetWindowTextW(state_, L"Stopped.");
        }
    }

    HWND listenersGroup_ = nullptr, restGroup_ = nullptr, usersGroup_ = nullptr, extensionsGroup_ = nullptr, quirksGroup_ = nullptr;
    std::vector<HWND> labels_, edits_;
    HWND restEnabled_ = nullptr;
    HWND users_list_ = nullptr, userAdd_ = nullptr, userEdit_ = nullptr, userRemove_ = nullptr;
    HWND scriptLabel_ = nullptr, scriptLoad_ = nullptr, scriptClear_ = nullptr;
    HWND start_ = nullptr, stop_ = nullptr, export_ = nullptr, import_ = nullptr, reset_ = nullptr, state_ = nullptr;
    HWND pluginsList_ = nullptr, quirksList_ = nullptr, help_ = nullptr;
    json plugins_ = json::array();
    json quirks_ = json::array();
    json script_ = json::array();
    std::vector<User> users_;
    std::string restHost_ = "127.0.0.1";
};

}  // namespace

Page* CreateServerPage() {
    return new ServerPage();
}
