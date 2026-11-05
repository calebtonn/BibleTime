// Page layout and drawing: a port of Sources/BibleTime/PageRenderer.swift and the Metrics/Palette parts of Theme.swift.
//
// Coordinates are DIPs (1/96 inch, the same unit as macOS points) with a TOP-LEFT origin. The layout math is kept in
// the macOS bottom-left convention (so it can be compared with the Swift source line by line) and converted once at
// the end of layout().
#pragma once

#include <d2d1.h>
#include <dwrite_1.h>

#include <optional>

#include "Core.h"
#include "Typeface.h"
#include "Win32.h"

namespace bt::app {

struct RectD {
    double x = 0, y = 0, width = 0, height = 0;
    double minX() const { return x; }
    double maxX() const { return x + width; }
    double minY() const { return y; }
    double maxY() const { return y + height; }
    double midX() const { return x + width / 2; }
    double midY() const { return y + height / 2; }
    RectD insetBy(double dx, double dy) const { return {x + dx, y + dy, width - 2 * dx, height - 2 * dy}; }
    bool operator==(const RectD& o) const { return x == o.x && y == o.y && width == o.width && height == o.height; }
    bool operator!=(const RectD& o) const { return !(*this == o); }
};

struct PointD {
    double x = 0, y = 0;
    bool operator==(const PointD& o) const { return x == o.x && y == o.y; }
    bool operator!=(const PointD& o) const { return !(*this == o); }
};

/// Colors for one theme (sRGB), from Theme.swift.
struct Palette {
    D2D1_COLOR_F background, ink, secondary, accent, hairline;
    static Palette paper();
    static Palette night();
    /// Auto: Night when apps use dark mode or the hour is before 7 or from 20 on.
    static Palette resolve(ThemeChoice theme, bool darkMode, int hour);
};

/// Geometry derived from the view size, in DIPs. Mirrors Metrics in Theme.swift exactly.
struct Metrics {
    RectD bounds;
    bool isPreview = false;
    double textScale = 1;

    double minDim() const;
    double margin() const;
    RectD safe() const;
    double bodySize() const;
    double minBodySize() const;
    double referenceSize() const;
    double clockSize() const;
    /// About 66 characters per line (0.425 em average advance).
    double measureWidth(double fontSize) const;
    bool operator==(const Metrics& o) const {
        return bounds == o.bounds && isPreview == o.isPreview && textScale == o.textScale;
    }
    bool operator!=(const Metrics& o) const { return !(*this == o); }
};

enum class TextAlign { Left, Center };

/// A fully laid-out minute: the fitted font size, every rectangle (top-left DIPs) and the prepared text layouts,
/// so drawing a frame never lays out text.
struct Page {
    DisplayContent content;
    LayoutStyle layout = LayoutStyle::Clock;
    TypefaceChoice typeface = TypefaceChoice::NewYork;
    bool showClock = true;
    Metrics metrics;
    double bodyFontSize = 0;
    double bodyWidth = 0;
    RectD bodyRect;
    /// Top-left of the reference line box.
    PointD referenceOrigin;
    std::optional<PointD> clockOrigin;
    /// The hairline in the macOS (bottom-left) convention: its minY is snapped to device pixels when drawn.
    std::optional<RectD> ruleUp;
    TextAlign alignment = TextAlign::Left;

    ComPtr<IDWriteTextLayout> bodyLayout, referenceLayout, clockLayout;
};

namespace PageRenderer {

constexpr double lineHeightMultiple = 1.3;

enum Part : unsigned { Body = 1, Reference = 2, Clock = 4, Rule = 8, All = 15 };

Page layout(const DisplayContent& content, const Preferences& preferences, const Metrics& metrics);

/// Parts that are identical on both pages and therefore need no crossfade.
unsigned unchangedParts(const Page& a, const Page& b);

/// Draw the requested parts with the given opacity, offset by `drift` (DIPs). `scale` = device pixels per DIP,
/// used to snap the hairline. `brush` is any solid color brush created on `target` (its color is overwritten).
void draw(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, const Page& page, const Palette& palette,
          float alpha, PointD drift, double scale, unsigned parts = All);

/// Largest size in [minSize, maxSize] whose wrapped text fits `box` (0.5 pt binary search, 7 pt floor).
double fittingFontSize(const std::wstring& text, const FontFace& face, double boxWidth, double boxHeight,
                       double minSize, double maxSize, const Metrics& metrics);

}  // namespace PageRenderer

}  // namespace bt::app
