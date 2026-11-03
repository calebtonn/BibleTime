import AppKit

/// Colors for one theme. Values are sRGB; contrast ratios were checked against WCAG (body ≥ 7:1, secondary ≥ 4.5:1).
struct Palette: Equatable {
    let background: NSColor
    let ink: NSColor
    let secondary: NSColor
    let accent: NSColor
    let hairline: NSColor

    static func hex(_ v: UInt32) -> NSColor {
        NSColor(srgbRed: CGFloat((v >> 16) & 0xff) / 255, green: CGFloat((v >> 8) & 0xff) / 255, blue: CGFloat(v & 0xff) / 255, alpha: 1)
    }

    /// Warm off-white, like the physical clock's display in an oak frame.
    static let paper = Palette(background: hex(0xF3EDE6), ink: hex(0x1C1A17), secondary: hex(0x6B6258), accent: hex(0x8A6D2F), hairline: hex(0xD9D0C4))
    /// Near-black with warm text; avoids pure white on pure black to limit halation at night.
    static let night = Palette(background: hex(0x0E0E10), ink: hex(0xEDE6DA), secondary: hex(0x9C948A), accent: hex(0xC9A96A), hairline: hex(0x2A2A2E))

    static func resolve(_ theme: Theme, appearance: NSAppearance?, hour: Int) -> Palette {
        switch theme {
        case .paper: return .paper
        case .night: return .night
        case .auto:
            let dark = appearance?.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
            return (dark || hour < 7 || hour >= 20) ? .night : .paper
        }
    }
}

/// Font lookup. New York (the system serif) is only reachable through the `.serif` design descriptor;
/// every other family is loaded by PostScript name with a fallback so a Mac without the font still works.
enum Typeface {
    static func body(_ choice: TypefaceChoice, size: CGFloat) -> NSFont {
        switch choice {
        case .newYork: return systemSerif(size: size, weight: .regular)
        case .charter: return named("Charter-Roman", size: size)
        case .palatino: return named("Palatino-Roman", size: size)
        case .georgia: return named("Georgia", size: size)
        case .sans: return NSFont.systemFont(ofSize: size, weight: .regular)
        }
    }

    /// Reference line: a medium weight of the body family with tabular figures so "03:16" never jitters.
    static func reference(_ choice: TypefaceChoice, size: CGFloat) -> NSFont {
        let base: NSFont
        switch choice {
        case .newYork: base = systemSerif(size: size, weight: .medium)
        case .charter: base = named("Charter-Bold", size: size, fallbackWeight: .medium)
        case .palatino: base = named("Palatino-Bold", size: size, fallbackWeight: .medium)
        case .georgia: base = named("Georgia-Bold", size: size, fallbackWeight: .medium)
        case .sans: base = NSFont.systemFont(ofSize: size, weight: .medium)
        }
        return tabular(base)
    }

    /// Small clock: San Francisco with monospaced digits.
    static func clock(size: CGFloat) -> NSFont {
        NSFont.monospacedDigitSystemFont(ofSize: size, weight: .regular)
    }

    static func systemSerif(size: CGFloat, weight: NSFont.Weight) -> NSFont {
        let base = NSFont.systemFont(ofSize: size, weight: weight)
        if let d = base.fontDescriptor.withDesign(.serif), let f = NSFont(descriptor: d, size: size) { return f }
        return NSFont(name: "Charter-Roman", size: size) ?? base
    }

    private static func named(_ name: String, size: CGFloat, fallbackWeight: NSFont.Weight = .regular) -> NSFont {
        NSFont(name: name, size: size) ?? systemSerif(size: size, weight: fallbackWeight)
    }

    /// Request tabular figures. Fonts without the feature (Georgia, Palatino) would make "03:16" jitter as digits
    /// change, so when the request has no effect the reference falls back to New York, which has them.
    private static func tabular(_ font: NSFont) -> NSFont {
        let d = font.fontDescriptor.addingAttributes([
            .featureSettings: [[
                NSFontDescriptor.FeatureKey.typeIdentifier: kNumberSpacingType,
                NSFontDescriptor.FeatureKey.selectorIdentifier: kMonospacedNumbersSelector,
            ]],
        ])
        let candidate = NSFont(descriptor: d, size: font.pointSize) ?? font
        if hasUniformDigits(candidate) { return candidate }
        let serif = systemSerif(size: font.pointSize, weight: .medium)
        let serifTabular = NSFont(descriptor: serif.fontDescriptor.addingAttributes([
            .featureSettings: [[
                NSFontDescriptor.FeatureKey.typeIdentifier: kNumberSpacingType,
                NSFontDescriptor.FeatureKey.selectorIdentifier: kMonospacedNumbersSelector,
            ]],
        ]), size: font.pointSize) ?? serif
        return hasUniformDigits(serifTabular) ? serifTabular : NSFont.monospacedDigitSystemFont(ofSize: font.pointSize, weight: .medium)
    }

    private static func hasUniformDigits(_ font: NSFont) -> Bool {
        let widths = (0...9).map { ("\($0)" as NSString).size(withAttributes: [.font: font]).width }
        guard let min = widths.min(), let max = widths.max() else { return false }
        return max - min < 0.05
    }
}

/// Geometry derived from the view size. Everything is in points; all values scale with the screen.
struct Metrics: Equatable {
    let bounds: CGRect
    let isPreview: Bool
    let textScale: CGFloat

    init(bounds: CGRect, isPreview: Bool, textScale: CGFloat) {
        self.bounds = bounds
        self.isPreview = isPreview
        self.textScale = textScale
    }

    var minDim: CGFloat { min(bounds.width, bounds.height) }
    var margin: CGFloat { round(minDim * (isPreview ? 0.06 : 0.08)) }
    var safe: CGRect { bounds.insetBy(dx: margin, dy: margin) }

    /// Default body is 4% of the height (desk viewing); the user scale lets it grow to ~8% for across-the-room reading.
    var bodySize: CGFloat { max(isPreview ? 10 : 18, round(bounds.height * 0.040 * textScale)) }
    /// Shrink-to-fit floor for very long verses, portrait displays and the preview.
    var minBodySize: CGFloat { max(isPreview ? 9 : 16, round(bounds.height * 0.018)) }
    var referenceSize: CGFloat { max(isPreview ? 8 : 12, round(bounds.height * 0.020 * sqrt(textScale))) }
    var clockSize: CGFloat { max(isPreview ? 8 : 12, round(bounds.height * 0.016 * sqrt(textScale))) }

    /// Width for about 66 characters per line (0.425 em average advance measured for New York over KJV prose).
    func measureWidth(for fontSize: CGFloat) -> CGFloat { min(safe.width, ceil(66 * fontSize * 0.425)) }
}
