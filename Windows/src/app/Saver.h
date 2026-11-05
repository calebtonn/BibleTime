// The screen saver itself: shared state, the per-display view (page + crossfade + drift), and the Win32 modes
// /s (full screen on every monitor) and /p (Screen Saver Settings preview). Port of Sources/BibleTime/BibleTimeView.swift.
#pragma once

#include <d2d1.h>

#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "Core.h"
#include "PageRenderer.h"
#include "Platform.h"
#include "Win32.h"

namespace bt::app {

/// Process-wide state shared by every display: the verse data (loaded once), settings, locale and Direct2D factory.
class SaverContext {
public:
    static SaverContext& shared();

    ID2D1Factory* d2d() const { return d2d_.Get(); }
    const Preferences& preferences() const { return preferences_; }
    const LocaleInfo& locale() const { return locale_; }
    bool darkMode() const { return darkMode_; }
    /// Bumped whenever settings, locale or theme change, so views know to lay out again.
    unsigned generation() const { return generation_; }

    /// Re-read settings.json when its timestamp changed (cheap; called on the 1 s tick). Returns true if reloaded.
    bool pollSettings();
    /// Re-read settings, locale and dark mode unconditionally (WM_SETTINGCHANGE).
    void reloadSystemSettings();

    /// What a display shows for `time`. `backgroundOnly` yields an empty page. Never throws: errors become the text.
    DisplayContent content(const ClockTime& time, int variant, bool backgroundOnly);

private:
    SaverContext();
    ComPtr<ID2D1Factory> d2d_;
    std::unique_ptr<VerseClock> clock_;
    std::string loadError_;
    SettingsStore store_;
    Preferences preferences_;
    LocaleInfo locale_;
    bool darkMode_ = false;
    unsigned generation_ = 1;
};

/// One display's state, independent of how it is presented (window or offscreen bitmap).
class SaverView {
public:
    static constexpr double fadeDuration = 1.0;
    static constexpr double driftPeriod = 300;
    static constexpr double driftRadius = 6;

    SaverView(bool isPreview, int displayIndex);

    /// Size in DIPs and device pixels per DIP. Lays out again (without a fade) when it changed.
    void setBounds(double widthDips, double heightDips, double scale);
    /// Which display this view stands for (0 = main; picks the verse variant). Lays out again when it changed.
    void setDisplayIndex(int displayIndex);
    int displayIndex() const { return displayIndex_; }
    /// Advance time: minute changes start a crossfade, settings changes relayout, drift moves every 5 minutes.
    /// Returns true when the view needs to be redrawn.
    bool tick();
    bool isFading() const { return fadeStart_.has_value(); }
    /// Draw a full frame (clears the background).
    void render(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush);
    /// Lay out for the current time; `animated` crossfades from the previous page.
    void refresh(bool animated);

private:
    bool effectivelyPreview() const;
    bool showsOnlyBackground() const;
    void endFade();

    bool isPreview_;
    int displayIndex_;
    double width_ = 0, height_ = 0, scale_ = 1;
    std::optional<Page> current_, previous_;
    Metrics laidOutMetrics_;
    unsigned seenGeneration_ = 0;
    std::optional<double> fadeStart_;
    Palette palette_ = Palette::paper();
    PointD drift_;
    double lastDrift_;
    std::mt19937 random_;
};

/// /s: full screen on every monitor until input. Returns the process exit code.
int runFullScreen();
/// /p <hwnd>: draw inside the Screen Saver Settings preview until the parent goes away.
int runPreview(HWND parent);

}  // namespace bt::app
