import AppKit

/// The Options sheet shown by System Settings. Built in code (no nib) so the bundle can be assembled without Xcode.
final class ConfigureSheet: NSObject {
    /// What the sheet needs from its owner to manage translations.
    struct TranslationActions {
        /// Current list (bundled first, then imported).
        var list: () -> [TranslationInfo]
        /// Validate and copy a file chosen by the user; returns the new entry.
        var importFile: (URL) throws -> TranslationInfo
        /// Delete an imported translation.
        var remove: (TranslationInfo) throws -> Void
    }

    let window: NSWindow
    private var preferences: Preferences
    private let completion: (Preferences?) -> Void
    private let actions: TranslationActions
    private var translations: [TranslationInfo]

    private let translationPopup = NSPopUpButton()
    private let importButton = NSButton(title: "Import…", target: nil, action: nil)
    private let removeButton = NSButton(title: "Remove", target: nil, action: nil)
    private let hourPopup = NSPopUpButton()
    private let themePopup = NSPopUpButton()
    private let layoutPopup = NSPopUpButton()
    private let typefacePopup = NSPopUpButton()
    private let displayPopup = NSPopUpButton()
    private let sizeSlider = NSSlider()
    private let sizeLabel = NSTextField(labelWithString: "")
    private let clockCheck = NSButton(checkboxWithTitle: "Show the time", target: nil, action: nil)
    private let famousCheck = NSButton(checkboxWithTitle: "Prefer well-known verses (John 3:16 at 3:16)", target: nil, action: nil)
    private let extendCheck = NSButton(checkboxWithTitle: "Complete the sentence when a verse ends mid-thought", target: nil, action: nil)
    private let padCheck = NSButton(checkboxWithTitle: "Pad the reference like a clock (John 03:16)", target: nil, action: nil)

    init(preferences: Preferences, actions: TranslationActions, completion: @escaping (Preferences?) -> Void) {
        self.preferences = preferences
        self.actions = actions
        self.translations = actions.list()
        self.completion = completion
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 560, height: 440),
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
        reloadTranslationPopup(selecting: preferences.translation)
        translationPopup.target = self
        translationPopup.action = #selector(controlChanged)
        popup(hourPopup, titles: HourStyle.allCases.map(\.title))
        popup(themePopup, titles: Theme.allCases.map(\.title))
        popup(layoutPopup, titles: LayoutStyle.allCases.map(\.title))
        popup(typefacePopup, titles: TypefaceChoice.allCases.map(\.title))
        popup(displayPopup, titles: DisplayMode.allCases.map(\.title))

        importButton.target = self
        importButton.action = #selector(importTranslation)
        importButton.toolTip = "Add a translation file made with Tools/build_verses.py (for a copy you are licensed to use)."
        removeButton.target = self
        removeButton.action = #selector(removeTranslation)
        let translationRow = NSStackView(views: [translationPopup, importButton, removeButton])
        translationRow.orientation = .horizontal
        translationRow.spacing = 6
        translationPopup.setContentHuggingPriority(.defaultLow, for: .horizontal)
        translationPopup.widthAnchor.constraint(greaterThanOrEqualToConstant: 250).isActive = true

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
            [label("Translation:"), translationRow],
            [label("Hours:"), hourPopup],
            [label("Appearance:"), themePopup],
            [label("Layout:"), layoutPopup],
            [label("Typeface:"), typefacePopup],
            [label("Text size:"), sizeRow],
            [label("Displays:"), displayPopup],
            [NSGridCell.emptyContentView, clockCheck],
            [NSGridCell.emptyContentView, famousCheck],
            [NSGridCell.emptyContentView, extendCheck],
            [NSGridCell.emptyContentView, padCheck],
        ])
        grid.rowSpacing = 10
        grid.columnSpacing = 12
        grid.column(at: 0).xPlacement = .trailing
        grid.column(at: 1).width = 400
        grid.rowAlignment = .firstBaseline

        let title = NSTextField(labelWithString: "BibleTime")
        title.font = NSFont.systemFont(ofSize: 17, weight: .semibold)
        let subtitle = NSTextField(wrappingLabelWithString: "The chapter is the hour and the verse is the minute. At 3:16 you'll see John 3:16.")
        subtitle.textColor = .secondaryLabelColor
        subtitle.font = NSFont.systemFont(ofSize: NSFont.smallSystemFontSize)
        subtitle.preferredMaxLayoutWidth = 500

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

    /// Bundled translations first, a separator, then imported ones. Each menu item carries its translation id.
    private func reloadTranslationPopup(selecting id: String) {
        translations = actions.list()
        translationPopup.removeAllItems()
        if translations.isEmpty {
            translationPopup.addItem(withTitle: "No translations found")
        }
        var separatorAdded = false
        for info in translations {
            if info.isImported, !separatorAdded, translationPopup.numberOfItems > 0 {
                translationPopup.menu?.addItem(.separator())
                separatorAdded = true
            }
            translationPopup.addItem(withTitle: info.displayName)
            translationPopup.lastItem?.representedObject = info.id
        }
        let item = translationPopup.itemArray.first { ($0.representedObject as? String) == id }
            ?? translationPopup.itemArray.first { $0.representedObject != nil }
        if let item { translationPopup.select(item) }
        updateRemoveButton()
    }

    private var selectedTranslation: TranslationInfo? {
        guard let id = translationPopup.selectedItem?.representedObject as? String else { return nil }
        return translations.first { $0.id == id }
    }

    private func updateRemoveButton() {
        removeButton.isEnabled = selectedTranslation?.isImported == true
    }

    // MARK: State

    private func apply(_ p: Preferences) {
        reloadTranslationPopup(selecting: p.translation)
        hourPopup.selectItem(at: HourStyle.allCases.firstIndex(of: p.hourStyle) ?? 0)
        themePopup.selectItem(at: Theme.allCases.firstIndex(of: p.theme) ?? 0)
        layoutPopup.selectItem(at: LayoutStyle.allCases.firstIndex(of: p.layout) ?? 0)
        typefacePopup.selectItem(at: TypefaceChoice.allCases.firstIndex(of: p.typeface) ?? 0)
        displayPopup.selectItem(at: DisplayMode.allCases.firstIndex(of: p.displayMode) ?? 1)
        sizeSlider.doubleValue = p.textScale
        clockCheck.state = p.showClock ? .on : .off
        famousCheck.state = p.preferFamous ? .on : .off
        extendCheck.state = p.extendFragments ? .on : .off
        padCheck.state = p.padReference ? .on : .off
        updateSizeLabel()
    }

    private func read() -> Preferences {
        var p = Preferences()
        if let selected = selectedTranslation { p.translation = selected.id }
        p.hourStyle = HourStyle.allCases[max(0, hourPopup.indexOfSelectedItem)]
        p.theme = Theme.allCases[max(0, themePopup.indexOfSelectedItem)]
        p.layout = LayoutStyle.allCases[max(0, layoutPopup.indexOfSelectedItem)]
        p.typeface = TypefaceChoice.allCases[max(0, typefacePopup.indexOfSelectedItem)]
        p.displayMode = DisplayMode.allCases[max(0, displayPopup.indexOfSelectedItem)]
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
        updateRemoveButton()
    }

    @objc private func restoreDefaults(_ sender: Any?) {
        apply(Preferences())
    }

    @objc private func importTranslation(_ sender: Any?) {
        let panel = NSOpenPanel()
        panel.title = "Import a Translation"
        panel.message = "Choose a translation file made with Tools/build_verses.py. You are responsible for holding a license to the text."
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.allowedContentTypes = [.json]
        panel.beginSheetModal(for: window) { [weak self] response in
            guard let self, response == .OK, let url = panel.url else { return }
            do {
                let info = try self.actions.importFile(url)
                self.reloadTranslationPopup(selecting: info.id)
            } catch {
                self.showError("Could not import \(url.lastPathComponent)", detail: "\(error)")
            }
        }
    }

    @objc private func removeTranslation(_ sender: Any?) {
        guard let info = selectedTranslation, info.isImported else { return }
        let alert = NSAlert()
        alert.messageText = "Remove \(info.name)?"
        alert.informativeText = "The imported file will be deleted. The bundled translations are not affected."
        alert.addButton(withTitle: "Remove")
        alert.addButton(withTitle: "Cancel")
        alert.beginSheetModal(for: window) { [weak self] response in
            guard let self, response == .alertFirstButtonReturn else { return }
            do {
                try self.actions.remove(info)
                self.reloadTranslationPopup(selecting: Preferences().translation)
            } catch {
                self.showError("Could not remove \(info.name)", detail: "\(error)")
            }
        }
    }

    private func showError(_ message: String, detail: String) {
        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = message
        alert.informativeText = detail
        alert.beginSheetModal(for: window)
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
