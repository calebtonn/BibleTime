#include "PageRenderer.h"

#include <algorithm>
#include <cmath>

namespace bt::app {

namespace {

D2D1_COLOR_F hex(uint32_t v) {
    return D2D1::ColorF(static_cast<float>((v >> 16) & 0xff) / 255.0f, static_cast<float>((v >> 8) & 0xff) / 255.0f,
                        static_cast<float>(v & 0xff) / 255.0f, 1.0f);
}

/// Swift's CGFloat.rounded() / round(): half away from zero.
double roundSwift(double v) { return std::round(v); }

constexpr double kBodySlack = 4;  // PageRenderer.bodySlack
constexpr float kUnbounded = 100000.0f;

struct TextBox {
    double width = 0, height = 0;
};

TextBox metricsOf(IDWriteTextLayout* layout) {
    DWRITE_TEXT_METRICS m{};
    if (!layout || FAILED(layout->GetMetrics(&m))) return {};
    return {m.widthIncludingTrailingWhitespace, m.height};
}

void setTracking(IDWriteTextLayout* layout, double tracking, UINT32 length) {
    ComPtr<IDWriteTextLayout1> layout1;
    if (tracking != 0 && SUCCEEDED(layout->QueryInterface(IID_PPV_ARGS(&layout1)))) {
        // NSAttributedString .kern adds space after every character, including the last.
        layout1->SetCharacterSpacing(0, static_cast<float>(tracking), 0, {0, length});
    }
}

/// Body text: line height multiple 1.3 with the extra space above the glyphs (like NSParagraphStyle.lineHeightMultiple),
/// 0.005 em tracking, word wrapping without hyphenation, and an ellipsis on the last visible line when it overflows.
ComPtr<IDWriteTextLayout> bodyLayout(const std::wstring& text, const FontFace& face, double size, double width,
                                     double height, TextAlign alignment) {
    Typography& typo = Typography::shared();
    auto format = typo.format(face, static_cast<float>(size));
    ComPtr<IDWriteTextLayout> layout;
    if (!format || FAILED(typo.factory()->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.Get(),
                                                           static_cast<float>(width), static_cast<float>(height), &layout))) {
        return nullptr;
    }
    const UINT32 length = static_cast<UINT32>(text.size());
    setTracking(layout.Get(), size * 0.005, length);
    LineMetrics lm = typo.lineMetrics(face);
    double natural = lm.lineHeight() * size;
    double spacing = natural * PageRenderer::lineHeightMultiple;
    double baseline = lm.ascent * size + (spacing - natural);
    layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, static_cast<float>(spacing), static_cast<float>(baseline));
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    layout->SetTextAlignment(alignment == TextAlign::Center ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    ComPtr<IDWriteInlineObject> ellipsis;
    if (SUCCEEDED(typo.factory()->CreateEllipsisTrimmingSign(format.Get(), &ellipsis))) {
        DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        layout->SetTrimming(&trimming, ellipsis.Get());
    }
    return layout;
}

/// One line of secondary text (reference or clock) with tracking and tabular figures.
ComPtr<IDWriteTextLayout> lineLayout(const std::wstring& text, const FontFace& face, double size, double trackingEm) {
    Typography& typo = Typography::shared();
    auto format = typo.format(face, static_cast<float>(size));
    ComPtr<IDWriteTextLayout> layout;
    if (!format || FAILED(typo.factory()->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.Get(),
                                                           kUnbounded, kUnbounded, &layout))) {
        return nullptr;
    }
    const UINT32 length = static_cast<UINT32>(text.size());
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    setTracking(layout.Get(), size * trackingEm, length);
    if (face.tabular && typo.tabularTypography()) layout->SetTypography(typo.tabularTypography(), {0, length});
    return layout;
}

/// Measured size of wrapped body text, rounded up like PageRenderer.measure.
TextBox measureBody(const std::wstring& text, const FontFace& face, double size, double width) {
    auto layout = bodyLayout(text, face, size, width, kUnbounded, TextAlign::Left);
    TextBox box = metricsOf(layout.Get());
    return {std::ceil(box.width), std::ceil(box.height)};
}

}  // namespace

// MARK: - Palette

Palette Palette::paper() { return {hex(0xF3EDE6), hex(0x1C1A17), hex(0x6B6258), hex(0x8A6D2F), hex(0xD9D0C4)}; }
Palette Palette::night() { return {hex(0x0E0E10), hex(0xEDE6DA), hex(0x9C948A), hex(0xC9A96A), hex(0x2A2A2E)}; }

Palette Palette::resolve(ThemeChoice theme, bool darkMode, int hour) {
    switch (theme) {
    case ThemeChoice::Paper: return paper();
    case ThemeChoice::Night: return night();
    case ThemeChoice::Auto: break;
    }
    return (darkMode || hour < 7 || hour >= 20) ? night() : paper();
}

// MARK: - Metrics

double Metrics::minDim() const { return std::min(bounds.width, bounds.height); }
double Metrics::margin() const { return roundSwift(minDim() * (isPreview ? 0.06 : 0.08)); }
RectD Metrics::safe() const { return bounds.insetBy(margin(), margin()); }
double Metrics::bodySize() const { return std::max(isPreview ? 10.0 : 18.0, roundSwift(bounds.height * 0.040 * textScale)); }
double Metrics::minBodySize() const { return std::max(isPreview ? 9.0 : 16.0, roundSwift(bounds.height * 0.018)); }
double Metrics::referenceSize() const {
    return std::max(isPreview ? 8.0 : 12.0, roundSwift(bounds.height * 0.020 * std::sqrt(textScale)));
}
double Metrics::clockSize() const {
    return std::max(isPreview ? 8.0 : 12.0, roundSwift(bounds.height * 0.016 * std::sqrt(textScale)));
}
double Metrics::measureWidth(double fontSize) const { return std::min(safe().width, std::ceil(66 * fontSize * 0.425)); }

// MARK: - Layout

double PageRenderer::fittingFontSize(const std::wstring& text, const FontFace& face, double boxWidth, double boxHeight,
                                     double minSize, double maxSize, const Metrics& metrics) {
    auto fits = [&](double size) {
        TextBox s = measureBody(text, face, size, std::min(boxWidth, metrics.measureWidth(size)));
        return s.height <= boxHeight;
    };
    if (fits(maxSize)) return maxSize;
    const double floorSize = 7;
    bool minFits = fits(minSize);
    double lo = minFits ? minSize : floorSize;
    double hi = minFits ? maxSize : minSize;
    if (lo == floorSize && !fits(floorSize)) return floorSize;
    while (hi - lo > 0.5) {
        double mid = (lo + hi) / 2;
        if (fits(mid)) lo = mid;
        else hi = mid;
    }
    return std::floor(lo);
}

Page PageRenderer::layout(const DisplayContent& content, const Preferences& preferences, const Metrics& m) {
    Typography& typo = Typography::shared();
    const LayoutStyle style = preferences.layout;
    const TextAlign alignment = style == LayoutStyle::Centered ? TextAlign::Center : TextAlign::Left;
    const RectD safe = m.safe();
    const RectD& bounds = m.bounds;

    Page page;
    page.content = content;
    page.layout = style;
    page.typeface = preferences.typeface;
    page.showClock = preferences.showClock;
    page.metrics = m;

    const std::wstring text = widen(content.text);
    const FontFace bodyFace = typo.body(preferences.typeface);
    page.referenceLayout = lineLayout(widen(content.reference), typo.reference(preferences.typeface), m.referenceSize(), 0.06);
    page.clockLayout = lineLayout(widen(content.clock), typo.clock(), m.clockSize(), 0.04);
    const TextBox refSize = metricsOf(page.referenceLayout.Get());
    const TextBox clockSize = metricsOf(page.clockLayout.Get());

    // Vertical space the non-body elements need, per layout, so the body is fitted into what is really left.
    double reserved = 0;
    const double centeredGap = m.referenceSize() * 1.6;
    const double bookGap = m.referenceSize() * 1.4;
    switch (style) {
    case LayoutStyle::Clock: reserved = refSize.height + m.referenceSize() * 1.2; break;
    case LayoutStyle::Centered:
        reserved = centeredGap + refSize.height + (preferences.showClock ? m.clockSize() * 1.6 : 0) + bounds.height * 0.04;
        break;
    case LayoutStyle::Book: reserved = bookGap + 1 + bookGap * 0.8 + refSize.height; break;
    }
    const double boxWidth = safe.width;
    const double boxHeight = std::max(10.0, safe.height - reserved - kBodySlack);

    const double fontSize = fittingFontSize(text, bodyFace, boxWidth, boxHeight, m.minBodySize(), m.bodySize(), m);
    const double width = std::min(boxWidth, m.measureWidth(fontSize));
    const TextBox textSize = measureBody(text, bodyFace, fontSize, width);
    const double textHeight = std::min(textSize.height, boxHeight);
    const double rectHeight = std::min(textHeight + kBodySlack, boxHeight + kBodySlack);

    // Centered text that wraps to many lines reads badly; drop to a left-aligned block for long verses.
    const double lineHeight = typo.lineMetrics(bodyFace).lineHeight() * fontSize;
    const int lines = std::max(1, static_cast<int>(roundSwift(textSize.height / (lineHeight * lineHeightMultiple))));
    const TextAlign finalAlignment = (style == LayoutStyle::Centered && lines > 4) ? TextAlign::Left : alignment;

    // Bottom-left (macOS) coordinates from here to the conversion below.
    RectD body;
    PointD reference;
    std::optional<PointD> clock;
    std::optional<RectD> rule;
    switch (style) {
    case LayoutStyle::Clock:
        // Verse top-left of the safe area, reference bottom-right, clock bottom-left (as on the physical clock).
        body = {safe.minX(), safe.maxY() - rectHeight, width, rectHeight};
        reference = {safe.maxX() - refSize.width, safe.minY()};
        if (preferences.showClock) clock = PointD{safe.minX(), safe.minY() + (refSize.height - clockSize.height) / 2};
        break;
    case LayoutStyle::Centered: {
        // Optically centered: the block sits slightly above true center, reference and clock stacked beneath.
        double stack = textHeight + centeredGap + refSize.height + (preferences.showClock ? m.clockSize() * 1.6 : 0);
        double top = bounds.midY() + stack / 2 + bounds.height * 0.02;
        top = std::min(top, safe.maxY());
        body = {bounds.midX() - width / 2, top - rectHeight, width, rectHeight};
        reference = {bounds.midX() - refSize.width / 2, top - textHeight - centeredGap - refSize.height};
        if (preferences.showClock) clock = PointD{bounds.midX() - clockSize.width / 2, reference.y - m.clockSize() * 1.6};
        break;
    }
    case LayoutStyle::Book: {
        // A left column, vertically centered, with a hairline rule and the reference right-aligned beneath it.
        double stack = textHeight + bookGap + 1 + bookGap * 0.8 + refSize.height;
        double top = bounds.midY() + stack / 2;
        top = std::min(top, safe.maxY());
        body = {safe.minX(), top - rectHeight, width, rectHeight};
        double ruleY = top - textHeight - bookGap;
        rule = RectD{safe.minX(), ruleY, width, 1};
        reference = {safe.minX() + width - refSize.width, ruleY - bookGap * 0.8 - refSize.height};
        if (preferences.showClock) clock = PointD{safe.minX(), reference.y + (refSize.height - clockSize.height) / 2};
        break;
    }
    }

    // Convert to top-left: a box at y (bottom edge) with height h starts at H - y - h.
    const double H = bounds.minY() + bounds.maxY();
    page.bodyFontSize = fontSize;
    page.bodyWidth = width;
    page.bodyRect = {body.x, H - body.y - body.height, body.width, body.height};
    page.referenceOrigin = {reference.x, H - reference.y - refSize.height};
    if (clock) page.clockOrigin = PointD{clock->x, H - clock->y - clockSize.height};
    page.ruleUp = rule;
    page.alignment = finalAlignment;
    page.bodyLayout = bodyLayout(text, bodyFace, fontSize, width, rectHeight, finalAlignment);
    if (!preferences.showClock) page.clockLayout = nullptr;
    return page;
}

unsigned PageRenderer::unchangedParts(const Page& a, const Page& b) {
    if (a.metrics != b.metrics || a.layout != b.layout || a.typeface != b.typeface) return 0;
    unsigned parts = 0;
    if (a.content.text == b.content.text && a.bodyRect == b.bodyRect && a.bodyFontSize == b.bodyFontSize &&
        a.alignment == b.alignment) {
        parts |= Body;
    }
    if (a.content.reference == b.content.reference && a.referenceOrigin == b.referenceOrigin) parts |= Reference;
    if (a.content.clock == b.content.clock && a.clockOrigin == b.clockOrigin) parts |= Clock;
    if (a.ruleUp == b.ruleUp) parts |= Rule;
    return parts;
}

// MARK: - Drawing

void PageRenderer::draw(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, const Page& page, const Palette& palette,
                        float alpha, PointD drift, double scale, unsigned parts) {
    if (alpha <= 0.001f || parts == 0 || !target || !brush) return;
    auto at = [&](double x, double y) {
        return D2D1::Point2F(static_cast<float>(x + drift.x), static_cast<float>(y + drift.y));
    };
    brush->SetOpacity(alpha);

    if ((parts & Body) && page.bodyLayout) {
        brush->SetColor(palette.ink);
        target->DrawTextLayout(at(page.bodyRect.x, page.bodyRect.y), page.bodyLayout.Get(), brush);
    }
    if ((parts & Reference) && page.referenceLayout) {
        brush->SetColor(palette.secondary);
        target->DrawTextLayout(at(page.referenceOrigin.x, page.referenceOrigin.y), page.referenceLayout.Get(), brush);
    }
    if ((parts & Rule) && page.ruleUp) {
        // One device pixel on the pixel grid (crisp at any scale). macOS snaps the rule's bottom edge down to a pixel
        // in bottom-left coordinates, which is the same edge snapped down the screen here.
        const RectD& r = *page.ruleUp;
        const double H = page.metrics.bounds.minY() + page.metrics.bounds.maxY();
        double bottomPx = std::ceil((H - r.minY() + drift.y) * scale - 1e-6);
        float top = static_cast<float>((bottomPx - 1) / scale);
        float bottom = static_cast<float>(bottomPx / scale);
        brush->SetColor(palette.hairline);
        target->FillRectangle(D2D1::RectF(static_cast<float>(r.minX() + drift.x), top,
                                          static_cast<float>(r.minX() + r.width + drift.x), bottom),
                              brush);
    }
    if ((parts & Clock) && page.clockOrigin && page.clockLayout) {
        brush->SetColor(palette.secondary);
        target->DrawTextLayout(at(page.clockOrigin->x, page.clockOrigin->y), page.clockLayout.Get(), brush);
    }
}

}  // namespace bt::app
