#include "Saver.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>

#include "Resources.h"

namespace bt::app {

// MARK: - SaverContext

SaverContext& SaverContext::shared() {
    static SaverContext instance;
    return instance;
}

SaverContext::SaverContext() {
    D2D1_FACTORY_OPTIONS options{};
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), &options,
                      reinterpret_cast<void**>(d2d_.GetAddressOf()));
    try {
        std::vector<std::filesystem::path> imports;
        if (auto dir = importDirectory(); !dir.empty()) imports.push_back(dir);
        clock_ = std::make_unique<VerseClock>(embeddedResourceLoader(), std::move(imports));
    } catch (const std::exception& e) {
        loadError_ = e.what();
    }
    reloadSystemSettings();
}

bool SaverContext::pollSettings() {
    bool changed = false;
    // Child windows (the preview) never see WM_SETTINGCHANGE, so the dark-mode switch is polled too (one registry read).
    if (bool dark = appsUseDarkTheme(); dark != darkMode_) {
        darkMode_ = dark;
        changed = true;
    }
    if (store_.changedSinceLastCheck()) {
        Preferences p = store_.load();
        if (p != preferences_) {
            preferences_ = p;
            changed = true;
        }
    }
    if (changed) ++generation_;
    return changed;
}

void SaverContext::reloadSystemSettings() {
    store_.changedSinceLastCheck();
    preferences_ = store_.load();
    locale_ = userLocale();
    darkMode_ = appsUseDarkTheme();
    ++generation_;
}

DisplayContent SaverContext::content(const ClockTime& time, int variant, bool backgroundOnly) {
    DisplayContent empty;
    empty.minuteKey = time.minuteKey();
    empty.kind = SelectionKind::Fallback;
    if (backgroundOnly) return empty;
    if (!clock_) {
        empty.text = loadError_.empty() ? "BibleTime could not load its verses." : loadError_;
        return empty;
    }
    try {
        static const std::optional<VerseRef> pinned = dev::pinnedReference();
        if (pinned) {
            if (auto c = clock_->content(*pinned, time, preferences_, locale_)) return *c;
        }
        return clock_->content(time, preferences_, locale_, variant);
    } catch (const std::exception& e) {
        empty.text = e.what();
        return empty;
    }
}

// MARK: - SaverView

SaverView::SaverView(bool isPreview, int displayIndex)
    : isPreview_(isPreview), displayIndex_(displayIndex), lastDrift_(-driftPeriod * 2), random_(std::random_device{}()) {}

bool SaverView::effectivelyPreview() const {
    // System Settings on macOS (and the tiny Windows preview) are small: treat small views as previews.
    return isPreview_ || height_ < 300 || width_ < 400;
}

namespace {
/// Display index honouring BIBLETIME_DISPLAY (read once).
int effectiveDisplayIndex(int index, bool preview) {
    static const std::optional<int> pinned = dev::pinnedDisplayIndex();
    return pinned.value_or(preview ? 0 : index);
}
}  // namespace

bool SaverView::showsOnlyBackground() const {
    int index = effectiveDisplayIndex(displayIndex_, effectivelyPreview());
    return SaverContext::shared().preferences().displayMode == DisplayMode::MainOnly && index > 0 && !effectivelyPreview();
}

void SaverView::setBounds(double widthDips, double heightDips, double scale) {
    if (widthDips == width_ && heightDips == height_ && scale == scale_ && current_) return;
    width_ = widthDips;
    height_ = heightDips;
    scale_ = scale;
    refresh(false);
}

void SaverView::endFade() {
    fadeStart_.reset();
    previous_.reset();
}

void SaverView::refresh(bool animated) {
    if (width_ <= 0 || height_ <= 0) return;
    SaverContext& ctx = SaverContext::shared();
    const Preferences& prefs = ctx.preferences();
    ClockTime time = now();
    palette_ = Palette::resolve(prefs.theme, ctx.darkMode(), time.hour);

    int index = effectiveDisplayIndex(displayIndex_, effectivelyPreview());
    int variant = prefs.displayMode == DisplayMode::DifferentVerses ? index : 0;
    DisplayContent content = ctx.content(time, variant, showsOnlyBackground());

    Metrics metrics;
    metrics.bounds = {0, 0, width_, height_};
    metrics.isPreview = effectivelyPreview();
    metrics.textScale = prefs.textScale;
    Page page = PageRenderer::layout(content, prefs, metrics);
    seenGeneration_ = ctx.generation();

    if (animated && current_ && current_->content != page.content) {
        previous_ = std::move(current_);
        fadeStart_ = monotonicSeconds();
    } else {
        endFade();
    }
    current_ = std::move(page);
}

bool SaverView::tick() {
    bool needsDisplay = false;
    const double seconds = monotonicSeconds();
    if (fadeStart_) {
        if (seconds - *fadeStart_ >= fadeDuration) endFade();
        needsDisplay = true;
    }
    if (seenGeneration_ != SaverContext::shared().generation()) {
        refresh(false);
        needsDisplay = true;
    }
    int minuteKey = now().minuteKey();
    if (!current_ || current_->content.minuteKey != minuteKey) {
        refresh(current_.has_value());
        needsDisplay = true;
    }
    if (!effectivelyPreview() && seconds - lastDrift_ > driftPeriod) {
        // A few DIPs of drift every five minutes against burn-in; whole DIPs so text keeps its pixel snapping.
        lastDrift_ = seconds;
        std::uniform_real_distribution<double> d(-driftRadius, driftRadius);
        drift_ = {std::round(d(random_)), std::round(d(random_))};
        needsDisplay = true;
    }
    return needsDisplay;
}

void SaverView::render(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush) {
    // A secondary display in Main-display-only mode stays dark, whatever the theme.
    const bool dark = showsOnlyBackground();
    target->Clear(dark ? D2D1::ColorF(0, 0, 0, 1) : palette_.background);
    if (dark || !current_ || current_->content.text.empty()) return;

    if (!fadeStart_ || !previous_) {
        PageRenderer::draw(target, brush, *current_, palette_, 1, drift_, scale_);
        return;
    }
    double t = std::clamp((monotonicSeconds() - *fadeStart_) / fadeDuration, 0.0, 1.0);
    float alpha = static_cast<float>(t * t * (3 - 2 * t));  // smoothstep

    // Elements identical on both pages (usually the clock and the rule) are drawn once at full opacity so they do
    // not dim and brighten every minute; only what changed crossfades.
    unsigned shared = PageRenderer::unchangedParts(*previous_, *current_);
    unsigned changed = PageRenderer::All & ~shared;
    PageRenderer::draw(target, brush, *previous_, palette_, 1 - alpha, drift_, scale_, changed);
    PageRenderer::draw(target, brush, *current_, palette_, alpha, drift_, scale_, changed);
    PageRenderer::draw(target, brush, *current_, palette_, 1, drift_, scale_, shared);
}

// MARK: - Windows

namespace {

constexpr wchar_t kSaverClass[] = L"BibleTimeSaverWindow";
constexpr wchar_t kControllerClass[] = L"BibleTimeSaverController";
constexpr UINT_PTR kTickTimer = 1;
constexpr UINT_PTR kRebuildTimer = 2;
constexpr UINT kMsgRebuild = WM_APP + 1;
/// Mouse travel (device pixels) that counts as the user coming back.
constexpr int kMouseThreshold = 4;

class SaverApp;

/// A window showing one SaverView through an ID2D1HwndRenderTarget.
class SaverWindow {
public:
    SaverWindow(SaverApp& app, int displayIndex, bool preview) : app_(app), view_(preview, displayIndex) {}
    ~SaverWindow() {
        if (hwnd_) {
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            DestroyWindow(hwnd_);
        }
    }

    bool create(HWND parent, const RECT& rect, bool fullScreen);
    HWND hwnd() const { return hwnd_; }
    SaverView& view() { return view_; }
    void updateGeometry();
    void paint();

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    LRESULT handle(UINT msg, WPARAM wParam, LPARAM lParam);
    bool ensureTarget();

    SaverApp& app_;
    SaverView view_;
    HWND hwnd_ = nullptr;
    bool fullScreen_ = false;
    ComPtr<ID2D1HwndRenderTarget> target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
};

/// Owns the windows and the single timer that drives them (so every display fades in step with the compositor).
class SaverApp {
public:
    explicit SaverApp(HWND previewParent) : previewParent_(previewParent) {}
    ~SaverApp() {
        windows_.clear();
        if (controller_) DestroyWindow(controller_);
    }

    bool start();
    int run();
    bool fullScreen() const { return previewParent_ == nullptr; }
    void quit() {
        if (!quitting_) {
            quitting_ = true;
            PostQuitMessage(0);
        }
    }
    void scheduleRebuild() { PostMessageW(controller_, kMsgRebuild, 0, 0); }
    void invalidateAll() {
        for (auto& w : windows_) InvalidateRect(w->hwnd(), nullptr, FALSE);
    }
    /// Classic screen saver input rule: any movement beyond a few pixels from the first position ends it.
    void mouseMoved(POINT p) {
        if (!firstMouse_) {
            firstMouse_ = p;
            return;
        }
        if (std::abs(p.x - firstMouse_->x) > kMouseThreshold || std::abs(p.y - firstMouse_->y) > kMouseThreshold) quit();
    }
    /// Deactivation during the first half second is ignored (window creation can briefly shuffle activation).
    bool pastStartupGrace() const { return monotonicSeconds() - startedAt_ > 0.5; }

private:
    static LRESULT CALLBACK controllerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void buildWindows();
    void tick();
    void schedule(bool fading);

    HWND previewParent_;
    HWND controller_ = nullptr;
    std::vector<std::unique_ptr<SaverWindow>> windows_;
    std::optional<POINT> firstMouse_;
    bool quitting_ = false;
    double startedAt_ = monotonicSeconds();
};

void registerClasses() {
    static bool done = false;
    if (done) return;
    done = true;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = SaverWindow::windowProc;
    wc.hInstance = instance;
    wc.hCursor = nullptr;  // hidden in full screen; the preview inherits the parent's
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kSaverClass;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(100));
    RegisterClassExW(&wc);
}

bool SaverWindow::create(HWND parent, const RECT& rect, bool fullScreen) {
    fullScreen_ = fullScreen;
    DWORD style = fullScreen ? WS_POPUP : (WS_CHILD | WS_CLIPSIBLINGS);
    DWORD exStyle = fullScreen ? (WS_EX_TOPMOST | WS_EX_TOOLWINDOW) : 0;
    hwnd_ = CreateWindowExW(exStyle, kSaverClass, L"BibleTime", style, rect.left, rect.top, rect.right - rect.left,
                            rect.bottom - rect.top, parent, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;
    updateGeometry();
    paint();  // draw before showing so the first visible frame is the page, not the desktop
    ShowWindow(hwnd_, fullScreen ? SW_SHOW : SW_SHOWNA);
    if (fullScreen) SetForegroundWindow(hwnd_);
    return true;
}

void SaverWindow::updateGeometry() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    UINT dpi = GetDpiForWindow(hwnd_);
    if (dpi == 0) dpi = 96;
    double scale = dpi / 96.0;
    UINT w = static_cast<UINT>(std::max<LONG>(1, rc.right - rc.left));
    UINT h = static_cast<UINT>(std::max<LONG>(1, rc.bottom - rc.top));
    if (target_) {
        target_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
        if (FAILED(target_->Resize(D2D1::SizeU(w, h)))) {
            target_.Reset();
            brush_.Reset();
        }
    }
    view_.setBounds(w / scale, h / scale, scale);
}

bool SaverWindow::ensureTarget() {
    if (target_) return true;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    float dpi = static_cast<float>(std::max<UINT>(96, GetDpiForWindow(hwnd_)));
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                              D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi, dpi);
    // IMMEDIATELY: never block on vsync per window (that would serialize monitors); pacing comes from DwmFlush.
    auto hwndProps = D2D1::HwndRenderTargetProperties(
        hwnd_, D2D1::SizeU(static_cast<UINT>(std::max<LONG>(1, rc.right)), static_cast<UINT>(std::max<LONG>(1, rc.bottom))),
        D2D1_PRESENT_OPTIONS_IMMEDIATELY);
    ID2D1Factory* factory = SaverContext::shared().d2d();
    if (!factory || FAILED(factory->CreateHwndRenderTarget(props, hwndProps, &target_))) return false;
    if (FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &brush_))) {
        target_.Reset();
        return false;
    }
    return true;
}

void SaverWindow::paint() {
    if (!ensureTarget()) return;
    target_->BeginDraw();
    view_.render(target_.Get(), brush_.Get());
    HRESULT hr = target_->EndDraw();
    if (hr == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) {
        // Device lost (driver update, GPU reset, remote session): rebuild on the next frame.
        target_.Reset();
        brush_.Reset();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

LRESULT CALLBACK SaverWindow::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<SaverWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<SaverWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->handle(msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT SaverWindow::handle(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd_, &ps);
        paint();
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
        if (target_ || hwnd_) updateGeometry();
        return 0;
    case WM_DPICHANGED:
        // Full screen windows keep covering their monitor; only the DPI (and so the DIP size) changes.
        if (!fullScreen_) {
            const RECT* r = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        updateGeometry();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_DISPLAYCHANGE:
        if (fullScreen_) app_.scheduleRebuild();
        return 0;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        SaverContext::shared().reloadSystemSettings();
        app_.invalidateAll();
        break;
    case WM_DESTROY:
        // In the preview the parent destroys us when the settings page closes or changes saver.
        if (!fullScreen_) app_.quit();
        return 0;
    default: break;
    }

    if (fullScreen_) {
        switch (msg) {
        case WM_SETCURSOR: SetCursor(nullptr); return TRUE;
        case WM_MOUSEMOVE: {
            POINT p{};
            GetCursorPos(&p);
            app_.mouseMoved(p);
            return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            app_.quit();
            return 0;
        case WM_ACTIVATEAPP:
            if (!wParam && app_.pastStartupGrace()) app_.quit();
            return 0;
        case WM_SYSCOMMAND:
            switch (wParam & 0xFFF0) {
            case SC_SCREENSAVE: return 0;  // already running
            case SC_CLOSE: app_.quit(); return 0;
            default: break;
            }
            break;
        case WM_CLOSE: app_.quit(); return 0;
        default: break;
        }
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

bool SaverApp::start() {
    registerClasses();
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = controllerProc;
    wc.hInstance = instance;
    wc.lpszClassName = kControllerClass;
    RegisterClassExW(&wc);
    controller_ = CreateWindowExW(0, kControllerClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
    if (!controller_) return false;
    SetWindowLongPtrW(controller_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    buildWindows();
    if (windows_.empty()) return false;
    tick();
    return true;
}

int SaverApp::run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

void SaverApp::buildWindows() {
    windows_.clear();
    if (!fullScreen()) {
        RECT rc{};
        GetClientRect(previewParent_, &rc);
        auto w = std::make_unique<SaverWindow>(*this, 0, true);
        if (w->create(previewParent_, rc, false)) windows_.push_back(std::move(w));
        return;
    }
    // Display index 0 is the primary monitor; the others follow left to right, then top to bottom.
    struct Monitor {
        RECT rect;
        bool primary;
    };
    std::vector<Monitor> monitors;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT, LPARAM data) -> BOOL {
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(m, &info)) {
            reinterpret_cast<std::vector<Monitor>*>(data)->push_back({info.rcMonitor, (info.dwFlags & MONITORINFOF_PRIMARY) != 0});
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&monitors));
    std::stable_sort(monitors.begin(), monitors.end(), [](const Monitor& a, const Monitor& b) {
        if (a.primary != b.primary) return a.primary;
        if (a.rect.left != b.rect.left) return a.rect.left < b.rect.left;
        return a.rect.top < b.rect.top;
    });
    if (monitors.empty()) {
        monitors.push_back({{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)}, true});
    }
    for (size_t i = 0; i < monitors.size(); ++i) {
        auto w = std::make_unique<SaverWindow>(*this, static_cast<int>(i), false);
        if (w->create(nullptr, monitors[i].rect, true)) windows_.push_back(std::move(w));
    }
}

void SaverApp::tick() {
    if (quitting_) return;
    if (!fullScreen() && !IsWindow(previewParent_)) {
        quit();
        return;
    }
    SaverContext::shared().pollSettings();
    bool fading = false;
    for (auto& w : windows_) {
        if (w->view().tick()) w->paint();
        fading = fading || w->view().isFading();
    }
    // During a crossfade, wait for the compositor so every frame lands on a vsync (smooth on all monitors at once).
    if (fading) DwmFlush();
    schedule(fading);
}

void SaverApp::schedule(bool fading) {
    UINT interval = USER_TIMER_MINIMUM;
    if (!fading) {
        // Idle: wake once a second, or right after the minute turns if that comes sooner.
        SYSTEMTIME st;
        GetLocalTime(&st);
        UINT toMinute = (59 - st.wSecond) * 1000u + (1000u - st.wMilliseconds) + 15u;
        interval = std::min<UINT>(1000, toMinute);
    }
    SetTimer(controller_, kTickTimer, interval, nullptr);
}

LRESULT CALLBACK SaverApp::controllerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<SaverApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) {
        if (msg == WM_NCCREATE) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    switch (msg) {
    case WM_TIMER:
        if (wParam == kTickTimer) {
            self->tick();
        } else if (wParam == kRebuildTimer) {
            KillTimer(hwnd, kRebuildTimer);
            self->buildWindows();
            self->tick();
        }
        return 0;
    case kMsgRebuild:
        // Display changes arrive in bursts; rebuild once things settle.
        SetTimer(hwnd, kRebuildTimer, 300, nullptr);
        return 0;
    default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace

int runFullScreen() {
    // A second /s (the system re-triggering, or a test) must not stack another set of windows.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\BibleTime.ScreenSaver.Running");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }
    ShowCursor(FALSE);
    int code = 1;
    {
        SaverApp app(nullptr);
        if (app.start()) code = app.run();
    }
    ShowCursor(TRUE);
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return code;
}

int runPreview(HWND parent) {
    if (!IsWindow(parent)) return 1;
    SaverApp app(parent);
    if (!app.start()) return 1;
    return app.run();
}

}  // namespace bt::app
