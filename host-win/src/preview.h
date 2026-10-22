// The message body pane: a WebView2 that shows HTML documents with scripts off and no network access
#pragma once

#include "app.h"

#include <wrl.h>

#include <WebView2.h>

class Preview {
public:
    // creates the WebView2 in `parent` (asynchronously), `onError` gets a message when that fails
    void Create(HWND parent, std::function<void(const std::wstring&)> onError);
    // remoteImages lets images from the network load, for this document only
    void Show(const std::wstring& html, bool remoteImages = false);
    void Clear();
    void Resize(const RECT& bounds);
    // WebView2 stops drawing while its window is hidden, the page tells it when it is on screen again
    void SetVisible(bool visible);

private:
    void Configure();

    HWND parent_ = nullptr;
    RECT bounds_ = {};
    // a document given before the WebView2 was ready
    std::wstring pending_;
    bool remoteImages_ = false;
    std::function<void(const std::wstring&)> onError_;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> view_;
};
