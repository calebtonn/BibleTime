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

void SaverView::setDisplayIndex(int displayIndex) {
    if (displayIndex == displayIndex_) return;
    displayIndex_ = displayIndex;
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

    /// `activate` asks for the foreground (full screen only); otherwise the window is shown without activation.
    bool create(HWND parent, const RECT& rect, bool fullScreen, bool activate);
    HWND hwnd() const { return hwnd_; }
    SaverView& view() { return view_; }
    /// The monitor this window covers: its device name (\\.\DISPLAYn, stable across display changes) and rectangle.
    const std::wstring& monitorDevice() const { return monitorDevice_; }
    const RECT& monitorRect() const { return monitorRect_; }
    void setMonitor(const std::wstring& device, const RECT& rect) {
        monitorDevice_ = device;
        monitorRect_ = rect;
    }
    /// Cover `rect` (full screen): reposition without activating, pick up the DPI there, lay out and redraw.
    void moveTo(const RECT& rect);
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
    std::wstring monitorDevice_;
    RECT monitorRect_{};
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
    /// Classic screen saver input rule: any movement beyond a few pixels from the first position ends it. During the
    /// grace period the position only sets the baseline (window creation and display changes move the cursor).
    void mouseMoved(POINT p) {
        if (!firstMouse_ || !pastStartupGrace()) {
            firstMouse_ = p;
            return;
        }
        if (std::abs(p.x - firstMouse_->x) > kMouseThreshold || std::abs(p.y - firstMouse_->y) > kMouseThreshold) quit();
    }
    /// Deactivation during the first half second (after start or a display change) is ignored: window creation and
    /// moves can briefly shuffle activation.
    bool pastStartupGrace() const { return monotonicSeconds() - startedAt_ > kGraceSeconds; }
    /// A display change is under way: restart the grace period and forget the cursor baseline.
    void beginGrace() {
        startedAt_ = monotonicSeconds();
        firstMouse_.reset();
    }

private:
    static constexpr double kGraceSeconds = 0.5;
    static LRESULT CALLBACK controllerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    /// Make the windows match the monitors: on start creates one per monitor; after a display change keeps existing
    /// windows (moving them to the new rectangles), creates windows only for added monitors and destroys only surplus.
    void syncWindows(bool initial);
    bool ownsForeground() const;
    /// True when a saver window is (or could be made) the foreground window.
    bool ensureForeground();
    void tick();
    void schedule(bool fading);

    HWND previewParent_;
    HWND controller_ = nullptr;
    std::vector<std::unique_ptr<SaverWindow>> windows_;
    std::optional<POINT> firstMouse_;
    bool quitting_ = false;
    double startedAt_ = monotonicSeconds();
    /// After a display change: when to confirm that a saver window still has the foreground.
    std::optional<double> verifyForegroundAt_;
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

bool SaverWindow::create(HWND parent, const RECT& rect, bool fullScreen, bool activate) {
    fullScreen_ = fullScreen;
    monitorRect_ = rect;
    DWORD style = fullScreen ? WS_POPUP : (WS_CHILD | WS_CLIPSIBLINGS);
    DWORD exStyle = fullScreen ? (WS_EX_TOPMOST | WS_EX_TOOLWINDOW) : 0;
    hwnd_ = CreateWindowExW(exStyle, kSaverClass, L"BibleTime", style, rect.left, rect.top, rect.right - rect.left,
                            rect.bottom - rect.top, parent, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;
    updateGeometry();
    paint();  // draw before showing so the first visible frame is the page, not the desktop
    const bool takeForeground = fullScreen && activate;
    ShowWindow(hwnd_, takeForeground ? SW_SHOW : SW_SHOWNA);
    if (takeForeground) SetForegroundWindow(hwnd_);
    return true;
}

void SaverWindow::moveTo(const RECT& rect) {
    monitorRect_ = rect;
    // Re-assert topmost and show (a display change may have hidden or reordered us); never activate here, so the
    // window that has the foreground keeps it. A move across DPIs sends WM_DPICHANGED, which updates geometry too.
    SetWindowPos(hwnd_, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
    updateGeometry();  // same size but new DPI, or a lost render target: still resize and lay out
    paint();
    InvalidateRect(hwnd_, nullptr, FALSE);
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
    syncWindows(true);
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

struct Monitor {
    RECT rect;
    bool primary;
    std::wstring device;
};

bool sameRect(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

/// Every monitor; index 0 is the primary, the others follow left to right, then top to bottom.
std::vector<Monitor> enumerateMonitors() {
    std::vector<Monitor> monitors;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT, LPARAM data) -> BOOL {
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(m, &info)) {
            reinterpret_cast<std::vector<Monitor>*>(data)->push_back(
                {info.rcMonitor, (info.dwFlags & MONITORINFOF_PRIMARY) != 0, info.szDevice});
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&monitors));
    std::stable_sort(monitors.begin(), monitors.end(), [](const Monitor& a, const Monitor& b) {
        if (a.primary != b.primary) return a.primary;
        if (a.rect.left != b.rect.left) return a.rect.left < b.rect.left;
        return a.rect.top < b.rect.top;
    });
    if (monitors.empty()) {
        monitors.push_back({{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)}, true, std::wstring()});
    }
    return monitors;
}

bool SaverApp::ownsForeground() const {
    HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    for (const auto& w : windows_) {
        if (w->hwnd() == foreground) return true;
    }
    return false;
}

bool SaverApp::ensureForeground() {
    if (ownsForeground()) return true;
    if (windows_.empty()) return false;
    SetForegroundWindow(windows_.front()->hwnd());
    return ownsForeground();
}

void SaverApp::syncWindows(bool initial) {
    if (!fullScreen()) {
        if (!initial) return;
        RECT rc{};
        GetClientRect(previewParent_, &rc);
        auto w = std::make_unique<SaverWindow>(*this, 0, true);
        if (w->create(previewParent_, rc, false, false)) windows_.push_back(std::move(w));
        return;
    }
    if (!initial) beginGrace();
    const std::vector<Monitor> monitors = enumerateMonitors();

    // Match existing windows to monitors without touching any window yet (so messages sent while windows move can
    // still walk windows_): same device first, then the same rectangle, then any window left over.
    std::vector<int> source(monitors.size(), -1);
    std::vector<bool> used(windows_.size(), false);
    auto match = [&](auto&& same) {
        for (size_t i = 0; i < monitors.size(); ++i) {
            if (source[i] >= 0) continue;
            for (size_t j = 0; j < windows_.size(); ++j) {
                if (used[j] || !same(*windows_[j], monitors[i])) continue;
                source[i] = static_cast<int>(j);
                used[j] = true;
                break;
            }
        }
    };
    match([](const SaverWindow& w, const Monitor& m) { return !m.device.empty() && w.monitorDevice() == m.device; });
    match([](const SaverWindow& w, const Monitor& m) { return sameRect(w.monitorRect(), m.rect); });
    match([](const SaverWindow&, const Monitor&) { return true; });

    // Kept windows move to their (possibly new) monitor and take its display index (variant, main-only darkening).
    for (size_t i = 0; i < monitors.size(); ++i) {
        if (source[i] < 0) continue;
        SaverWindow& w = *windows_[static_cast<size_t>(source[i])];
        w.setMonitor(monitors[i].device, monitors[i].rect);
        w.view().setDisplayIndex(static_cast<int>(i));
        w.moveTo(monitors[i].rect);
    }
    // New windows only for monitors that were added. They ask for the foreground only when no saver window has it.
    std::vector<std::unique_ptr<SaverWindow>> created(monitors.size());
    for (size_t i = 0; i < monitors.size(); ++i) {
        if (source[i] >= 0) continue;
        auto w = std::make_unique<SaverWindow>(*this, static_cast<int>(i), false);
        w->setMonitor(monitors[i].device, monitors[i].rect);
        if (w->create(nullptr, monitors[i].rect, true, initial || !ownsForeground())) created[i] = std::move(w);
    }

    // Surplus windows (their monitor went away) go last. If one of them is the foreground window, hand the
    // foreground to a window that stays first, while this process still may.
    std::vector<std::unique_ptr<SaverWindow>> next, surplus;
    for (size_t i = 0; i < monitors.size(); ++i) {
        if (source[i] >= 0) next.push_back(std::move(windows_[static_cast<size_t>(source[i])]));
        else if (created[i]) next.push_back(std::move(created[i]));
    }
    for (size_t j = 0; j < windows_.size(); ++j) {
        if (!used[j]) surplus.push_back(std::move(windows_[j]));
    }
    HWND foreground = GetForegroundWindow();
    bool surplusHasForeground = false;
    for (const auto& w : surplus) surplusHasForeground = surplusHasForeground || w->hwnd() == foreground;
    windows_ = std::move(next);
    if (surplusHasForeground && !windows_.empty()) SetForegroundWindow(windows_.front()->hwnd());
    surplus.clear();

    if (!initial) {
        // A cover that is not the foreground window gets no keyboard input and no deactivation: if no saver window
        // can be made active now, give the system a moment (display changes shuffle activation), then end.
        ensureForeground();
        beginGrace();
        verifyForegroundAt_ = monotonicSeconds() + kGraceSeconds;
    }
}

void SaverApp::tick() {
    if (quitting_) return;
    if (!fullScreen() && !IsWindow(previewParent_)) {
        quit();
        return;
    }
    if (verifyForegroundAt_ && monotonicSeconds() >= *verifyForegroundAt_) {
        verifyForegroundAt_.reset();
        if (!ensureForeground()) {
            quit();
            return;
        }
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
            self->syncWindows(false);
            self->tick();
        }
        return 0;
    case kMsgRebuild:
        // Display changes arrive in bursts; reconcile once things settle. Until then the cursor may jump and activation
        // shuffle as the desktop is rearranged: neither must end the saver.
        self->beginGrace();
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
