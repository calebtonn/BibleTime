import AppKit

/// A fully laid-out minute of content: the fitted font size and every rectangle, so `draw` is cheap.
struct Page: Equatable {
    let content: DisplayContent
    let layout: LayoutStyle
    let typeface: TypefaceChoice
    let showClock: Bool
    let metrics: Metrics
    let bodyFontSize: CGFloat
    let bodyWidth: CGFloat
    /// The rect handed to `draw(with:)`. Slightly taller than the measured text so the last line is never clipped;
    /// text is drawn from the top edge, so the extra height at the bottom does not move anything.
    let bodyRect: CGRect
    let referenceOrigin: CGPoint
    let clockOrigin: CGPoint?
    let rule: CGRect?
    let alignment: NSTextAlignment
}

/// Measures and draws pages. Stateless; all inputs come from the page and the palette.
enum PageRenderer {
    static let lineHeightMultiple: CGFloat = 1.3
    /// Slack added below the measured text; boundingRect can under-report by a fraction of a line.
    private static let bodySlack: CGFloat = 4

    struct Part: OptionSet, Hashable {
        let rawValue: Int
        static let body = Part(rawValue: 1)
        static let reference = Part(rawValue: 2)
        static let clock = Part(rawValue: 4)
        static let rule = Part(rawValue: 8)
        static let all: Part = [.body, .reference, .clock, .rule]
    }

    // MARK: Layout

    static func layout(content: DisplayContent, preferences: Preferences, metrics m: Metrics) -> Page {
        let style = preferences.layout
        let alignment: NSTextAlignment = style == .centered ? .center : .left

        let refFont = Typeface.reference(preferences.typeface, size: m.referenceSize)
        let refAttributes = referenceAttributes(font: refFont, color: .black, size: m.referenceSize)
        let refSize = (content.reference as NSString).size(withAttributes: refAttributes)
        let clockAttrs = clockAttributes(size: m.clockSize, color: .black)
        let clockSize = (content.clock as NSString).size(withAttributes: clockAttrs)

        // Vertical space the non-body elements need, per layout, so the body is fitted into what is really left.
        let reserved: CGFloat
        let centeredGap = m.referenceSize * 1.6
        let bookGap = m.referenceSize * 1.4
        switch style {
        case .clock:
            reserved = refSize.height + m.referenceSize * 1.2
        case .centered:
            reserved = centeredGap + refSize.height + (preferences.showClock ? m.clockSize * 1.6 : 0) + m.bounds.height * 0.04
        case .book:
            reserved = bookGap + 1 + bookGap * 0.8 + refSize.height
        }
        let box = CGSize(width: m.safe.width, height: max(10, m.safe.height - reserved - bodySlack))

        let fontSize = fittingFontSize(text: content.text, typeface: preferences.typeface, box: box,
                                       minSize: m.minBodySize, maxSize: m.bodySize, measure: m.measureWidth(for:),
                                       alignment: alignment)
        let width = min(box.width, m.measureWidth(for: fontSize))
        let font = Typeface.body(preferences.typeface, size: fontSize)
        let textSize = measure(text: content.text, font: font, width: width, alignment: alignment)
        let textHeight = min(textSize.height, box.height)
        let rectHeight = min(textHeight + bodySlack, box.height + bodySlack)

        // Centered text that wraps to many lines reads badly; drop to a left-aligned block for long verses.
        let lineHeight = font.ascender - font.descender + font.leading
        let lines = max(1, Int((textSize.height / (lineHeight * lineHeightMultiple)).rounded()))
        let finalAlignment: NSTextAlignment = (style == .centered && lines > 4) ? .left : alignment

        var bodyRect: CGRect
        var referenceOrigin: CGPoint
        var clockOrigin: CGPoint?
        var rule: CGRect?

        switch style {
        case .clock:
            // Verse top-left of the safe area, reference bottom-right, clock bottom-left (as on the physical clock).
            bodyRect = CGRect(x: m.safe.minX, y: m.safe.maxY - rectHeight, width: width, height: rectHeight)
            referenceOrigin = CGPoint(x: m.safe.maxX - refSize.width, y: m.safe.minY)
            if preferences.showClock { clockOrigin = CGPoint(x: m.safe.minX, y: m.safe.minY + (refSize.height - clockSize.height) / 2) }
        case .centered:
            // Optically centered: the block sits slightly above true center, reference and clock stacked beneath.
            let stack = textHeight + centeredGap + refSize.height + (preferences.showClock ? m.clockSize * 1.6 : 0)
            var top = m.bounds.midY + stack / 2 + m.bounds.height * 0.02
            top = min(top, m.safe.maxY)
            bodyRect = CGRect(x: m.bounds.midX - width / 2, y: top - rectHeight, width: width, height: rectHeight)
            referenceOrigin = CGPoint(x: m.bounds.midX - refSize.width / 2, y: top - textHeight - centeredGap - refSize.height)
            if preferences.showClock { clockOrigin = CGPoint(x: m.bounds.midX - clockSize.width / 2, y: referenceOrigin.y - m.clockSize * 1.6) }
        case .book:
            // A left column, vertically centered, with a hairline rule and the reference right-aligned beneath it.
            let stack = textHeight + bookGap + 1 + bookGap * 0.8 + refSize.height
            var top = m.bounds.midY + stack / 2
            top = min(top, m.safe.maxY)
            bodyRect = CGRect(x: m.safe.minX, y: top - rectHeight, width: width, height: rectHeight)
            let ruleY = top - textHeight - bookGap
            rule = CGRect(x: m.safe.minX, y: ruleY, width: width, height: 1)
            referenceOrigin = CGPoint(x: m.safe.minX + width - refSize.width, y: ruleY - bookGap * 0.8 - refSize.height)
            if preferences.showClock { clockOrigin = CGPoint(x: m.safe.minX, y: referenceOrigin.y + (refSize.height - clockSize.height) / 2) }
        }

        return Page(content: content, layout: style, typeface: preferences.typeface, showClock: preferences.showClock,
                    metrics: m, bodyFontSize: fontSize, bodyWidth: width, bodyRect: bodyRect,
                    referenceOrigin: referenceOrigin, clockOrigin: clockOrigin, rule: rule, alignment: finalAlignment)
    }

    /// Parts that are pixel-identical on both pages and therefore need no crossfade.
    static func unchangedParts(from a: Page, to b: Page) -> Part {
        guard a.metrics == b.metrics, a.layout == b.layout, a.typeface == b.typeface else { return [] }
        var parts: Part = []
        if a.content.text == b.content.text, a.bodyRect == b.bodyRect, a.bodyFontSize == b.bodyFontSize, a.alignment == b.alignment { parts.insert(.body) }
        if a.content.reference == b.content.reference, a.referenceOrigin == b.referenceOrigin { parts.insert(.reference) }
        if a.content.clock == b.content.clock, a.clockOrigin == b.clockOrigin { parts.insert(.clock) }
        if a.rule == b.rule { parts.insert(.rule) }
        return parts
    }

    // MARK: Drawing

    /// Draw the requested parts of a page with the given opacity (used for the crossfade), offset by `drift`.
    /// `scale` is the backing scale factor, used to snap the hairline to device pixels.
    static func draw(_ page: Page, palette: Palette, alpha: CGFloat, drift: CGPoint, scale: CGFloat, parts: Part = .all) {
        guard alpha > 0.001, !parts.isEmpty else { return }
        let ink = palette.ink.withAlphaComponent(alpha)
        let secondary = palette.secondary.withAlphaComponent(alpha)

        if parts.contains(.body) {
            let font = Typeface.body(page.typeface, size: page.bodyFontSize)
            let attributes = bodyAttributes(font: font, color: ink, alignment: page.alignment)
            let bodyRect = page.bodyRect.offsetBy(dx: drift.x, dy: drift.y)
            (page.content.text as NSString).draw(with: bodyRect, options: [.usesLineFragmentOrigin, .usesFontLeading, .truncatesLastVisibleLine], attributes: attributes)
        }

        if parts.contains(.reference) {
            let refFont = Typeface.reference(page.typeface, size: page.metrics.referenceSize)
            let refAttributes = referenceAttributes(font: refFont, color: secondary, size: page.metrics.referenceSize)
            (page.content.reference as NSString).draw(at: page.referenceOrigin.applying(drift), withAttributes: refAttributes)
        }

        if parts.contains(.rule), let rule = page.rule {
            palette.hairline.withAlphaComponent(alpha).setFill()
            // One device pixel, on the pixel grid, so the line is crisp on Retina and non-Retina alike.
            let y = (floor((rule.minY + drift.y) * scale) / scale)
            CGRect(x: rule.minX + drift.x, y: y, width: rule.width, height: 1 / scale).fill()
        }

        if parts.contains(.clock), let clockOrigin = page.clockOrigin {
            (page.content.clock as NSString).draw(at: clockOrigin.applying(drift), withAttributes: clockAttributes(size: page.metrics.clockSize, color: secondary))
        }
    }

    // MARK: Text attributes and measurement

    static func paragraphStyle(alignment: NSTextAlignment) -> NSParagraphStyle {
        let p = NSMutableParagraphStyle()
        p.alignment = alignment
        p.lineHeightMultiple = lineHeightMultiple
        p.lineBreakMode = .byWordWrapping
        p.hyphenationFactor = 0
        return p
    }

    static func bodyAttributes(font: NSFont, color: NSColor, alignment: NSTextAlignment) -> [NSAttributedString.Key: Any] {
        [.font: font, .foregroundColor: color, .paragraphStyle: paragraphStyle(alignment: alignment), .kern: font.pointSize * 0.005]
    }

    static func referenceAttributes(font: NSFont, color: NSColor, size: CGFloat) -> [NSAttributedString.Key: Any] {
        [.font: font, .foregroundColor: color, .kern: size * 0.06]
    }

    static func clockAttributes(size: CGFloat, color: NSColor) -> [NSAttributedString.Key: Any] {
        [.font: Typeface.clock(size: size), .foregroundColor: color, .kern: size * 0.04]
    }

    static func measure(text: String, font: NSFont, width: CGFloat, alignment: NSTextAlignment) -> CGSize {
        let r = (text as NSString).boundingRect(
            with: CGSize(width: width, height: .greatestFiniteMagnitude),
            options: [.usesLineFragmentOrigin, .usesFontLeading],
            attributes: bodyAttributes(font: font, color: .black, alignment: alignment))
        return CGSize(width: ceil(r.width), height: ceil(r.height))
    }

    /// Largest point size in [minSize, maxSize] whose wrapped text fits `box` (binary search, 0.5 pt resolution).
    /// If even `minSize` overflows (a 500-character verse in the 184-pt preview), keeps shrinking to a hard floor of
    /// 7 pt before giving up; past that the body is truncated with an ellipsis.
    static func fittingFontSize(text: String, typeface: TypefaceChoice, box: CGSize, minSize: CGFloat, maxSize: CGFloat,
                                measure measureWidth: (CGFloat) -> CGFloat, alignment: NSTextAlignment) -> CGFloat {
        func fits(_ size: CGFloat) -> Bool {
            let font = Typeface.body(typeface, size: size)
            let s = measure(text: text, font: font, width: min(box.width, measureWidth(size)), alignment: alignment)
            return s.height <= box.height
        }
        if fits(maxSize) { return maxSize }
        let floorSize: CGFloat = 7
        var lo = fits(minSize) ? minSize : floorSize
        var hi = fits(minSize) ? maxSize : minSize
        if lo == floorSize, !fits(floorSize) { return floorSize }
        while hi - lo > 0.5 {
            let mid = (lo + hi) / 2
            if fits(mid) { lo = mid } else { hi = mid }
        }
        return floor(lo)
    }
}

private extension CGPoint {
    func applying(_ drift: CGPoint) -> CGPoint { CGPoint(x: x + drift.x, y: y + drift.y) }
}
