import AppKit
import ScreenSaver

/// The screen saver. One instance per display. All drawing is plain AppKit in `draw(_:)`; the only animation is a
/// one-second crossfade when the minute changes, plus a few points of drift every five minutes for burn-in protection.
@objc(BibleTimeView)
final class BibleTimeView: ScreenSaverView {
    private static let fadeDuration: TimeInterval = 1.0
    private static let idleInterval: TimeInterval = 1.0
    private static let fadeInterval: TimeInterval = 1.0 / 30.0
    private static let driftPeriod: TimeInterval = 300
    private static let driftRadius: CGFloat = 6

    /// Posted by `PreferencesStore.save` so every instance in the process (System Settings keeps several) reloads.
    static let preferencesDidChange = Notification.Name("com.calebtonn.BibleTime.preferencesDidChange")

    /// The verse data is immutable once loaded and the catalog is locked, so one copy serves every instance.
    /// System Settings creates several views per preview and never releases them; this keeps memory flat.
    private static let sharedClock: Result<VerseClock, Error> = {
        let bundle = Bundle(for: BibleTimeView.self)
        guard let resources = bundle.resourceURL else {
            return .failure(VerseClock.LoadError.missing("Resources"))
        }
        return Result { try VerseClock(resources: resources) }
    }()

    private let store = PreferencesStore()
    private var preferences = Preferences()
    private var clock: VerseClock?
    private var loadError: String?

    private var current: Page?
    private var previous: Page?
    private var fadeStart: TimeInterval?
    private var palette: Palette = .paper
    private var drift: CGPoint = .zero
    private var lastDrift: TimeInterval = 0
    private var laidOutBounds: CGRect = .zero
    private var sheet: ConfigureSheet?

    // MARK: Lifecycle

    override init?(frame: NSRect, isPreview: Bool) {
        super.init(frame: frame, isPreview: isPreview)
        commonInit()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        commonInit()
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
    }

    private func commonInit() {
        animationTimeInterval = BibleTimeView.idleInterval
        preferences = store.load()
        switch BibleTimeView.sharedClock {
        case .success(let clock): self.clock = clock
        case .failure(let error): loadError = "\(error)"
        }
        NotificationCenter.default.addObserver(self, selector: #selector(preferencesChanged), name: BibleTimeView.preferencesDidChange, object: nil)
    }

    @objc private func preferencesChanged() {
        preferences = store.load()
        refresh(now: DevOverrides.now(), animated: false)
    }

    /// System Settings on macOS 26 creates a zero-sized preview instance; treat small views as previews whatever `isPreview` says.
    private var effectivelyPreview: Bool { isPreview || bounds.height < 300 || bounds.width < 400 }

    /// Which display this view is on: 0 for the main display (the one with the menu bar), counting up in the
    /// order macOS lists screens. Unknown (no window yet, or the preview) counts as the main display.
    private var displayIndex: Int {
        if let pinned = DevOverrides.pinnedDisplayIndex { return pinned }
        guard !effectivelyPreview, let screen = window?.screen else { return 0 }
        let screens = NSScreen.screens
        return screens.firstIndex(where: { $0 == screen }) ?? 0
    }

    /// True when this view should draw only the background (Main-display-only mode on a secondary screen).
    private var showsOnlyBackground: Bool {
        preferences.displayMode == .mainOnly && displayIndex > 0 && !effectivelyPreview
    }

    override var hasConfigureSheet: Bool { true }

    override var configureSheet: NSWindow? {
        let clock = self.clock
        let actions = ConfigureSheet.TranslationActions(
            list: { clock?.catalog.available ?? [] },
            importFile: { url in
                guard let clock, let importer = clock.importer else { throw VerseClock.LoadError.missing("import directory") }
                let info = try importer.importFile(at: url)
                clock.catalog.rescan()
                return info
            },
            remove: { info in
                guard let clock, let importer = clock.importer else { return }
                try importer.remove(info)
                clock.catalog.rescan()
            })
        let sheet = ConfigureSheet(preferences: store.load(), actions: actions) { [weak self] result in
            guard let self else { return }
            if let result {
                self.store.save(result)   // posts preferencesDidChange, which refreshes every instance including this one
            }
            self.sheet = nil
        }
        self.sheet = sheet
        return sheet.window
    }

    // MARK: Animation

    override func startAnimation() {
        super.startAnimation()
        preferences = store.load()
        refresh(now: DevOverrides.now(), animated: false)
    }

    override func stopAnimation() {
        super.stopAnimation()
        endFade()
    }

    override func animateOneFrame() {
        let now = DevOverrides.now()
        let seconds = now.timeIntervalSinceReferenceDate

        if let start = fadeStart {
            if seconds - start >= BibleTimeView.fadeDuration { endFade() }
            needsDisplay = true
        }

        let minuteKey = ClockTime(date: now).minuteKey
        if current == nil || current?.content.minuteKey != minuteKey || laidOutBounds != bounds {
            refresh(now: now, animated: current != nil && laidOutBounds == bounds)
        }

        if !effectivelyPreview, seconds - lastDrift > BibleTimeView.driftPeriod {
            lastDrift = seconds
            let r = BibleTimeView.driftRadius
            drift = CGPoint(x: CGFloat.random(in: -r...r).rounded(), y: CGFloat.random(in: -r...r).rounded())
            needsDisplay = true
        }
    }

    override func setFrameSize(_ newSize: NSSize) {
        super.setFrameSize(newSize)
        if newSize.width > 0, newSize.height > 0, laidOutBounds != bounds {
            refresh(now: DevOverrides.now(), animated: false)
        }
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        if window != nil, bounds.width > 0, bounds.height > 0 { refresh(now: DevOverrides.now(), animated: false) }
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        refresh(now: DevOverrides.now(), animated: false)
    }

    private func endFade() {
        fadeStart = nil
        previous = nil
        animationTimeInterval = BibleTimeView.idleInterval
    }

    /// Recompute the page for `now`. Called at most once a minute outside of size and settings changes.
    private func refresh(now: Date, animated: Bool) {
        guard bounds.width > 0, bounds.height > 0 else { return }
        let time = ClockTime(date: now)
        palette = Palette.resolve(preferences.theme, appearance: effectiveAppearance, hour: time.hour)

        let variant = preferences.displayMode == .differentVerses ? displayIndex : 0
        let content: DisplayContent
        if showsOnlyBackground {
            content = DisplayContent(minuteKey: time.minuteKey, text: "", reference: "", clock: "", kind: .fallback, translation: "")
        } else if let clock {
            do {
                if let ref = DevOverrides.pinnedReference, let pinned = try clock.content(for: ref, date: now, preferences: preferences) {
                    content = pinned
                } else {
                    content = try clock.content(for: now, preferences: preferences, variant: variant)
                }
            } catch {
                content = DisplayContent(minuteKey: time.minuteKey, text: "\(error)", reference: "", clock: "", kind: .fallback, translation: "")
            }
        } else {
            content = DisplayContent(minuteKey: time.minuteKey, text: loadError ?? "BibleTime could not load its verses.",
                                     reference: "", clock: "", kind: .fallback, translation: "")
        }

        let metrics = Metrics(bounds: bounds, isPreview: effectivelyPreview, textScale: CGFloat(preferences.textScale))
        let page = PageRenderer.layout(content: content, preferences: preferences, metrics: metrics)
        laidOutBounds = bounds

        if animated, let old = current, old.content != page.content {
            previous = old
            fadeStart = now.timeIntervalSinceReferenceDate
            animationTimeInterval = BibleTimeView.fadeInterval
        } else {
            endFade()
        }
        current = page
        needsDisplay = true
    }

    // MARK: Drawing

    override func draw(_ rect: NSRect) {
        // A secondary display in Main-display-only mode stays dark, whatever the theme.
        (showsOnlyBackground ? NSColor.black : palette.background).setFill()
        bounds.fill()
        guard let current, !current.content.text.isEmpty else { return }
        let scale = window?.backingScaleFactor ?? 2

        guard let start = fadeStart, let previous else {
            PageRenderer.draw(current, palette: palette, alpha: 1, drift: drift, scale: scale)
            return
        }
        let t = min(1, max(0, (Date().timeIntervalSinceReferenceDate - start) / BibleTimeView.fadeDuration))
        let alpha = CGFloat(t * t * (3 - 2 * t))   // smoothstep

        // Elements that are identical on both pages (usually the clock and the rule) are drawn once at full
        // opacity so they do not dim and brighten every minute; only what changed crossfades.
        let shared = PageRenderer.unchangedParts(from: previous, to: current)
        PageRenderer.draw(previous, palette: palette, alpha: 1 - alpha, drift: drift, scale: scale, parts: PageRenderer.Part.all.subtracting(shared))
        PageRenderer.draw(current, palette: palette, alpha: alpha, drift: drift, scale: scale, parts: PageRenderer.Part.all.subtracting(shared))
        PageRenderer.draw(current, palette: palette, alpha: 1, drift: drift, scale: scale, parts: shared)
    }

    override var isOpaque: Bool { true }
}
