#include "AppWindow.h"
#include "Util.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <regex>
#include <sstream>
#include <thread>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <dwmapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <windowsx.h>

#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Shcore.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace vtracer {

namespace {

constexpr wchar_t kClassName[]   = L"VTracerWindow";
constexpr wchar_t kWindowTitle[] = L"VTracer";

// Resource ids must match app.rc.in.
constexpr int IDR_INDEX_HTML = 1001;
constexpr int IDI_VTRACER    = 1;

std::string loadEmbeddedIndex() {
    HMODULE mod = ::GetModuleHandleW(nullptr);
    HRSRC   res = ::FindResourceW(mod, MAKEINTRESOURCEW(IDR_INDEX_HTML), RT_RCDATA);
    if (!res) return {};
    HGLOBAL h   = ::LoadResource(mod, res);
    if (!h) return {};
    DWORD   sz  = ::SizeofResource(mod, res);
    void*   p   = ::LockResource(h);
    if (!p || sz == 0) return {};
    return std::string(static_cast<const char*>(p), sz);
}

std::wstring exeDirImpl() {
    wchar_t buf[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s = buf;
    auto pos = s.find_last_of(L"\\/");
    if (pos != std::wstring::npos) s.resize(pos);
    return s;
}

std::wstring webview2UserDataFolder() {
    wchar_t* roaming = nullptr;
    std::wstring base;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &roaming))) {
        base = roaming;
        ::CoTaskMemFree(roaming);
    } else {
        base = exeDirImpl();
    }
    base += L"\\VTracer\\WebView2";
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    return base;
}

std::string isoTimestamp() {
    using namespace std::chrono;
    auto now  = system_clock::now();
    auto secs = system_clock::to_time_t(now);
    std::tm  tm{};
    ::localtime_s(&tm, &secs);
    std::ostringstream s;
    s << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return s.str();
}

std::string csvEscape(const std::string& v) {
    bool needs = v.find_first_of(";\"\n\r") != std::string::npos;
    if (!needs) return v;
    std::string out;
    out.reserve(v.size() + 2);
    out.push_back('"');
    for (char c : v) {
        if (c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string fmtNum(std::optional<double> v) {
    if (!v) return "";
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << std::fixed << std::setprecision(2) << *v;
    return s.str();
}

} // anonymous namespace

AppWindow::AppWindow() = default;
AppWindow::~AppWindow() = default;

std::wstring AppWindow::exeDir_() { return exeDirImpl(); }

std::string AppWindow::findCatalogDir_() {
    // Search order:
    //   1. <exe-dir>\catalogs\          (production layout - multiple brand CSVs)
    //   2. <exe-dir>\..\..\data\        (CLion debug build at obj\debug\VTracer.exe)
    //   3. <exe-dir>\..\..\..\data\     (deeper out-of-source build)
    //   4. <exe-dir>\                   (any *.csv next to exe - legacy single-file)
    std::wstring dir = exeDirImpl();
    auto isDirWithCsv = [&](std::wstring p) -> std::string {
        std::error_code ec;
        if (!std::filesystem::is_directory(p, ec)) return {};
        for (auto& e : std::filesystem::directory_iterator(p, ec)) {
            auto ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
            if (ext == ".csv") return wideToUtf8(p);
        }
        return {};
    };
    if (auto p = isDirWithCsv(dir + L"\\catalogs"); !p.empty())     return p;
    if (auto p = isDirWithCsv(dir + L"\\..\\..\\data"); !p.empty()) return p;
    if (auto p = isDirWithCsv(dir + L"\\..\\..\\..\\data"); !p.empty()) return p;
    if (auto p = isDirWithCsv(dir); !p.empty())                     return p;
    return wideToUtf8(dir + L"\\catalogs"); // for nicer error message
}

std::string AppWindow::resultsCsvPath_() {
    return wideToUtf8(exeDirImpl() + L"\\results.csv");
}

int AppWindow::run(HINSTANCE inst, int showCmd) {
    inst_ = inst;

    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HICON appIcon   = ::LoadIconW(inst, MAKEINTRESOURCEW(IDI_VTRACER));
    HICON appIconSm = static_cast<HICON>(::LoadImageW(inst, MAKEINTRESOURCEW(IDI_VTRACER),
                                                      IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &AppWindow::wndProc_;
    wc.hInstance     = inst;
    wc.hCursor       = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = ::CreateSolidBrush(RGB(13, 17, 23));
    wc.lpszClassName = kClassName;
    wc.hIcon         = appIcon;
    wc.hIconSm       = appIconSm ? appIconSm : appIcon;
    ::RegisterClassExW(&wc);

    hwnd_ = ::CreateWindowExW(0, kClassName, kWindowTitle,
                              WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1320, 860,
                              nullptr, nullptr, inst, this);
    if (!hwnd_) return 1;

    BOOL dark = TRUE;
    ::DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    // Pull a 1px frame into the client area so Win11 keeps the drop shadow.
    MARGINS margins{0, 0, 0, 1};
    ::DwmExtendFrameIntoClientArea(hwnd_, &margins);

    // Force a frame recalc so WM_NCCALCSIZE runs with our overrides before
    // the first paint, otherwise the client rect can be a tiny default size.
    ::SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    ::ShowWindow(hwnd_, showCmd ? showCmd : SW_SHOW);
    ::UpdateWindow(hwnd_);

    initWebView_();

    MSG m;
    while (::GetMessageW(&m, nullptr, 0, 0)) {
        ::TranslateMessage(&m);
        ::DispatchMessageW(&m);
    }
    return static_cast<int>(m.wParam);
}

LRESULT CALLBACK AppWindow::wndProc_(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AppWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<AppWindow*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<AppWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return ::DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_NCCALCSIZE:
            // Returning 0 with wp==TRUE makes the whole window client area;
            // the Vue UI paints its own title bar.
            if (wp == TRUE) return 0;
            break;

        case WM_NCACTIVATE:
            // Suppress the legacy non-client repaint on focus change.
            return TRUE;

        case WM_NCHITTEST: {
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ::ScreenToClient(hwnd, &pt);
            RECT rc;
            ::GetClientRect(hwnd, &rc);
            const int b = kResizeBorder;
            if (!::IsZoomed(hwnd)) {
                if (pt.y < b) {
                    if (pt.x < b)              return HTTOPLEFT;
                    if (pt.x > rc.right - b)   return HTTOPRIGHT;
                    return HTTOP;
                }
                if (pt.y > rc.bottom - b) {
                    if (pt.x < b)              return HTBOTTOMLEFT;
                    if (pt.x > rc.right - b)   return HTBOTTOMRIGHT;
                    return HTBOTTOM;
                }
                if (pt.x < b)                  return HTLEFT;
                if (pt.x > rc.right - b)       return HTRIGHT;
            }
            return HTCLIENT;
        }

        case WM_GETMINMAXINFO: {
            auto mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = 920;
            mmi->ptMinTrackSize.y = 620;
            return 0;
        }

        case WM_SIZE:
            self->onResize_();
            return 0;

        case WM_CLOSE:
            ::DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;

        case WM_APP_SEND: {
            std::string s;
            {
                std::lock_guard<std::mutex> g(self->qmu_);
                if (!self->outboundQueue_.empty()) {
                    s = std::move(self->outboundQueue_.front());
                    self->outboundQueue_.pop();
                }
            }
            if (!s.empty()) self->sendToJs_(s);
            return 0;
        }
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

void AppWindow::onResize_() {
    if (!controller_ || !hwnd_) return;
    RECT rc;
    ::GetClientRect(hwnd_, &rc);
    if (!::IsZoomed(hwnd_)) {
        rc.left   += kResizeBorder;
        rc.top    += kResizeBorder;
        rc.right  -= kResizeBorder;
        rc.bottom -= kResizeBorder;
    }
    controller_->put_Bounds(rc);
}

void AppWindow::initWebView_() {
    std::wstring userData = webview2UserDataFolder();

    HRESULT hr = ::CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userData.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) {
                    ::MessageBoxW(hwnd_,
                        L"WebView2 runtime not available. Install Microsoft Edge WebView2 Runtime, then restart.",
                        L"VTracer", MB_ICONERROR);
                    ::PostQuitMessage(1);
                    return S_OK;
                }
                env->CreateCoreWebView2Controller(
                    hwnd_,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT res2, ICoreWebView2Controller* ctrl) -> HRESULT {
                            if (FAILED(res2) || !ctrl) {
                                ::MessageBoxW(hwnd_, L"Failed to create WebView2 controller.",
                                              L"VTracer", MB_ICONERROR);
                                ::PostQuitMessage(1);
                                return S_OK;
                            }
                            controller_ = ctrl;
                            ctrl->get_CoreWebView2(&webview_);
                            onWebViewReady_();
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    if (FAILED(hr)) {
        ::MessageBoxW(hwnd_,
            L"Could not create WebView2 environment. Make sure the WebView2 Runtime is installed.",
            L"VTracer", MB_ICONERROR);
        ::PostQuitMessage(1);
    }
}

void AppWindow::onWebViewReady_() {
    onResize_();

    if (controller_) controller_->put_IsVisible(TRUE);

    if (auto settings = ComPtr<ICoreWebView2Settings>{}; SUCCEEDED(webview_->get_Settings(&settings))) {
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_AreDevToolsEnabled(TRUE);
    }

    webview_->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                LPWSTR raw = nullptr;
                if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
                    std::wstring s = raw;
                    ::CoTaskMemFree(raw);
                    onMessageFromJs_(s);
                }
                return S_OK;
            }).Get(), &msgToken_);

    handleReloadCsv_();
    loadFrontend_();
}

void AppWindow::loadFrontend_() {
    std::string html = loadEmbeddedIndex();
    if (html.empty()) {
        ::MessageBoxW(hwnd_,
            L"Embedded UI is missing. The frontend was not built into the exe.",
            L"VTracer", MB_ICONERROR);
        return;
    }
    // NavigateToString puts the doc on a data: URL which blocks <script type="module">
    // and localStorage. Write to a temp file and navigate via file:// so the page
    // gets a proper origin.
    std::wstring userData = webview2UserDataFolder();
    std::wstring htmlPath = userData + L"\\app.html";
    {
        std::ofstream out(htmlPath, std::ios::binary | std::ios::trunc);
        out.write(html.data(), static_cast<std::streamsize>(html.size()));
    }
    std::wstring url = L"file:///";
    for (wchar_t c : htmlPath) url.push_back(c == L'\\' ? L'/' : c);
    webview_->Navigate(url.c_str());
}

// ---------------------------------------------------------------------------
// Bridge layer.  Messages are JSON of the form { "type": "...", ... }.
// ---------------------------------------------------------------------------

std::string AppWindow::parseStringField(const std::string& json, const std::string& key) {
    // Pattern:  "key" \s* : \s* " (   [^"\\]   |   \\.   )*  "
    // Built without raw strings because the regex itself contains the )" sequence.
    std::regex re("\"" + key + "\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
    std::smatch m;
    if (std::regex_search(json, m, re) && m.size() >= 2) {
        // Unescape \" and \\ minimally.
        std::string s = m[1].str();
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char n = s[++i];
                switch (n) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    default: out.push_back(n); break;
                }
            } else {
                out.push_back(s[i]);
            }
        }
        return out;
    }
    return {};
}

std::optional<double> AppWindow::parseNumberField(const std::string& json, const std::string& key) {
    std::regex re("\"" + key + R"(")" R"(\s*:\s*(-?\d+(?:\.\d+)?))");
    std::smatch m;
    if (std::regex_search(json, m, re) && m.size() >= 2) {
        return parseNumber(m[1].str());
    }
    return std::nullopt;
}

void AppWindow::onMessageFromJs_(const std::wstring& wjson) {
    std::string json = wideToUtf8(wjson);

    std::string type = parseStringField(json, "type");
    if      (type == "ready")          { sendCatalogState_(); handleListPorts_(); }
    else if (type == "list_ports")     { handleListPorts_(); }
    else if (type == "reload_csv")     { handleReloadCsv_(); }
    else if (type == "lookup_model")   { handleLookupModel_(parseStringField(json, "model")); }
    else if (type == "save_result")    { handleSaveResult_(json); }
    else if (type == "start_trace")    { handleStartTrace_(json); }
    else if (type == "window")         { handleWindowAction_(parseStringField(json, "action")); }
    else if (type == "ping")           {
        JsonWriter w; { auto r = w.obj(); w.key("type"); w.str("pong"); }
        postToJs_(std::move(w).str_());
    }
}

void AppWindow::handleWindowAction_(const std::string& action) {
    if (!hwnd_) return;
    if (action == "drag") {
        ::ReleaseCapture();
        ::SendMessageW(hwnd_, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    } else if (action == "minimize") {
        ::ShowWindow(hwnd_, SW_MINIMIZE);
    } else if (action == "maximize") {
        if (::IsZoomed(hwnd_)) ::ShowWindow(hwnd_, SW_RESTORE);
        else                   ::ShowWindow(hwnd_, SW_MAXIMIZE);
        // Update WebView2 bounds after the maximize/restore.
        onResize_();
    } else if (action == "close") {
        ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
}

void AppWindow::sendToJs_(const std::string& json) {
    if (!webview_) return;
    std::wstring w = utf8ToWide(json);
    webview_->PostWebMessageAsString(w.c_str());
}

void AppWindow::postToJs_(std::string json) {
    {
        std::lock_guard<std::mutex> g(qmu_);
        outboundQueue_.push(std::move(json));
    }
    ::PostMessageW(hwnd_, WM_APP_SEND, 0, 0);
}

void AppWindow::handleListPorts_() {
    auto ports = listSerialPorts();
    JsonWriter w;
    {
        auto root = w.obj();
        w.key("type"); w.str("ports");
        w.key("ports");
        {
            auto a = w.arr();
            for (auto& p : ports) w.str(p);
        }
    }
    postToJs_(std::move(w).str_());
}

void AppWindow::handleStartTrace_(const std::string& json) {
    std::string portName = parseStringField(json, "port");
    std::string side     = parseStringField(json, "side");
    bool simulate = (json.find("\"simulate\":true") != std::string::npos);

    ExpectedFrame exp;
    exp.aL = parseNumberField(json, "aL");
    exp.bL = parseNumberField(json, "bL");
    exp.aR = parseNumberField(json, "aR");
    exp.bR = parseNumberField(json, "bR");

    if (tracing_.exchange(true)) {
        JsonWriter w;
        { auto r = w.obj();
          w.key("type"); w.str("log");
          w.key("level"); w.str("warn");
          w.key("msg"); w.str("Trace already in progress."); }
        postToJs_(std::move(w).str_());
        return;
    }
    if (!simulate && portName.empty()) {
        tracing_ = false;
        JsonWriter w;
        { auto r = w.obj();
          w.key("type"); w.str("trace_finished");
          w.key("side"); w.str(side);
          w.key("ok"); w.boolean(false);
          w.key("error"); w.str("No COM port selected"); }
        postToJs_(std::move(w).str_());
        return;
    }
    if (side.empty()) side = "both";
    std::thread(&AppWindow::runTraceWorker_, this, portName, side, simulate, exp).detach();
}

void AppWindow::runTraceWorker_(std::string portName, std::string side,
                                 bool simulate, ExpectedFrame expected) {
    auto logCb = [this](std::string_view level, std::string_view msg) {
        JsonWriter w;
        { auto r = w.obj();
          w.key("type"); w.str("log");
          w.key("level"); w.str(level);
          w.key("msg"); w.str(msg); }
        postToJs_(std::move(w).str_());
    };

    NidekTracer::TraceResult t;

    if (simulate) {
        // Synthesize a measurement near the looked-up expected dimensions, with
        // small jitter so the user can see PASS/FAIL flip across runs.  If no
        // model was looked up, use mid-range defaults.
        logCb("info", "Simulator: synthesizing trace");
        std::this_thread::sleep_for(std::chrono::milliseconds(450));

        static thread_local std::mt19937 gen{std::random_device{}()};
        std::uniform_real_distribution<double> jitter(-0.6, 0.6);

        double aL = expected.aL.value_or(54.21) + jitter(gen);
        double bL = expected.bL.value_or(41.55) + jitter(gen);
        double aR = expected.aR.value_or(aL) + jitter(gen);
        double bR = expected.bR.value_or(bL) + jitter(gen);

        // Build a non-trivial outline for each lens so the wireframe shows
        // something more interesting than a clean ellipse.  Base ellipse +
        // small harmonics + per-point noise.
        auto synth = [&](double a, double b) {
            std::vector<NidekTracer::TracePoint> pts;
            constexpr int N = 240;
            std::uniform_real_distribution<double> noise(-0.18, 0.18);
            // Random shape variant per call: 0=ellipse, 1=cat-eye-ish, 2=softrect
            int variant = std::uniform_int_distribution<int>(0, 2)(gen);
            double ph = std::uniform_real_distribution<double>(0.0, 2 * M_PI)(gen);
            for (int i = 0; i < N; ++i) {
                double t = i * 2.0 * M_PI / N;
                double rEll = (a * 0.5 * b * 0.5) /
                              std::sqrt(std::pow((b * 0.5) * std::cos(t), 2.0) +
                                        std::pow((a * 0.5) * std::sin(t), 2.0));
                double extra = 0.0;
                if (variant == 1)      extra = 0.6 * std::sin(2 * t + ph);          // cat-eye
                else if (variant == 2) extra = -0.5 * std::cos(4 * t);              // soft rect
                double r = rEll + extra + noise(gen);
                pts.push_back({r * std::cos(t), r * std::sin(t)});
            }
            return pts;
        };

        t.success = true;
        t.aBoxL = aL; t.bBoxL = bL;
        t.aBoxR = aR; t.bBoxR = bR;
        t.dbl = 17.5;
        t.circumferenceL = 165.4;
        t.circumferenceR = 165.3;
        t.pointsL = synth(aL, bL);
        t.pointsR = synth(aR, bR);
        logCb("info", "Simulator: done");
    } else {
        NidekTracer tracer(logCb);
        std::string err = tracer.open(portName);
        if (!err.empty()) {
            JsonWriter w;
            { auto r = w.obj();
              w.key("type"); w.str("trace_finished");
              w.key("side"); w.str(side);
              w.key("ok"); w.boolean(false);
              w.key("error"); w.str(err); }
            postToJs_(std::move(w).str_());
            tracing_ = false;
            return;
        }

        NidekTracer::TraceMode mode = NidekTracer::TraceMode::Both;
        if      (side == "left")   mode = NidekTracer::TraceMode::Left;
        else if (side == "right")  mode = NidekTracer::TraceMode::Right;
        else if (side == "center") mode = NidekTracer::TraceMode::Center;

        t = tracer.runTrace(mode);
        tracer.close();
    }

    auto emitNum = [](JsonWriter& w, const char* key, std::optional<double> v) {
        w.key(key);
        if (v.has_value()) w.num(*v); else w.null_();
    };

    JsonWriter w;
    {
        auto root = w.obj();
        w.key("type"); w.str("trace_finished");
        w.key("side"); w.str(side);
        w.key("ok");   w.boolean(t.success);
        if (!t.success) {
            w.key("error"); w.str(t.error);
        }
        w.key("measurement");
        {
            auto m = w.obj();
            emitNum(w, "aL", t.aBoxL);
            emitNum(w, "bL", t.bBoxL);
            emitNum(w, "aR", t.aBoxR);
            emitNum(w, "bR", t.bBoxR);
            emitNum(w, "dbl", t.dbl);
            emitNum(w, "circumferenceL", t.circumferenceL);
            emitNum(w, "circumferenceR", t.circumferenceR);
            auto emitPoints = [&w](const char* key, const std::vector<NidekTracer::TracePoint>& pts) {
                w.key(key);
                auto arr = w.arr();
                for (auto& p : pts) {
                    auto o = w.obj();
                    w.key("x"); w.num(p.x);
                    w.key("y"); w.num(p.y);
                }
            };
            emitPoints("pointsL", t.pointsL);
            emitPoints("pointsR", t.pointsR);
        }
    }

    postToJs_(std::move(w).str_());
    tracing_ = false;
}

void AppWindow::handleReloadCsv_() {
    std::string dir = findCatalogDir_();
    db_.loadDirectory(dir);
    loadedCatalogDir_ = dir;
    sendCatalogState_();
}

void AppWindow::sendCatalogState_() {
    JsonWriter w;
    {
        auto root = w.obj();
        w.key("type"); w.str("catalog");
        w.key("dir"); w.str(loadedCatalogDir_);
        w.key("error"); w.str(db_.stats().error);
        w.key("stats");
        {
            auto s = w.obj();
            w.key("total");    w.num(static_cast<int64_t>(db_.stats().total));
            w.key("accepted"); w.num(static_cast<int64_t>(db_.stats().accepted));
            w.key("flagged");  w.num(static_cast<int64_t>(db_.stats().flagged));
            w.key("skipped");  w.num(static_cast<int64_t>(db_.stats().skipped));
        }
        w.key("files");
        {
            auto arr = w.arr();
            for (auto& f : db_.stats().files) {
                auto o = w.obj();
                w.key("path");     w.str(f.path);
                w.key("brand");    w.str(f.brand);
                w.key("accepted"); w.num(static_cast<int64_t>(f.accepted));
                w.key("flagged");  w.num(static_cast<int64_t>(f.flagged));
                w.key("skipped");  w.num(static_cast<int64_t>(f.skipped));
                w.key("error");    w.str(f.error);
            }
        }
        w.key("flagged");
        {
            auto a = w.arr();
            for (auto& e : db_.entries()) {
                if (!e.flagged) continue;
                auto o = w.obj();
                w.key("brand"); w.str(e.brand);
                w.key("model"); w.str(e.model);
                w.key("reason"); w.str(e.flagReason);
            }
        }
    }
    postToJs_(std::move(w).str_());
}

void AppWindow::handleLookupModel_(const std::string& model) {
    JsonWriter w;
    {
        auto root = w.obj();
        w.key("type"); w.str("model_lookup");
        w.key("model"); w.str(model);
        const ModelEntry* hit = nullptr;
        for (auto& e : db_.entries()) {
            if (e.model == model) { hit = &e; break; }
        }
        w.key("found"); w.boolean(hit != nullptr);
        if (hit) {
            w.key("brand"); w.str(hit->brand);
            w.key("intro"); w.str(hit->introduction);
            w.key("flagged"); w.boolean(hit->flagged);
            w.key("flagReason"); w.str(hit->flagReason);
            w.key("expected");
            {
                auto e = w.obj();
                w.key("aL"); w.num(hit->aBoxL);
                w.key("bL"); w.num(hit->bBoxL);
                w.key("aR"); w.num(hit->aBoxR);
                w.key("bR"); w.num(hit->bBoxR);
            }
        }
    }
    postToJs_(std::move(w).str_());
}

void AppWindow::handleSaveResult_(const std::string& json) {
    std::string model    = parseStringField(json, "model");
    std::string batch    = parseStringField(json, "batch");
    std::string brand    = parseStringField(json, "brand");
    std::string note     = parseStringField(json, "note");

    auto aL  = parseNumberField(json, "aL");
    auto bL  = parseNumberField(json, "bL");
    auto aR  = parseNumberField(json, "aR");
    auto bR  = parseNumberField(json, "bR");
    auto eaL = parseNumberField(json, "expectedAL");
    auto ebL = parseNumberField(json, "expectedBL");
    auto eaR = parseNumberField(json, "expectedAR");
    auto ebR = parseNumberField(json, "expectedBR");

    double tolerance = parseNumberField(json, "tolerance").value_or(ModelDatabase::TOLERANCE_MM);

    auto pass = [tolerance](std::optional<double> a, std::optional<double> b) {
        if (!a || !b) return std::string{"-"};
        return std::abs(*a - *b) <= tolerance ? std::string{"PASS"} : std::string{"FAIL"};
    };
    std::string passAL = pass(aL, eaL);
    std::string passBL = pass(bL, ebL);
    std::string passAR = pass(aR, eaR);
    std::string passBR = pass(bR, ebR);

    std::string passLens = (passAL == "PASS" && passBL == "PASS") ? "PASS"
                          : ((passAL == "-" || passBL == "-") ? "-" : "FAIL");
    std::string passLensR = (passAR == "PASS" && passBR == "PASS") ? "PASS"
                          : ((passAR == "-" || passBR == "-") ? "-" : "FAIL");

    std::string path = resultsCsvPath_();
    bool needsHeader = false;
    {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) needsHeader = true;
        else if (std::filesystem::file_size(path, ec) == 0) needsHeader = true;
    }

    std::ofstream out(utf8ToWide(path), std::ios::app | std::ios::binary);
    bool ok = out.good();
    if (ok && needsHeader) {
        out << "timestamp;brand;model;batch;tolerance_mm;"
               "measured_aL;measured_bL;measured_aR;measured_bR;"
               "expected_aL;expected_bL;expected_aR;expected_bR;"
               "pass_left;pass_right;pass_aL;pass_bL;pass_aR;pass_bR;note\r\n";
    }
    if (ok) {
        out << csvEscape(isoTimestamp())  << ';'
            << csvEscape(brand)           << ';'
            << csvEscape(model)           << ';'
            << csvEscape(batch)           << ';'
            << fmtNum(tolerance)          << ';'
            << fmtNum(aL)                 << ';'
            << fmtNum(bL)                 << ';'
            << fmtNum(aR)                 << ';'
            << fmtNum(bR)                 << ';'
            << fmtNum(eaL)                << ';'
            << fmtNum(ebL)                << ';'
            << fmtNum(eaR)                << ';'
            << fmtNum(ebR)                << ';'
            << passLens                   << ';'
            << passLensR                  << ';'
            << passAL << ';' << passBL << ';' << passAR << ';' << passBR << ';'
            << csvEscape(note) << "\r\n";
        ok = out.good();
    }

    JsonWriter w;
    {
        auto root = w.obj();
        w.key("type"); w.str("save_result_done");
        w.key("ok"); w.boolean(ok);
        w.key("path"); w.str(path);
        w.key("model"); w.str(model);
        w.key("batch"); w.str(batch);
        w.key("passL"); w.str(passLens);
        w.key("passR"); w.str(passLensR);
    }
    postToJs_(std::move(w).str_());
}

} // namespace vtracer
