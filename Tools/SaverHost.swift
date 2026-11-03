import AppKit
import ScreenSaver

// A tiny host that loads a .saver bundle, runs it in an offscreen window and writes a PNG.
// It is the development loop for this project: System Settings caches screen saver bundles aggressively,
// so iterating there is slow, and legacyScreenSaver gives no feedback when a bundle fails to load.
//
// Usage: SaverHost <path/to/BibleTime.saver> --out render.png [--width 1440] [--height 900] [--seconds 1.5] [--preview]
// Environment: BIBLETIME_TIME="15:16", BIBLETIME_REF="EST 8:9" and BIBLETIME_PREFS='{"layout":"book"}' are honoured by the saver.

struct Options {
    var bundlePath = ""
    var out = "render.png"
    var width: Double = 1440
    var height: Double = 900
    var seconds: Double = 1.5
    var preview = false
}

func parse() -> Options {
    var o = Options()
    var args = Array(CommandLine.arguments.dropFirst())
    func take(_ flag: String) -> String? {
        guard let i = args.firstIndex(of: flag), i + 1 < args.count else { return nil }
        let v = args[i + 1]
        args.removeSubrange(i...(i + 1))
        return v
    }
    if let v = take("--out") { o.out = v }
    if let v = take("--width"), let d = Double(v) { o.width = d }
    if let v = take("--height"), let d = Double(v) { o.height = d }
    if let v = take("--seconds"), let d = Double(v) { o.seconds = d }
    if let i = args.firstIndex(of: "--preview") { o.preview = true; args.remove(at: i) }
    guard let path = args.first else {
        fputs("usage: SaverHost <bundle.saver> [--out file.png] [--width W] [--height H] [--seconds S] [--preview]\n", stderr)
        exit(2)
    }
    o.bundlePath = path
    return o
}

let options = parse()

guard let bundle = Bundle(path: options.bundlePath) else { fputs("error: not a bundle: \(options.bundlePath)\n", stderr); exit(1) }
guard bundle.load() else { fputs("error: bundle.load() failed (check codesign and architecture)\n", stderr); exit(1) }
guard let principal = bundle.principalClass else { fputs("error: NSPrincipalClass did not resolve\n", stderr); exit(1) }
guard let saverClass = principal as? ScreenSaverView.Type else { fputs("error: principal class is not a ScreenSaverView\n", stderr); exit(1) }

let app = NSApplication.shared
app.setActivationPolicy(.prohibited)

let frame = NSRect(x: 0, y: 0, width: options.width, height: options.height)
let window = NSWindow(contentRect: frame, styleMask: [.borderless], backing: .buffered, defer: false)
window.isReleasedWhenClosed = false
guard let view = saverClass.init(frame: frame, isPreview: options.preview) else { fputs("error: init(frame:isPreview:) returned nil\n", stderr); exit(1) }
window.contentView = view
view.startAnimation()
print("loaded \(NSStringFromClass(principal)) \(bundle.infoDictionary?["CFBundleShortVersionString"] ?? "") build \(bundle.infoDictionary?["CFBundleVersion"] ?? "") preview=\(view.isPreview) interval=\(view.animationTimeInterval)")

// Pump the run loop so animateOneFrame fires (the saver only lays out once it has a size and a tick).
RunLoop.main.run(until: Date(timeIntervalSinceNow: options.seconds))

view.displayIfNeeded()
guard let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { fputs("error: no bitmap rep\n", stderr); exit(1) }
view.cacheDisplay(in: view.bounds, to: rep)
guard let png = rep.representation(using: .png, properties: [:]) else { fputs("error: png encode failed\n", stderr); exit(1) }
do {
    try png.write(to: URL(fileURLWithPath: options.out))
} catch {
    fputs("error: \(error)\n", stderr)
    exit(1)
}
print("wrote \(options.out) (\(rep.pixelsWide)x\(rep.pixelsHigh))")
view.stopAnimation()
window.close()
exit(0)
