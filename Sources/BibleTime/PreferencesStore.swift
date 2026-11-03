import Foundation
import ScreenSaver

/// Reads and writes `Preferences` through `ScreenSaverDefaults`, the per-module defaults domain System Settings expects.
///
/// Developer override: when the environment variable `BIBLETIME_PREFS` holds a JSON object (same keys as the
/// settings), it replaces the stored values. The preview host uses this to render every layout without touching defaults.
final class PreferencesStore {
    static let moduleName = Bundle(for: PreferencesStore.self).bundleIdentifier ?? "com.calebtonn.BibleTime"

    private let defaults: ScreenSaverDefaults?

    init() {
        defaults = ScreenSaverDefaults(forModuleWithName: PreferencesStore.moduleName)
    }

    static var environmentOverride: Preferences? {
        guard let json = ProcessInfo.processInfo.environment["BIBLETIME_PREFS"], let data = json.data(using: .utf8),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { return nil }
        return Preferences(dictionary: object)
    }

    func load() -> Preferences {
        if let override = PreferencesStore.environmentOverride { return override }
        guard let defaults else { return Preferences() }
        var dict: [String: Any] = [:]
        for key in Preferences.Key.allCases {
            if let value = defaults.object(forKey: key.rawValue) { dict[key.rawValue] = value }
        }
        return Preferences(dictionary: dict)
    }

    func save(_ preferences: Preferences) {
        guard let defaults else { return }
        for (key, value) in preferences.normalized.dictionary {
            defaults.set(value, forKey: key)
        }
        defaults.synchronize()
        NotificationCenter.default.post(name: BibleTimeView.preferencesDidChange, object: nil)
    }
}
