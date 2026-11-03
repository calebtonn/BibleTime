import AppKit

/// The Options sheet shown by System Settings. Built in code (no nib) so the bundle can be assembled without Xcode.
final class ConfigureSheet: NSObject {
    let window: NSWindow
    private var preferences: Preferences
    private let completion: (Preferences?) -> Void

    private let translationPopup = NSPopUpButton()
    private let hourPopup = NSPopUpButton()
    private let themePopup = NSPopUpButton()
    private let layoutPopup = NSPopUpButton()
    private let typefacePopup = NSPopUpButton()
    private let sizeSlider = NSSlider()
    private let sizeLabel = NSTextField(labelWithString: "")
    private let clockCheck = NSButton(checkboxWithTitle: "Show the time", target: nil, action: nil)
    private let famousCheck = NSButton(checkboxWithTitle: "Prefer well-known verses (John 3:16 at 3:16)", target: nil, action: nil)
    private let extendCheck = NSButton(checkboxWithTitle: "Complete the sentence when a verse ends mid-thought", target: nil, action: nil)
    private let padCheck = NSButton(checkboxWithTitle: "Pad the reference like a clock (John 03:16)", target: nil, action: nil)
    private let translations: [TranslationInfo]

    init(preferences: Preferences, translations: [TranslationInfo], completion: @escaping (Preferences?) -> Void) {
        self.preferences = preferences
        self.translations = translations
        self.completion = completion
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 480, height: 420),
                          styleMask: [.titled, .closable], backing: .buffered, defer: false)
        super.init()
        window.title = "BibleTime Options"
        window.isReleasedWhenClosed = false
        buildUI()
        apply(preferences)
    }

    // MARK: UI

    private func buildUI() {
        func popup(_ p: NSPopUpButton, titles: [String]) {
            p.removeAllItems()
            p.addItems(withTitles: titles)
            p.target = self
            p.action = #selector(controlChanged)
        }
        popup(translationPopup, titles: translations.isEmpty ? ["King James Version"] : translations.map { "\($0.name) (\($0.abbreviation))" })
        popup(hourPopup, titles: HourStyle.allCases.map(\.title))
        popup(themePopup, titles: Theme.allCases.map(\.title))
        popup(layoutPopup, titles: LayoutStyle.allCases.map(\.title))
        popup(typefacePopup, titles: TypefaceChoice.allCases.map(\.title))

        sizeSlider.minValue = Preferences.textScaleRange.lowerBound
        sizeSlider.maxValue = Preferences.textScaleRange.upperBound
        sizeSlider.target = self
        sizeSlider.action = #selector(controlChanged)
        sizeSlider.isContinuous = true
        sizeLabel.alignment = .right
        sizeLabel.font = NSFont.monospacedDigitSystemFont(ofSize: NSFont.smallSystemFontSize, weight: .regular)
        sizeLabel.textColor = .secondaryLabelColor
        sizeLabel.widthAnchor.constraint(equalToConstant: 44).isActive = true

        for check in [clockCheck, famousCheck, extendCheck, padCheck] {
            check.target = self
            check.action = #selector(controlChanged)
        }

        let sizeRow = NSStackView(views: [sizeSlider, sizeLabel])
        sizeRow.orientation = .horizontal
        sizeRow.spacing = 8

        let grid = NSGridView(views: [
            [label("Translation:"), translationPopup],
            [label("Hours:"), hourPopup],
            [label("Appearance:"), themePopup],
            [label("Layout:"), layoutPopup],
            [label("Typeface:"), typefacePopup],
            [label("Text size:"), sizeRow],
            [NSGridCell.emptyContentView, clockCheck],
            [NSGridCell.emptyContentView, famousCheck],
            [NSGridCell.emptyContentView, extendCheck],
            [NSGridCell.emptyContentView, padCheck],
        ])
        grid.rowSpacing = 10
        grid.columnSpacing = 12
        grid.column(at: 0).xPlacement = .trailing
        grid.column(at: 1).width = 300
        grid.rowAlignment = .firstBaseline

        let title = NSTextField(labelWithString: "BibleTime")
        title.font = NSFont.systemFont(ofSize: 17, weight: .semibold)
        let subtitle = NSTextField(wrappingLabelWithString: "The chapter is the hour and the verse is the minute. At 3:16 you'll see John 3:16.")
        subtitle.textColor = .secondaryLabelColor
        subtitle.font = NSFont.systemFont(ofSize: NSFont.smallSystemFontSize)
        subtitle.preferredMaxLayoutWidth = 420

        let defaultsButton = NSButton(title: "Restore Defaults", target: self, action: #selector(restoreDefaults))
        let cancel = NSButton(title: "Cancel", target: self, action: #selector(cancel(_:)))
        cancel.keyEquivalent = "\u{1b}"
        let ok = NSButton(title: "OK", target: self, action: #selector(save(_:)))
        ok.keyEquivalent = "\r"
        let spacer = NSView()
        spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        let buttons = NSStackView(views: [defaultsButton, spacer, cancel, ok])
        buttons.orientation = .horizontal
        buttons.spacing = 8

        let stack = NSStackView(views: [title, subtitle, grid, buttons])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 14
        stack.setCustomSpacing(4, after: title)
        stack.setCustomSpacing(18, after: subtitle)
        stack.edgeInsets = NSEdgeInsets(top: 20, left: 20, bottom: 20, right: 20)
        stack.translatesAutoresizingMaskIntoConstraints = false
        buttons.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -40).isActive = true

        let content = NSView()
        content.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            stack.topAnchor.constraint(equalTo: content.topAnchor),
            stack.bottomAnchor.constraint(equalTo: content.bottomAnchor),
        ])
        window.contentView = content
        window.setContentSize(content.fittingSize)
    }

    private func label(_ text: String) -> NSTextField {
        let l = NSTextField(labelWithString: text)
        l.alignment = .right
        return l
    }

    // MARK: State

    private func apply(_ p: Preferences) {
        if let i = translations.firstIndex(where: { $0.id == p.translation }) { translationPopup.selectItem(at: i) }
        hourPopup.selectItem(at: HourStyle.allCases.firstIndex(of: p.hourStyle) ?? 0)
        themePopup.selectItem(at: Theme.allCases.firstIndex(of: p.theme) ?? 0)
        layoutPopup.selectItem(at: LayoutStyle.allCases.firstIndex(of: p.layout) ?? 0)
        typefacePopup.selectItem(at: TypefaceChoice.allCases.firstIndex(of: p.typeface) ?? 0)
        sizeSlider.doubleValue = p.textScale
        clockCheck.state = p.showClock ? .on : .off
        famousCheck.state = p.preferFamous ? .on : .off
        extendCheck.state = p.extendFragments ? .on : .off
        padCheck.state = p.padReference ? .on : .off
        updateSizeLabel()
    }

    private func read() -> Preferences {
        var p = Preferences()
        if !translations.isEmpty, translationPopup.indexOfSelectedItem >= 0 { p.translation = translations[translationPopup.indexOfSelectedItem].id }
        p.hourStyle = HourStyle.allCases[max(0, hourPopup.indexOfSelectedItem)]
        p.theme = Theme.allCases[max(0, themePopup.indexOfSelectedItem)]
        p.layout = LayoutStyle.allCases[max(0, layoutPopup.indexOfSelectedItem)]
        p.typeface = TypefaceChoice.allCases[max(0, typefacePopup.indexOfSelectedItem)]
        p.textScale = sizeSlider.doubleValue
        p.showClock = clockCheck.state == .on
        p.preferFamous = famousCheck.state == .on
        p.extendFragments = extendCheck.state == .on
        p.padReference = padCheck.state == .on
        return p.normalized
    }

    private func updateSizeLabel() {
        sizeLabel.stringValue = String(format: "%d%%", Int((sizeSlider.doubleValue * 100).rounded()))
    }

    // MARK: Actions

    @objc private func controlChanged(_ sender: Any?) {
        updateSizeLabel()
    }

    @objc private func restoreDefaults(_ sender: Any?) {
        apply(Preferences())
    }

    @objc private func save(_ sender: Any?) {
        preferences = read()
        dismiss(result: preferences)
    }

    @objc private func cancel(_ sender: Any?) {
        dismiss(result: nil)
    }

    private func dismiss(result: Preferences?) {
        if let parent = window.sheetParent {
            parent.endSheet(window, returnCode: result == nil ? .cancel : .OK)
        } else {
            window.close()
        }
        completion(result)
    }
}
