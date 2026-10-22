// The Messages page, laid out like Outlook Express: toolbar, folder tree on the left, message list on the top
// right, and the preview with its header strip below it. It follows the changes that IMAP and SMTP clients make
#include "app.h"
#include "preview.h"

#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cmath>

// GDI+ uses min and max, which NOMINMAX keeps out of the Windows headers
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <objidl.h>
#include <gdiplus.h>
#include <unordered_map>

namespace {

enum ControlId {
    IDC_TOOLBAR = 400,
    IDC_FOLDERS,
    IDC_MESSAGES,
    IDC_FOLDER_BAR,
    IDC_PREVIEW_HOST,
    IDC_INFO_BAR,
    IDC_NEW_MAIL = 450,
    IDC_IMPORT,
    IDC_DELETE,
    IDC_MARK_READ,
    IDC_MARK_UNREAD,
    IDC_SOURCE,
    IDC_REFRESH,
    // menu commands
    IDM_FLAG = 500,
    IDM_UNFLAG,
    IDM_SAVE_AS,
    IDM_SAVE_ALL_ATTACHMENTS,
    IDM_NEW_FOLDER,
    IDM_RENAME_FOLDER,
    IDM_DELETE_FOLDER,
    // Move to: IDM_MOVE_FIRST + folder index, Save attachment: IDM_ATTACHMENT_FIRST + attachment index
    IDM_MOVE_FIRST = 600,
    IDM_ATTACHMENT_FIRST = 900,
};

constexpr int kMaxAttachmentItems = 100;

// the columns of the message list
enum Column { kAttachmentColumn, kFlagColumn, kFromColumn, kSubjectColumn, kReceivedColumn, kSizeColumn };
constexpr UINT_PTR kRefreshTimer = 1;
// height of a line of the header strip
const int kHeaderLine = Scale(17);

// a mailbox as the sidecar describes it for the folder tree
struct Folder {
    std::string path;
    std::string parent;
    std::wstring name;
    int unseen = 0;
    bool canRename = false;
    bool canDelete = false;
};

struct Summary {
    int uid = 0;
    bool seen = false;
    bool flagged = false;
    // attached files, not just images that the HTML shows
    bool attachments = false;
    std::wstring from, subject, received;
    // ISO, sorts like the dates it stands for
    std::string receivedIso;
    double size = 0;
};

const wchar_t* kEmlFilter = L"Email messages (*.eml)\0*.eml\0All files (*.*)\0*.*\0";

class MessagesPage : public Page {
public:
    void OnEvent(const std::string& name, const json& data) override {
        if (name == "ready") {
            LoadFolders();
        } else if (name == "store") {
            OnStore(data);
        }
    }

    void OnShow() override {
        preview_.SetVisible(true);
        if (foldersDirty_ || folders_.empty()) {
            Refresh();
        }
    }

protected:
    void OnCreate() override {
        CreateToolbar();

        tree_ = AddControl(WC_TREEVIEWW, L"", WS_TABSTOP | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
                           IDC_FOLDERS, WS_EX_CLIENTEDGE);
        folderBar_ = AddControl(L"STATIC", L"  Inbox", SS_LEFT | SS_CENTERIMAGE, IDC_FOLDER_BAR);
        static HFONT folderBarFont = MakeFont(L"Tahoma", 12, FW_BOLD);
        SendMessageW(folderBar_, WM_SETFONT, reinterpret_cast<WPARAM>(folderBarFont), TRUE);

        list_ = AddControl(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA, IDC_MESSAGES, WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP);
        // the first column has the paperclip of Outlook Express, drawn in the rows and in the header
        AddColumns(list_, {{L"", 22}, {L"!", 20}, {L"From", 180}, {L"Subject", 320}, {L"Received", 120}, {L"Size", 70, LVCFMT_RIGHT}});
        listProc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(list_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ListProc)));

        // the bar of Outlook Express 6 above a message whose pictures it did not download
        infoBar_ = AddControl(L"STATIC",
                              L"  Pictures from the internet are blocked, so the sender can not tell when and where the message was read. "
                              L"Click here to show them.",
                              SS_LEFT | SS_CENTERIMAGE | SS_NOTIFY | SS_NOPREFIX | SS_ENDELLIPSIS, IDC_INFO_BAR, WS_EX_STATICEDGE);
        ShowWindow(infoBar_, SW_HIDE);

        previewHost_ = AddControl(L"STATIC", L"", SS_SUNKEN | WS_CLIPCHILDREN, IDC_PREVIEW_HOST);
        preview_.Create(previewHost_, [this](const std::wstring& error) { SetWindowTextW(previewHost_, error.c_str()); });

        DragAcceptFiles(hwnd_, TRUE);

        splitters_.push_back({true, {}, [this](int x) { treeWidth_ = x; }});
        splitters_.push_back({false, {}, [this](int y) { listHeight_ = y - listTop_; }});
    }

    void OnSize(int width, int height) override {
        if (width == 0 || height == 0) {
            // created before the main window gave it a size, the splits are set on the first real size
            return;
        }
        SendMessageW(toolbar_, TB_AUTOSIZE, 0, 0);
        RECT toolbarRect;
        GetWindowRect(toolbar_, &toolbarRect);
        int top = toolbarRect.bottom - toolbarRect.top + Scale(2);

        int pad = Scale(4);
        if (treeWidth_ == 0) {
            treeWidth_ = Scale(190);
        }
        treeWidth_ = std::clamp(treeWidth_, Scale(100), std::max(Scale(100), width - Scale(300)));
        MoveWindow(tree_, 0, top, treeWidth_, height - top, TRUE);
        splitters_[0].area = {treeWidth_, top, treeWidth_ + pad, height};

        int left = treeWidth_ + pad;
        int barHeight = Scale(26);
        MoveWindow(folderBar_, left, top, width - left, barHeight, TRUE);
        int y = top + barHeight + pad;
        listTop_ = y;
        if (listHeight_ == 0) {
            listHeight_ = (height - y) * 40 / 100;
        }
        listHeight_ = std::clamp(listHeight_, Scale(60), std::max(Scale(60), height - y - Scale(160)));
        MoveWindow(list_, left, y, width - left, listHeight_, TRUE);
        splitters_[1].area = {left, y + listHeight_, width, y + listHeight_ + pad};
        y += listHeight_ + pad;

        // the header strip is painted in WM_PAINT
        header_ = {left, y, width, y + Scale(8) + 4 * kHeaderLine};
        InvalidateRect(hwnd_, &header_, TRUE);
        y = header_.bottom + pad;
        if (IsWindowVisible(infoBar_)) {
            MoveWindow(infoBar_, left, y, width - left, Scale(24), TRUE);
            y += Scale(24) + pad;
        }
        MoveWindow(previewHost_, left, y, width - left, height - y, TRUE);
        preview_.Resize({0, 0, width - left, height - y});
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void OnCommand(int id, int code, HWND) override {
        if (id == IDC_INFO_BAR && code == STN_CLICKED) {
            ShowRemoteImages();
            return;
        }
        if (id >= IDM_MOVE_FIRST && id < IDM_MOVE_FIRST + static_cast<int>(folders_.size())) {
            const std::string& target = folders_[id - IDM_MOVE_FIRST].path;
            if (target != current_) {
                CallForSelection("moveMessages", {{"target", target}}, L"Move");
            }
            return;
        }
        if (id >= IDM_ATTACHMENT_FIRST && id < IDM_ATTACHMENT_FIRST + kMaxAttachmentItems) {
            SaveAttachment(id - IDM_ATTACHMENT_FIRST);
            return;
        }
        switch (id) {
            case IDC_NEW_MAIL:
                NewMessage();
                break;
            case IDC_IMPORT:
                ImportFiles(OpenFilesDialog(hwnd_, L"Import messages", kEmlFilter));
                break;
            case IDC_DELETE:
                CallForSelection("deleteMessages", json::object(), L"Delete");
                break;
            case IDC_MARK_READ:
            case IDC_MARK_UNREAD:
                CallForSelection("setFlags", {{"flags", {"\\Seen"}}, {"mode", id == IDC_MARK_READ ? "add" : "remove"}}, L"Mark");
                break;
            case IDM_FLAG:
            case IDM_UNFLAG:
                CallForSelection("setFlags", {{"flags", {"\\Flagged"}}, {"mode", id == IDM_FLAG ? "add" : "remove"}}, L"Flag");
                break;
            case IDM_SAVE_AS:
                SaveSelected();
                break;
            case IDM_SAVE_ALL_ATTACHMENTS:
                SaveAllAttachments();
                break;
            case IDM_NEW_FOLDER:
                NewFolder();
                break;
            case IDM_RENAME_FOLDER:
                RenameFolder();
                break;
            case IDM_DELETE_FOLDER:
                DeleteFolder();
                break;
            case IDC_SOURCE:
                showSource_ = !showSource_;
                SendMessageW(toolbar_, TB_CHECKBUTTON, IDC_SOURCE, showSource_);
                ShowCurrentDocument();
                break;
            case IDC_REFRESH:
                listDirty_ = true;
                Refresh();
                break;
        }
    }

    LRESULT OnNotify(NMHDR* header) override {
        if (header->hwndFrom == tree_ && header->code == TVN_SELCHANGEDW) {
            const Folder* folder = FolderOf(reinterpret_cast<NMTREEVIEWW*>(header)->itemNew.hItem);
            if (!selectingFolder_ && folder && folder->path != current_) {
                OpenFolder(*folder);
            }
            return 0;
        }
        if (header->hwndFrom == list_) {
            return OnListNotify(header);
        }
        if (header->code == TTN_GETDISPINFOW) {
            auto* tip = reinterpret_cast<NMTTDISPINFOW*>(header);
            if (tip->hdr.idFrom == IDC_IMPORT) {
                tip->lpszText = const_cast<wchar_t*>(L"Import .eml files into this folder (or drop them on the window)");
            } else if (tip->hdr.idFrom == IDC_SOURCE) {
                tip->lpszText = const_cast<wchar_t*>(L"Show the message source");
            }
        }
        return 0;
    }

    LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam, bool& handled) override {
        switch (message) {
            case WM_SHOWWINDOW:
                preview_.SetVisible(wParam != FALSE);
                handled = false;
                return 0;
            case WM_CONTEXTMENU: {
                POINT at = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (reinterpret_cast<HWND>(wParam) == list_) {
                    ShowMessageMenu(at);
                } else if (reinterpret_cast<HWND>(wParam) == tree_) {
                    ShowFolderMenu(at);
                } else {
                    handled = false;
                }
                return 0;
            }
            case WM_TIMER:
                KillTimer(hwnd_, kRefreshTimer);
                Refresh();
                return 0;
            case WM_SETCURSOR:
                if (reinterpret_cast<HWND>(wParam) == infoBar_ || (reinterpret_cast<HWND>(wParam) == hwnd_ && OverAttachments())) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
                break;
            case WM_LBUTTONDOWN: {
                if (OverAttachments()) {
                    POINT at = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                    ClientToScreen(hwnd_, &at);
                    ShowAttachmentMenu(at);
                    return 0;
                }
                break;
            }
            case WM_CTLCOLORSTATIC:
                if (reinterpret_cast<HWND>(lParam) == infoBar_) {
                    // the yellow of a classic tooltip
                    static HBRUSH infoBrush = CreateSolidBrush(GetSysColor(COLOR_INFOBK));
                    HDC dc = reinterpret_cast<HDC>(wParam);
                    SetTextColor(dc, GetSysColor(COLOR_INFOTEXT));
                    SetBkColor(dc, GetSysColor(COLOR_INFOBK));
                    return reinterpret_cast<LRESULT>(infoBrush);
                }
                if (reinterpret_cast<HWND>(lParam) == folderBar_) {
                    // the dark folder bar of Outlook Express 5
                    static HBRUSH brush = CreateSolidBrush(RGB(128, 128, 128));
                    HDC dc = reinterpret_cast<HDC>(wParam);
                    SetTextColor(dc, RGB(255, 255, 255));
                    SetBkColor(dc, RGB(128, 128, 128));
                    return reinterpret_cast<LRESULT>(brush);
                }
                break;
            case WM_PAINT: {
                PAINTSTRUCT paint;
                HDC dc = BeginPaint(hwnd_, &paint);
                PaintHeader(dc);
                EndPaint(hwnd_, &paint);
                return 0;
            }
            case WM_DROPFILES: {
                HDROP drop = reinterpret_cast<HDROP>(wParam);
                UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                std::vector<std::wstring> files;
                for (UINT i = 0; i < count; i++) {
                    wchar_t file[MAX_PATH];
                    DragQueryFileW(drop, i, file, MAX_PATH);
                    files.push_back(file);
                }
                DragFinish(drop);
                ImportFiles(files);
                return 0;
            }
        }
        handled = false;
        return 0;
    }

private:
    LRESULT OnListNotify(NMHDR* header) {
        switch (header->code) {
            case LVN_GETDISPINFOW:
                FillMessageItem(reinterpret_cast<NMLVDISPINFOW*>(header)->item);
                return 0;
            case LVN_ITEMCHANGED:
            case LVN_ODSTATECHANGED:
                if (!rebuilding_) {
                    OnSelectionChanged();
                }
                return 0;
            case LVN_COLUMNCLICK: {
                int column = reinterpret_cast<NMLISTVIEW*>(header)->iSubItem;
                // a new column sorts ascending, but dates newest first
                sortAscending_ = column == sortColumn_ ? !sortAscending_ : column != kReceivedColumn;
                sortColumn_ = column;
                FillList();
                return 0;
            }
            case LVN_KEYDOWN:
                if (reinterpret_cast<NMLVKEYDOWN*>(header)->wVKey == VK_DELETE) {
                    OnCommand(IDC_DELETE, 0, nullptr);
                }
                return 0;
            case NM_CUSTOMDRAW: {
                auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(header);
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
                    return CDRF_NOTIFYITEMDRAW;
                }
                int row = static_cast<int>(draw->nmcd.dwItemSpec);
                const Summary* message = MessageAt(row);
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && message) {
                    LRESULT result = message->attachments ? CDRF_NOTIFYPOSTPAINT : CDRF_DODEFAULT;
                    // unread messages in bold, as in Outlook Express
                    if (!message->seen) {
                        SelectObject(draw->nmcd.hdc, BoldFont());
                        result |= CDRF_NEWFONT;
                    }
                    return result;
                }
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPOSTPAINT && message && message->attachments) {
                    RECT item;
                    ListView_GetItemRect(list_, row, &item, LVIR_BOUNDS);
                    RECT cell = {item.left, item.top, item.left + ListView_GetColumnWidth(list_, kAttachmentColumn), item.bottom};
                    bool highlighted = (ListView_GetItemState(list_, row, LVIS_SELECTED) & LVIS_SELECTED) && GetFocus() == list_;
                    DrawPaperclip(draw->nmcd.hdc, cell, GetSysColor(highlighted ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
                }
                return CDRF_DODEFAULT;
            }
        }
        return 0;
    }

    void CreateToolbar() {
        toolbar_ = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr, WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER,
                                   0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(IDC_TOOLBAR), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(toolbar_, WM_SETFONT, reinterpret_cast<WPARAM>(UiFont()), TRUE);
        SendMessageW(toolbar_, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
        // the large standard images of the common controls, the closest built-in match to the Outlook Express toolbar,
        // and the envelopes of app.rc after them
        HIMAGELIST images = ImageList_Create(24, 24, ILC_COLOR24 | ILC_MASK, 18, 0);
        SendMessageW(toolbar_, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(images));
        SendMessageW(toolbar_, TB_LOADIMAGES, IDB_STD_LARGE_COLOR, reinterpret_cast<LPARAM>(HINST_COMMCTRL));
        int envelopes = ImageList_GetImageCount(images);
        HBITMAP strip = static_cast<HBITMAP>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(201), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
        ImageList_AddMasked(images, strip, RGB(255, 0, 255));
        DeleteObject(strip);

        struct ButtonDef {
            int id;
            int image;
            const wchar_t* text;
            BYTE style;
        };
        const ButtonDef defs[] = {
            {IDC_NEW_MAIL, envelopes, L"New Mail", BTNS_BUTTON},
            {IDC_IMPORT, STD_FILEOPEN, L"Import", BTNS_BUTTON},
            {0, 0, nullptr, BTNS_SEP},
            {IDC_DELETE, STD_DELETE, L"Delete", BTNS_BUTTON},
            {IDC_MARK_READ, envelopes + 1, L"Mark Read", BTNS_BUTTON},
            {IDC_MARK_UNREAD, envelopes + 2, L"Mark Unread", BTNS_BUTTON},
            {0, 0, nullptr, BTNS_SEP},
            {IDC_SOURCE, STD_FIND, L"Source", BTNS_CHECK},
            {IDC_REFRESH, STD_REDOW, L"Refresh", BTNS_BUTTON},
        };
        std::vector<TBBUTTON> buttons;
        for (const ButtonDef& def : defs) {
            TBBUTTON button = {};
            button.iBitmap = def.image;
            button.idCommand = def.id;
            button.fsState = TBSTATE_ENABLED;
            button.fsStyle = def.style;
            button.iString = def.text ? reinterpret_cast<INT_PTR>(def.text) : -1;
            buttons.push_back(button);
        }
        SendMessageW(toolbar_, TB_ADDBUTTONSW, buttons.size(), reinterpret_cast<LPARAM>(buttons.data()));
        SendMessageW(toolbar_, TB_SETBUTTONSIZE, 0, MAKELPARAM(Scale(76), Scale(46)));
        SendMessageW(toolbar_, TB_AUTOSIZE, 0, 0);
    }

    // ---- following the changes of the sidecar ----

    void OnStore(const json& data) {
        std::string type = JsonString(data, "type");
        std::string path = JsonString(data, "path");
        if (type == "mailbox" && !current_.empty() && JsonString(data, "oldPath") == current_) {
            // the open folder was renamed, by a client or here
            current_ = path;
        }
        auto messages = data.find("messages");
        if (type == "flags" && path == current_ && messages != data.end()) {
            PatchFlags(*messages);
        } else if (path.empty() || path == current_) {
            listDirty_ = true;
        }
        // unread counts and the tree change with almost everything
        foldersDirty_ = true;
        if (IsWindowVisible(hwnd_)) {
            // changes come in bursts (a client moving 100 messages), refresh once after them
            SetTimer(hwnd_, kRefreshTimer, 250, nullptr);
        }
    }

    void Refresh() {
        foldersDirty_ = false;
        LoadFolders();
        if (listDirty_) {
            LoadMessages();
        }
    }

    // flag changes come with the new flags, the rows change without a reload
    void PatchFlags(const json& changes) {
        for (const auto& change : changes) {
            auto found = uidIndex_.find(change.value("uid", 0));
            if (found == uidIndex_.end()) {
                continue;
            }
            Summary& message = messages_[found->second];
            const json& flags = change["flags"];
            message.seen = std::find(flags.begin(), flags.end(), "\\Seen") != flags.end();
            message.flagged = std::find(flags.begin(), flags.end(), "\\Flagged") != flags.end();
        }
        InvalidateRect(list_, nullptr, FALSE);
    }

    // ---- folders ----

    void LoadFolders() {
        TheSidecar().Call("mailboxes", json::object(), [this](const json& result, const std::string& error) {
            if (!error.empty() || !result.is_array()) {
                return;
            }
            std::vector<Folder> folders;
            for (const auto& mailbox : result) {
                folders.push_back({JsonString(mailbox, "path"), JsonString(mailbox, "parent"), JsonText(mailbox, "name"), mailbox.value("unseen", 0),
                                   mailbox.value("canRename", false), mailbox.value("canDelete", false)});
            }
            bool samePaths = std::equal(folders.begin(), folders.end(), folders_.begin(), folders_.end(),
                                        [](const Folder& a, const Folder& b) { return a.path == b.path; });
            folders_ = std::move(folders);
            if (samePaths) {
                for (size_t i = 0; i < folders_.size(); i++) {
                    UpdateTreeItem(treeItems_[i], folders_[i]);
                }
            } else {
                BuildTree();
            }
        });
    }

    void UpdateTreeItem(HTREEITEM handle, const Folder& folder) {
        std::wstring text = folder.unseen > 0 ? folder.name + L" (" + std::to_wstring(folder.unseen) + L")" : folder.name;
        TVITEMW item = {};
        item.mask = TVIF_TEXT | TVIF_STATE | TVIF_HANDLE;
        item.hItem = handle;
        item.pszText = text.data();
        item.stateMask = TVIS_BOLD;
        item.state = folder.unseen > 0 ? TVIS_BOLD : 0;
        TreeView_SetItem(tree_, &item);
    }

    void BuildTree() {
        TreeView_DeleteAllItems(tree_);
        treeItems_.clear();
        TVINSERTSTRUCTW root = {};
        root.hParent = TVI_ROOT;
        root.hInsertAfter = TVI_LAST;
        root.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE;
        root.item.pszText = const_cast<wchar_t*>(L"Local Folders");
        root.item.lParam = -1;
        root.item.stateMask = TVIS_EXPANDED;
        root.item.state = TVIS_EXPANDED;
        HTREEITEM rootItem = TreeView_InsertItem(tree_, &root);

        // listMailboxes lists a parent before its children
        std::unordered_map<std::string, HTREEITEM> byPath;
        for (size_t i = 0; i < folders_.size(); i++) {
            const Folder& folder = folders_[i];
            auto parent = byPath.find(folder.parent);
            TVINSERTSTRUCTW insert = {};
            insert.hParent = parent == byPath.end() ? rootItem : parent->second;
            insert.hInsertAfter = TVI_LAST;
            insert.item.mask = TVIF_TEXT | TVIF_PARAM;
            insert.item.pszText = const_cast<wchar_t*>(folder.name.c_str());
            insert.item.lParam = static_cast<LPARAM>(i);
            HTREEITEM handle = TreeView_InsertItem(tree_, &insert);
            byPath[folder.path] = handle;
            treeItems_.push_back(handle);
            UpdateTreeItem(handle, folder);
            TreeView_Expand(tree_, insert.hParent, TVE_EXPAND);
        }

        // the folder that was open, or Inbox when it is gone (deleted by a client) or nothing was open yet
        auto open = byPath.find(current_);
        if (open == byPath.end()) {
            open = byPath.find("INBOX");
        }
        if (open != byPath.end()) {
            selectingFolder_ = true;
            TreeView_SelectItem(tree_, open->second);
            selectingFolder_ = false;
            OpenFolder(*FolderOf(open->second));
        }
    }

    const Folder* FolderOf(HTREEITEM handle) {
        if (!handle) {
            return nullptr;
        }
        TVITEMW item = {};
        item.mask = TVIF_PARAM;
        item.hItem = handle;
        TreeView_GetItem(tree_, &item);
        size_t index = static_cast<size_t>(item.lParam);
        return index < folders_.size() ? &folders_[index] : nullptr;
    }

    const Folder* SelectedFolder() { return FolderOf(TreeView_GetSelection(tree_)); }

    void OpenFolder(const Folder& folder) {
        bool changed = folder.path != current_;
        current_ = folder.path;
        SetWindowTextW(folderBar_, (L"  " + folder.name).c_str());
        if (changed) {
            ClearPreview();
        }
        LoadMessages();
    }

    // ---- the message list ----

    void LoadMessages() {
        listDirty_ = false;
        if (current_.empty()) {
            return;
        }
        std::string path = current_;
        TheSidecar().Call("messages", {{"path", path}}, [this, path](const json& result, const std::string& error) {
            if (path != current_) {
                return;
            }
            messages_.clear();
            uidIndex_.clear();
            if (error.empty() && result.is_array()) {
                for (const auto& entry : result) {
                    Summary summary;
                    summary.uid = entry.value("uid", 0);
                    for (const auto& flag : entry.value("flags", json::array())) {
                        summary.seen |= flag == "\\Seen";
                        summary.flagged |= flag == "\\Flagged";
                    }
                    summary.from = JsonText(entry, "fromName");
                    summary.subject = JsonText(entry, "subject");
                    summary.receivedIso = JsonString(entry, "received");
                    summary.received = FormatTime(summary.receivedIso);
                    summary.size = entry.value("size", 0.0);
                    summary.attachments = entry.value("hasAttachments", false);
                    uidIndex_[summary.uid] = messages_.size();
                    messages_.push_back(std::move(summary));
                }
            }
            FillList();
        });
    }

    bool Before(const Summary& a, const Summary& b) const {
        switch (sortColumn_) {
            case kAttachmentColumn:
                return a.attachments < b.attachments;
            case kFlagColumn:
                return a.flagged < b.flagged;
            case kFromColumn:
                return _wcsicmp(a.from.c_str(), b.from.c_str()) < 0;
            case kSubjectColumn:
                return _wcsicmp(a.subject.c_str(), b.subject.c_str()) < 0;
            case kSizeColumn:
                return a.size < b.size;
            default:
                return a.receivedIso < b.receivedIso;
        }
    }

    void FillList() {
        std::vector<int> selected = SelectedUids();
        order_.resize(messages_.size());
        for (size_t i = 0; i < order_.size(); i++) {
            order_[i] = i;
        }
        std::stable_sort(order_.begin(), order_.end(), [this](size_t a, size_t b) {
            return sortAscending_ ? Before(messages_[a], messages_[b]) : Before(messages_[b], messages_[a]);
        });

        rebuilding_ = true;
        ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemCountEx(list_, static_cast<int>(order_.size()), LVSICF_NOSCROLL);
        bool keepsPreview = false;
        for (size_t row = 0; row < order_.size(); row++) {
            int uid = messages_[order_[row]].uid;
            if (std::find(selected.begin(), selected.end(), uid) != selected.end()) {
                ListView_SetItemState(list_, static_cast<int>(row), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                keepsPreview |= uid == selectedUid_;
            }
        }
        rebuilding_ = false;
        InvalidateRect(list_, nullptr, TRUE);

        // the previewed message may have gone (deleted or moved by a client)
        if (selectedUid_ && !keepsPreview) {
            ClearPreview();
        }
    }

    const Summary* MessageAt(int row) const {
        return row >= 0 && row < static_cast<int>(order_.size()) ? &messages_[order_[row]] : nullptr;
    }

    void FillMessageItem(LVITEMW& item) {
        const Summary* message = MessageAt(item.iItem);
        if (!message) {
            return;
        }
        if (!(item.mask & LVIF_TEXT)) {
            return;
        }
        const wchar_t* text = L"";
        std::wstring size;
        switch (item.iSubItem) {
            case kFlagColumn:
                text = message->flagged ? L"!" : L"";
                break;
            case kFromColumn:
                text = message->from.c_str();
                break;
            case kSubjectColumn:
                text = message->subject.c_str();
                break;
            case kReceivedColumn:
                text = message->received.c_str();
                break;
            case kSizeColumn:
                size = FormatSize(message->size);
                text = size.c_str();
                break;
        }
        wcsncpy_s(item.pszText, item.cchTextMax, text, _TRUNCATE);
    }

    std::vector<int> SelectedUids() {
        std::vector<int> uids;
        int row = -1;
        while ((row = ListView_GetNextItem(list_, row, LVNI_SELECTED)) >= 0) {
            if (const Summary* message = MessageAt(row)) {
                uids.push_back(message->uid);
            }
        }
        return uids;
    }

    // runs a sidecar method on the selected messages of the open folder
    void CallForSelection(const char* method, json params, const wchar_t* action) {
        std::vector<int> uids = SelectedUids();
        if (uids.empty() || current_.empty()) {
            return;
        }
        params["path"] = current_;
        params["uids"] = uids;
        TheSidecar().Call(method, params, ErrorReporter(hwnd_, action));
    }

    // ---- the preview ----

    void OnSelectionChanged() {
        std::vector<int> uids = SelectedUids();
        if (uids.size() != 1) {
            if (uids.empty()) {
                ClearPreview();
            }
            return;
        }
        int uid = uids[0];
        if (uid == selectedUid_) {
            return;
        }
        selectedUid_ = uid;
        std::string path = current_;
        TheSidecar().Call("message", {{"path", path}, {"uid", uid}, {"markSeen", true}}, [this, path, uid](const json& result, const std::string& error) {
            if (path != current_ || uid != selectedUid_) {
                return;
            }
            if (!error.empty()) {
                ClearPreview();
                return;
            }
            currentMessage_ = result;
            sourceHtml_.clear();
            remoteImages_ = false;
            SetInfoBar(result.value("remoteImages", false));
            std::wstring to = JsonText(result, "to");
            std::wstring cc = JsonText(result, "cc");
            std::wstring files;
            for (const auto& attachment : CurrentAttachments()) {
                files += (files.empty() ? L"" : L", ") + AttachmentText(attachment);
            }
            SetHeader({JsonText(result, "from"), cc.empty() ? to : to + L"; Cc: " + cc, JsonText(result, "subject"), files});
            ShowCurrentDocument();
        });
    }

    json CurrentAttachments() const {
        return currentMessage_.is_object() ? currentMessage_.value("attachments", json::array()) : json::array();
    }

    void ShowCurrentDocument() {
        if (!currentMessage_.is_object()) {
            return;
        }
        if (!showSource_) {
            preview_.Show(JsonText(currentMessage_, "previewHtml"), remoteImages_);
            return;
        }
        if (!sourceHtml_.empty()) {
            preview_.Show(sourceHtml_);
            return;
        }
        // the source is loaded when it is asked for, most messages are only previewed
        std::string path = current_;
        int uid = selectedUid_;
        TheSidecar().Call("messageSource", {{"path", path}, {"uid", uid}}, [this, path, uid](const json& result, const std::string& error) {
            if (error.empty() && path == current_ && uid == selectedUid_ && showSource_) {
                sourceHtml_ = JsonText(result, "sourceHtml");
                preview_.Show(sourceHtml_);
            }
        });
    }

    // loads the preview again with the images from the network, for this message only
    void ShowRemoteImages() {
        std::string path = current_;
        int uid = selectedUid_;
        TheSidecar().Call("message", {{"path", path}, {"uid", uid}, {"remoteImages", true}}, [this, path, uid](const json& result, const std::string& error) {
            if (!error.empty() || path != current_ || uid != selectedUid_) {
                return;
            }
            currentMessage_ = result;
            remoteImages_ = true;
            SetInfoBar(false);
            ShowCurrentDocument();
        });
    }

    // From, To, Subject and the attachments line of the previewed message
    void SetHeader(std::array<std::wstring, 4> values) {
        headerValues_ = std::move(values);
        InvalidateRect(hwnd_, &header_, TRUE);
    }

    // the raised strip with the headers of the previewed message, painted here rather than with label controls,
    // which covered its border and left old text behind when they moved
    void PaintHeader(HDC dc) {
        RECT strip = header_;
        DrawEdge(dc, &strip, EDGE_ETCHED, BF_RECT);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        int labelRight = header_.left + Scale(60);
        int valueLeft = labelRight + Scale(6);
        const wchar_t* labels[] = {L"From:", L"To:", L"Subject:", headerValues_[3].empty() ? L"" : L"Attach:"};
        for (int i = 0; i < 4; i++) {
            int top = header_.top + Scale(4) + i * kHeaderLine;
            RECT label = {header_.left + Scale(4), top, labelRight, top + kHeaderLine};
            RECT value = {valueLeft, top, header_.right - Scale(6), top + kHeaderLine};
            SelectObject(dc, BoldFont());
            DrawTextW(dc, labels[i], -1, &label, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            SelectObject(dc, UiFont());
            DrawTextW(dc, headerValues_[i].c_str(), -1, &value, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
    }

    void SetInfoBar(bool visible) {
        if ((IsWindowVisible(infoBar_) != FALSE) != visible) {
            ShowWindow(infoBar_, visible ? SW_SHOW : SW_HIDE);
            RECT client;
            GetClientRect(hwnd_, &client);
            OnSize(client.right, client.bottom);
        }
    }

    void ClearPreview() {
        selectedUid_ = 0;
        currentMessage_ = nullptr;
        sourceHtml_.clear();
        remoteImages_ = false;
        SetInfoBar(false);
        SetHeader({});
        preview_.Clear();
    }

    // ---- menus ----

    void ShowMessageMenu(POINT at) {
        size_t count = SelectedUids().size();
        UINT enabled = count ? 0 : MF_GRAYED;
        HMENU move = CreatePopupMenu();
        for (size_t i = 0; i < folders_.size(); i++) {
            AppendMenuW(move, MF_STRING | (folders_[i].path == current_ ? MF_GRAYED : 0), IDM_MOVE_FIRST + i, folders_[i].name.c_str());
        }
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | enabled, IDC_MARK_READ, L"Mark as &Read");
        AppendMenuW(menu, MF_STRING | enabled, IDC_MARK_UNREAD, L"Mark as &Unread");
        AppendMenuW(menu, MF_STRING | enabled, IDM_FLAG, L"&Flag");
        AppendMenuW(menu, MF_STRING | enabled, IDM_UNFLAG, L"U&nflag");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_POPUP | enabled, reinterpret_cast<UINT_PTR>(move), L"&Move to");
        AppendMenuW(menu, MF_STRING | (count == 1 ? 0 : MF_GRAYED), IDM_SAVE_AS, L"&Save As...");
        if (count == 1 && !CurrentAttachments().empty()) {
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(AttachmentMenu()), L"Save a&ttachment");
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | enabled, IDC_DELETE, L"&Delete\tDel");
        RECT anchor = {};
        int focused = ListView_GetNextItem(list_, -1, LVNI_FOCUSED);
        if (focused >= 0) {
            ListView_GetItemRect(list_, focused, &anchor, LVIR_LABEL);
        }
        TrackContextMenu(menu, hwnd_, list_, at, anchor);
    }

    // the header of the list sends its paint notifications to the list, not to the page: the paperclip of the first
    // column is drawn here
    static LRESULT CALLBACK ListProc(HWND list, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NOTIFY) {
            auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
            if (draw->hdr.hwndFrom == ListView_GetHeader(list) && draw->hdr.code == NM_CUSTOMDRAW) {
                if (draw->dwDrawStage == CDDS_PREPAINT) {
                    return CDRF_NOTIFYITEMDRAW;
                }
                if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
                    return draw->dwItemSpec == kAttachmentColumn ? CDRF_NOTIFYPOSTPAINT : CDRF_DODEFAULT;
                }
                if (draw->dwDrawStage == CDDS_ITEMPOSTPAINT) {
                    DrawPaperclip(draw->hdc, draw->rc, GetSysColor(COLOR_BTNTEXT));
                    return CDRF_DODEFAULT;
                }
            }
        }
        return CallWindowProcW(listProc_, list, message, wParam, lParam);
    }

    // a paperclip in the middle of a cell, in the classic outline shape at 45 degrees: one wire with two nested
    // bends and both ends open. Drawn antialiased with lines, so it has no background and scales with the DPI
    static void DrawPaperclip(HDC dc, const RECT& cell, COLORREF color) {
        // the centerline in a 512 by 512 box, in coordinates along the clip (u, toward the top right) and across it
        // (v): u = x - y, v = x + y. Four legs at v = 315, 425, 580 and 695
        std::vector<Gdiplus::PointF> points;
        // a few pixels smaller than the cell, rows are only about as high as the text
        const double size = std::min({cell.right - cell.left, cell.bottom - cell.top, static_cast<LONG>(Scale(16))}) - Scale(5);
        const double scale = std::max(size, static_cast<double>(Scale(8))) / 512.0;
        const double cx = (cell.left + cell.right) / 2.0;
        const double cy = (cell.top + cell.bottom) / 2.0;
        auto add = [&](double u, double v) {
            double x = (u + v) / 2;
            double y = (v - u) / 2;
            points.emplace_back(static_cast<Gdiplus::REAL>(cx + (x - 256) * scale), static_cast<Gdiplus::REAL>(cy + (y - 256) * scale));
        };
        auto bend = [&](double u, double v, double radius, double from, double to) {
            for (int i = 0; i <= 16; i++) {
                double angle = from + (to - from) * i / 16;
                add(u + radius * sin(angle), v + radius * cos(angle));
            }
        };
        const double pi = 3.14159265358979;
        // the open outer end, down to the big bottom bend, up the outer leg, over the top bend, down to the small
        // bend and up to the open inner end
        add(295, 695);
        bend(-265, 505, 190, 0, -pi);
        bend(297.5, 447.5, 132.5, pi, 0);
        bend(-202, 502.5, 77.5, 0, -pi);
        add(195, 425);

        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)), static_cast<Gdiplus::REAL>(std::max(1.0, 30 * scale)));
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        graphics.DrawLines(&pen, points.data(), static_cast<INT>(points.size()));
    }

    // "name (size)", and "embedded" for an image that the HTML shows
    static std::wstring AttachmentText(const json& attachment) {
        return JsonText(attachment, "filename") + L" (" + FormatSize(attachment.value("size", 0.0)) +
               (attachment.value("inline", false) ? L", embedded)" : L")");
    }

    // every attachment of the previewed message, embedded images too, and Save All
    HMENU AttachmentMenu() {
        HMENU menu = CreatePopupMenu();
        json attachments = CurrentAttachments();
        for (size_t i = 0; i < attachments.size() && i < kMaxAttachmentItems; i++) {
            AppendMenuW(menu, MF_STRING, IDM_ATTACHMENT_FIRST + i, (L"Save " + AttachmentText(attachments[i]) + L"...").c_str());
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_SAVE_ALL_ATTACHMENTS, L"Save &All...");
        return menu;
    }

    void ShowAttachmentMenu(POINT at) {
        HMENU menu = AttachmentMenu();
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, at.x, at.y, 0, hwnd_, nullptr);
        DestroyMenu(menu);
    }

    // the mouse is on the Attach: line of the header strip
    bool OverAttachments() {
        if (headerValues_[3].empty()) {
            return false;
        }
        POINT cursor;
        GetCursorPos(&cursor);
        ScreenToClient(hwnd_, &cursor);
        int top = header_.top + Scale(4) + 3 * kHeaderLine;
        RECT line = {header_.left, top, header_.right, top + kHeaderLine};
        return PtInRect(&line, cursor) != FALSE;
    }

    void SaveAllAttachments() {
        std::wstring dir = FolderDialog(hwnd_, L"Save the attachments to this folder");
        if (!dir.empty() && selectedUid_) {
            TheSidecar().Call("saveAttachments", {{"path", current_}, {"uid", selectedUid_}, {"dir", Narrow(dir)}}, ErrorReporter(hwnd_, L"Save attachments"));
        }
    }

    void ShowFolderMenu(POINT at) {
        // the menu is for the folder under the mouse, select it first like Explorer does
        if (at.x != -1 || at.y != -1) {
            TVHITTESTINFO hit = {};
            hit.pt = at;
            ScreenToClient(tree_, &hit.pt);
            if (HTREEITEM item = TreeView_HitTest(tree_, &hit)) {
                TreeView_SelectItem(tree_, item);
            }
        }
        const Folder* folder = SelectedFolder();
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, IDM_NEW_FOLDER, L"&New Folder...");
        AppendMenuW(menu, MF_STRING | (folder && folder->canRename ? 0 : MF_GRAYED), IDM_RENAME_FOLDER, L"&Rename...");
        AppendMenuW(menu, MF_STRING | (folder && folder->canDelete ? 0 : MF_GRAYED), IDM_DELETE_FOLDER, L"&Delete");
        RECT anchor = {};
        if (HTREEITEM selected = TreeView_GetSelection(tree_)) {
            TreeView_GetItemRect(tree_, selected, &anchor, TRUE);
        }
        TrackContextMenu(menu, hwnd_, tree_, at, anchor);
    }

    // ---- actions ----

    void NewFolder() {
        const Folder* parent = SelectedFolder();
        std::vector<PromptField> fields = {{L"Name:", L""}};
        if (!PromptDialog(hwnd_, parent ? L"New folder in " + parent->name : L"New folder", fields) || fields[0].value.empty()) {
            return;
        }
        TheSidecar().Call("createMailbox", {{"parent", parent ? json(parent->path) : json()}, {"name", Narrow(fields[0].value)}},
                          ErrorReporter(hwnd_, L"New folder"));
    }

    void RenameFolder() {
        const Folder* folder = SelectedFolder();
        if (!folder || !folder->canRename) {
            return;
        }
        std::vector<PromptField> fields = {{L"New name:", folder->name}};
        if (PromptDialog(hwnd_, L"Rename folder", fields) && !fields[0].value.empty()) {
            TheSidecar().Call("renameMailbox", {{"path", folder->path}, {"name", Narrow(fields[0].value)}}, ErrorReporter(hwnd_, L"Rename"));
        }
    }

    void DeleteFolder() {
        const Folder* folder = SelectedFolder();
        if (!folder || !folder->canDelete) {
            return;
        }
        std::wstring question = L"Delete the folder \"" + folder->name + L"\" and every message in it?";
        if (MessageBoxW(hwnd_, question.c_str(), L"Delete folder", MB_ICONWARNING | MB_OKCANCEL) == IDOK) {
            TheSidecar().Call("deleteMailbox", {{"path", folder->path}}, ErrorReporter(hwnd_, L"Delete folder"));
        }
    }

    void NewMessage() {
        const Folder* folder = SelectedFolder();
        std::string path = folder ? folder->path : "INBOX";
        std::vector<PromptField> fields = {{L"From:", L"Test Sender <sender@example.com>"},
                                           {L"To:", L"Test User <testuser@imapkit.local>"},
                                           {L"Cc:", L""},
                                           {L"Subject:", L""},
                                           {L"Message:", L"", true}};
        if (!PromptDialog(hwnd_, L"New message in " + (folder ? folder->name : L"Inbox"), fields, 520)) {
            return;
        }
        TheSidecar().Call("composeMessage",
                          {{"path", path},
                           {"from", Narrow(fields[0].value)},
                           {"to", Narrow(fields[1].value)},
                           {"cc", Narrow(fields[2].value)},
                           {"subject", Narrow(fields[3].value)},
                           {"text", Narrow(fields[4].value)}},
                          ErrorReporter(hwnd_, L"New message"));
    }

    void SaveSelected() {
        std::vector<int> uids = SelectedUids();
        if (uids.size() != 1) {
            return;
        }
        std::wstring subject = currentMessage_.is_object() ? JsonText(currentMessage_, "subject") : L"";
        std::wstring file = SaveFileDialog(hwnd_, L"Save message", L"Email messages (*.eml)\0*.eml\0", L"eml", subject.empty() ? L"message" : subject);
        if (!file.empty()) {
            TheSidecar().Call("saveMessage", {{"path", current_}, {"uid", uids[0]}, {"file", Narrow(file)}}, ErrorReporter(hwnd_, L"Save"));
        }
    }

    void SaveAttachment(int index) {
        json attachments = CurrentAttachments();
        if (index >= static_cast<int>(attachments.size()) || !selectedUid_) {
            return;
        }
        std::wstring file = SaveFileDialog(hwnd_, L"Save attachment", L"All files (*.*)\0*.*\0", nullptr, JsonText(attachments[index], "filename"));
        if (!file.empty()) {
            TheSidecar().Call("saveAttachment", {{"path", current_}, {"uid", selectedUid_}, {"index", index}, {"file", Narrow(file)}},
                              ErrorReporter(hwnd_, L"Save attachment"));
        }
    }

    void ImportFiles(const std::vector<std::wstring>& files) {
        if (files.empty()) {
            return;
        }
        json paths = json::array();
        for (const std::wstring& file : files) {
            paths.push_back(Narrow(file));
        }
        TheSidecar().Call("importMessages", {{"path", current_.empty() ? "INBOX" : current_}, {"files", paths}}, ErrorReporter(hwnd_, L"Import"));
    }

    HWND toolbar_ = nullptr, tree_ = nullptr, folderBar_ = nullptr, list_ = nullptr, previewHost_ = nullptr;
    HWND infoBar_ = nullptr;
    RECT header_ = {};
    std::array<std::wstring, 4> headerValues_;
    int treeWidth_ = 0, listHeight_ = 0, listTop_ = 0;
    Preview preview_;

    std::vector<Folder> folders_;
    std::vector<HTREEITEM> treeItems_;
    std::vector<Summary> messages_;
    std::unordered_map<int, size_t> uidIndex_;
    // rows of the list as positions in messages_
    std::vector<size_t> order_;
    std::string current_;
    int selectedUid_ = 0;
    json currentMessage_;
    std::wstring sourceHtml_;
    bool showSource_ = false;
    // the user asked for the images from the network of the previewed message
    bool remoteImages_ = false;
    bool listDirty_ = false;
    bool foldersDirty_ = false;
    bool rebuilding_ = false;
    bool selectingFolder_ = false;
    int sortColumn_ = kReceivedColumn;
    static inline WNDPROC listProc_ = nullptr;
    bool sortAscending_ = false;
};

}  // namespace

Page* CreateMessagesPage() {
    return new MessagesPage();
}
