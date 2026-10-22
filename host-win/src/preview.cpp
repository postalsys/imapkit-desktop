// WebView2 for message bodies. The documents come from the sidecar with a Content-Security-Policy that allows only
// embedded images; on top of that scripts are off, every http(s) request is refused here and links open in the
// default browser instead of in the pane
#include "preview.h"

#include <shellapi.h>
#include <shlobj.h>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

// NavigateToString takes at most 2 MB
constexpr size_t kMaxDocument = 1900000;

bool StartsWith(const wchar_t* text, const wchar_t* prefix) {
    return text && _wcsnicmp(text, prefix, wcslen(prefix)) == 0;
}

void OpenExternally(const wchar_t* uri) {
    if (StartsWith(uri, L"http://") || StartsWith(uri, L"https://") || StartsWith(uri, L"mailto:")) {
        ShellExecuteW(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL);
    }
}

std::wstring UserDataFolder() {
    PWSTR local = nullptr;
    std::wstring folder;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        folder = std::wstring(local) + L"\\ImapKitDesktop\\WebView2";
    }
    CoTaskMemFree(local);
    return folder;
}

}  // namespace

void Preview::Create(HWND parent, std::function<void(const std::wstring&)> onError) {
    parent_ = parent;
    onError_ = std::move(onError);
    std::wstring folder = UserDataFolder();
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, folder.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([this](HRESULT result, ICoreWebView2Environment* environment) {
            if (FAILED(result) || !environment) {
                onError_(L"The WebView2 runtime is not available, message bodies can not be shown.");
                return S_OK;
            }
            environment_ = environment;
            environment->CreateCoreWebView2Controller(
                parent_, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                             [this](HRESULT created, ICoreWebView2Controller* controller) {
                                 if (FAILED(created) || !controller) {
                                     onError_(L"Creating the WebView2 failed.");
                                     return S_OK;
                                 }
                                 controller_ = controller;
                                 controller_->get_CoreWebView2(&view_);
                                 Configure();
                                 return S_OK;
                             })
                             .Get());
            return S_OK;
        }).Get());
    if (FAILED(hr)) {
        onError_(L"The WebView2 runtime is not available, message bodies can not be shown.");
    }
}

void Preview::Configure() {
    ComPtr<ICoreWebView2Settings> settings;
    view_->get_Settings(&settings);
    settings->put_IsScriptEnabled(FALSE);
    settings->put_AreDefaultScriptDialogsEnabled(FALSE);
    settings->put_IsWebMessageEnabled(FALSE);
    settings->put_AreDevToolsEnabled(FALSE);
    settings->put_IsStatusBarEnabled(FALSE);
    settings->put_IsZoomControlEnabled(TRUE);

    // nothing is loaded from the network, also not what a CSP would let through
    view_->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    view_->add_WebResourceRequested(
        Callback<ICoreWebView2WebResourceRequestedEventHandler>([this](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) {
            ComPtr<ICoreWebView2WebResourceRequest> request;
            args->get_Request(&request);
            LPWSTR uri = nullptr;
            request->get_Uri(&uri);
            if (StartsWith(uri, L"http://") || StartsWith(uri, L"https://") || StartsWith(uri, L"ftp://") || StartsWith(uri, L"file:")) {
                ComPtr<ICoreWebView2WebResourceResponse> response;
                environment_->CreateWebResourceResponse(nullptr, 403, L"Blocked", L"", &response);
                args->put_Response(response.Get());
            }
            CoTaskMemFree(uri);
            return S_OK;
        }).Get(),
        nullptr);

    // links open in the browser, the pane only ever shows the documents it is given
    view_->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>([](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) {
            LPWSTR uri = nullptr;
            args->get_Uri(&uri);
            if (!StartsWith(uri, L"data:") && !StartsWith(uri, L"about:")) {
                args->put_Cancel(TRUE);
                BOOL user = FALSE;
                args->get_IsUserInitiated(&user);
                if (user) {
                    OpenExternally(uri);
                }
            }
            CoTaskMemFree(uri);
            return S_OK;
        }).Get(),
        nullptr);
    view_->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>([](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) {
            LPWSTR uri = nullptr;
            args->get_Uri(&uri);
            args->put_Handled(TRUE);
            OpenExternally(uri);
            CoTaskMemFree(uri);
            return S_OK;
        }).Get(),
        nullptr);

    controller_->put_Bounds(bounds_);
    controller_->put_IsVisible(IsWindowVisible(parent_) ? TRUE : FALSE);
    if (pending_.empty()) {
        Clear();
    } else {
        Show(pending_);
    }
}

void Preview::Show(const std::wstring& html) {
    if (!view_) {
        pending_ = html;
        return;
    }
    pending_.clear();
    if (html.size() > kMaxDocument) {
        view_->NavigateToString(
            L"<!doctype html><html><body style=\"font:10pt Arial\"><p>This message is too large to preview. Use <b>Source</b> to see "
            L"it.</p></body></html>");
        return;
    }
    view_->NavigateToString(html.c_str());
}

void Preview::Clear() {
    Show(L"<!doctype html><html><body></body></html>");
}

void Preview::SetVisible(bool visible) {
    if (controller_) {
        controller_->put_IsVisible(visible ? TRUE : FALSE);
    }
}

void Preview::Resize(const RECT& bounds) {
    bounds_ = bounds;
    if (controller_) {
        controller_->put_Bounds(bounds);
    }
}
