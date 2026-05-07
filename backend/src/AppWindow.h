#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include <windows.h>
#include <wrl.h>

#include "WebView2.h"

#include "ModelDatabase.h"
#include "NidekTracer.h"

namespace vtracer {

// Frameless dark Win32 window hosting WebView2. The Vue UI paints its own
// title bar; window control flows back through window.chrome.webview.postMessage.
class AppWindow {
public:
    AppWindow();
    ~AppWindow();

    int run(HINSTANCE inst, int showCmd);

private:
    static LRESULT CALLBACK wndProc_(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void onResize_();
    void initWebView_();
    void onWebViewReady_();
    void loadFrontend_();

    void onMessageFromJs_(const std::wstring& json);
    void handleWindowAction_(const std::string& action);

    void sendToJs_(const std::string& json);
    void postToJs_(std::string json);

    void handleListPorts_();
    void handleStartTrace_(const std::string& msgJson);
    void handleReloadCsv_();
    void handleLookupModel_(const std::string& model);
    void handleSaveResult_(const std::string& json);
    void sendCatalogState_();

    struct ExpectedFrame {
        std::optional<double> aL, bL, aR, bR;
    };
    void runTraceWorker_(std::string portName, std::string side,
                         bool simulate, ExpectedFrame expected);

    static std::string findCatalogDir_();
    static std::string resultsCsvPath_();
    static std::wstring exeDir_();
    static std::string parseStringField(const std::string& json, const std::string& key);
    static std::optional<double> parseNumberField(const std::string& json, const std::string& key);

    HWND hwnd_{nullptr};
    HINSTANCE inst_{nullptr};

    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2>           webview_;
    EventRegistrationToken msgToken_{};

    ModelDatabase db_;
    std::string   loadedCatalogDir_;

    std::mutex              qmu_;
    std::queue<std::string> outboundQueue_;
    static constexpr UINT WM_APP_SEND = WM_APP + 1;

    std::atomic<bool> tracing_{false};

    // Width of the window edge reserved for the resize hit-test, since
    // WM_NCCALCSIZE returns 0 (entire window is client area).
    static constexpr int kResizeBorder = 6;
};

} // namespace vtracer
