// A small modal form for names, accounts and new messages, file dialogs and the clipboard
#include "app.h"

#include <commdlg.h>
#include <shlobj.h>

#include <fstream>

namespace {

const wchar_t* kPromptClass = L"ImapKitDesktopPrompt";

struct PromptState {
    std::vector<PromptField>* fields;
    std::vector<HWND> edits;
    bool done = false;
    bool accepted = false;
};

LRESULT CALLBACK PromptProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<PromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (message) {
        case WM_COMMAND:
            if (state && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)) {
                state->accepted = LOWORD(wParam) == IDOK;
                if (state->accepted) {
                    for (size_t i = 0; i < state->edits.size(); i++) {
                        (*state->fields)[i].value = GetWindowString(state->edits[i]);
                    }
                }
                state->done = true;
            }
            return 0;
        case WM_CLOSE:
            if (state) {
                state->done = true;
            }
            return 0;
        case WM_CTLCOLORSTATIC:
            return ButtonFaceColor(reinterpret_cast<HDC>(wParam));
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

bool PromptDialog(HWND owner, const std::wstring& title, std::vector<PromptField>& fields, int width) {
    RegisterPlainClass(kPromptClass, PromptProc);

    int pad = Scale(10);
    int labelWidth = Scale(70);
    int clientWidth = Scale(width);
    auto fieldHeight = [](const PromptField& field) { return field.multiline ? Scale(180) : Scale(22); };
    int y = pad;
    std::vector<int> tops;
    for (const PromptField& field : fields) {
        tops.push_back(y);
        y += fieldHeight(field) + Scale(6);
    }
    int clientHeight = y + Scale(32) + pad;

    RECT frame = {0, 0, clientWidth, clientHeight};
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    AdjustWindowRectEx(&frame, style, FALSE, WS_EX_DLGMODALFRAME);
    HWND root = GetAncestor(owner, GA_ROOT);
    RECT ownerRect;
    GetWindowRect(root, &ownerRect);
    int frameWidth = frame.right - frame.left;
    int frameHeight = frame.bottom - frame.top;
    int x = ownerRect.left + (ownerRect.right - ownerRect.left - frameWidth) / 2;
    int top = ownerRect.top + (ownerRect.bottom - ownerRect.top - frameHeight) / 2;

    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, kPromptClass, title.c_str(), style, x, top, frameWidth,
                                  frameHeight, root, nullptr, GetModuleHandleW(nullptr), nullptr);
    PromptState state;
    state.fields = &fields;
    SetWindowLongPtrW(dialog, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

    for (size_t i = 0; i < fields.size(); i++) {
        const PromptField& field = fields[i];
        CreateChild(dialog, L"STATIC", field.label.c_str(), SS_LEFT, 0, {pad, tops[i] + Scale(3), pad + labelWidth, tops[i] + Scale(20)});
        DWORD editStyle = WS_TABSTOP | (field.multiline ? ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL : ES_AUTOHSCROLL);
        HWND edit = CreateChild(dialog, L"EDIT", field.value.c_str(), editStyle, 100 + static_cast<int>(i),
                                {pad + labelWidth, tops[i], clientWidth - pad, tops[i] + fieldHeight(field)}, WS_EX_CLIENTEDGE);
        state.edits.push_back(edit);
    }
    int buttonWidth = Scale(80);
    int buttonTop = clientHeight - pad - Scale(25);
    CreateChild(dialog, L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK,
             {clientWidth - pad - 2 * buttonWidth - Scale(6), buttonTop, clientWidth - pad - buttonWidth - Scale(6), buttonTop + Scale(25)});
    CreateChild(dialog, L"BUTTON", L"Cancel", WS_TABSTOP, IDCANCEL,
             {clientWidth - pad - buttonWidth, buttonTop, clientWidth - pad, buttonTop + Scale(25)});

    EnableWindow(root, FALSE);
    ShowWindow(dialog, SW_SHOW);
    if (!state.edits.empty()) {
        SetFocus(state.edits[0]);
        SendMessageW(state.edits[0], EM_SETSEL, 0, -1);
    }
    MSG msg = {};
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (msg.message == WM_QUIT) {
        // keep the quit for the main loop
        PostQuitMessage(static_cast<int>(msg.wParam));
    }
    EnableWindow(root, TRUE);
    SetWindowLongPtrW(dialog, GWLP_USERDATA, 0);
    DestroyWindow(dialog);
    SetActiveWindow(root);
    return state.accepted;
}

namespace {

// the common settings of the file dialogs, `buffer` takes the file name (and returns the chosen ones)
OPENFILENAMEW FileDialog(HWND owner, const wchar_t* title, const wchar_t* filter, wchar_t* buffer, DWORD size, DWORD flags) {
    OPENFILENAMEW dialog = {sizeof(dialog)};
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = size;
    dialog.lpstrTitle = title;
    dialog.Flags = OFN_EXPLORER | flags;
    return dialog;
}

}  // namespace

std::wstring OpenFileDialog(HWND owner, const wchar_t* title, const wchar_t* filter) {
    std::vector<std::wstring> files = OpenFilesDialog(owner, title, filter);
    return files.size() == 1 ? files[0] : L"";
}

std::vector<std::wstring> OpenFilesDialog(HWND owner, const wchar_t* title, const wchar_t* filter) {
    std::vector<wchar_t> buffer(32768, L'\0');
    OPENFILENAMEW dialog = FileDialog(owner, title, filter, buffer.data(), static_cast<DWORD>(buffer.size()), OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT);
    if (!GetOpenFileNameW(&dialog)) {
        return {};
    }
    // one file is a full path, several are the directory followed by the names
    std::vector<std::wstring> parts;
    for (const wchar_t* part = buffer.data(); *part; part += wcslen(part) + 1) {
        parts.push_back(part);
    }
    if (parts.size() <= 1) {
        return parts;
    }
    std::vector<std::wstring> files;
    for (size_t i = 1; i < parts.size(); i++) {
        files.push_back(parts[0] + L"\\" + parts[i]);
    }
    return files;
}

std::wstring SaveFileDialog(HWND owner, const wchar_t* title, const wchar_t* filter, const wchar_t* extension, const std::wstring& name) {
    wchar_t buffer[MAX_PATH] = L"";
    // characters Windows does not allow in file names
    std::wstring clean = name;
    for (auto& c : clean) {
        if (wcschr(L"\\/:*?\"<>|", c) || c < 32) {
            c = L'_';
        }
    }
    wcsncpy_s(buffer, clean.c_str(), _TRUNCATE);
    OPENFILENAMEW dialog = FileDialog(owner, title, filter, buffer, MAX_PATH, OFN_OVERWRITEPROMPT);
    dialog.lpstrDefExt = extension;
    return GetSaveFileNameW(&dialog) ? buffer : L"";
}

std::wstring FolderDialog(HWND owner, const wchar_t* title) {
    BROWSEINFOW browse = {};
    browse.hwndOwner = owner;
    browse.lpszTitle = title;
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE list = SHBrowseForFolderW(&browse);
    if (!list) {
        return {};
    }
    wchar_t path[MAX_PATH] = L"";
    SHGetPathFromIDListW(list, path);
    CoTaskMemFree(list);
    return path;
}

void CopyToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) {
        return;
    }
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        memcpy(GlobalLock(memory), text.c_str(), bytes);
        GlobalUnlock(memory);
        if (!SetClipboardData(CF_UNICODETEXT, memory)) {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

bool WriteTextFile(const std::wstring& file, const std::wstring& text) {
    std::ofstream stream(file, std::ios::binary);
    std::string utf8 = Narrow(text);
    stream.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    return stream.good();
}
