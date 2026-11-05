#include "Platform.h"

#include <shlobj.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <iterator>
#include <system_error>

namespace bt::app {

// MARK: - Win32.h helpers

std::wstring widen(std::string_view utf8) {
    if (utf8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string narrow(std::wstring_view wide) {
    if (wide.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring modulePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (n == 0) return {};
        if (n < buffer.size()) {
            buffer.resize(n);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring environment(const wchar_t* name, bool* set) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (set) *set = n > 0;
    if (n == 0) return {};
    std::wstring value(n, L'\0');
    n = GetEnvironmentVariableW(name, value.data(), n);
    value.resize(n);
    return value;
}

double monotonicSeconds() {
    static const double frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / frequency;
}

// MARK: - Time and locale

ClockTime localClockTime() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    ClockTime t;
    t.hour = st.wHour;
    t.minute = st.wMinute;
    t.dayOrdinal = ClockTime::dayOrdinalFor(st.wYear, st.wMonth, st.wDay);
    return t;
}

namespace {
std::wstring localeString(LCTYPE type) {
    wchar_t buffer[128] = {};
    int n = GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, type, buffer, static_cast<int>(std::size(buffer)));
    return n > 1 ? std::wstring(buffer, static_cast<size_t>(n - 1)) : std::wstring();
}
}  // namespace

LocaleInfo userLocale() {
    LocaleInfo info;
    // Skip quoted literals ('h' inside quotes is text, not a pattern letter).
    std::wstring format = localeString(LOCALE_STIMEFORMAT);
    bool quoted = false;
    for (wchar_t c : format) {
        if (c == L'\'') quoted = !quoted;
        else if (!quoted && c == L'H') info.uses24Hour = true;
    }
    std::wstring am = localeString(LOCALE_S1159), pm = localeString(LOCALE_S2359);
    if (!am.empty()) info.amSymbol = narrow(am);
    if (!pm.empty()) info.pmSymbol = narrow(pm);
    return info;
}

bool appsUseDarkTheme() {
    DWORD value = 1, size = sizeof(value);
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                  L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return status == ERROR_SUCCESS && value == 0;
}

// MARK: - Dev overrides

namespace dev {

std::optional<ClockTime> pinnedTime() {
    std::string v = narrow(environment(L"BIBLETIME_TIME"));
    int h = -1, m = -1;
    char extra = 0;
    if (v.empty() || std::sscanf(v.c_str(), "%d:%d%c", &h, &m, &extra) != 2) return std::nullopt;
    if (h < 0 || h > 23 || m < 0 || m > 59) return std::nullopt;
    ClockTime t = localClockTime();  // today, at the pinned time
    t.hour = h;
    t.minute = m;
    return t;
}

std::optional<int> pinnedDisplayIndex() {
    std::string v = narrow(environment(L"BIBLETIME_DISPLAY"));
    if (v.empty()) return std::nullopt;
    char* end = nullptr;
    long i = std::strtol(v.c_str(), &end, 10);
    if (end == v.c_str() || *end != '\0' || i < 0 || i > 64) return std::nullopt;
    return static_cast<int>(i);
}

std::optional<VerseRef> pinnedReference() {
    std::string v = narrow(environment(L"BIBLETIME_REF"));
    size_t space = v.find(' ');
    if (space == std::string::npos || v.find(' ', space + 1) != std::string::npos) return std::nullopt;
    int c = 0, n = 0;
    char extra = 0;
    if (std::sscanf(v.c_str() + space + 1, "%d:%d%c", &c, &n, &extra) != 2) return std::nullopt;
    std::string book = v.substr(0, space);
    for (char& ch : book) ch = static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
    return VerseRef(book, c, n);
}

}  // namespace dev

ClockTime now() {
    static const std::optional<ClockTime> pinned = dev::pinnedTime();
    if (pinned) {
        // Keep the pinned hour and minute but today's date (re-read so a run across midnight stays correct).
        ClockTime t = localClockTime();
        t.hour = pinned->hour;
        t.minute = pinned->minute;
        return t;
    }
    return localClockTime();
}

// MARK: - Settings

std::filesystem::path appDataDirectory() {
    PWSTR raw = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) {
        result = std::filesystem::path(raw) / L"BibleTime";
    }
    CoTaskMemFree(raw);
    return result;
}

std::filesystem::path importDirectory() {
    auto base = appDataDirectory();
    return base.empty() ? base : base / L"Translations";
}

SettingsStore::SettingsStore() {
    auto base = appDataDirectory();
    if (!base.empty()) path_ = base / L"settings.json";
    changedSinceLastCheck();  // prime the timestamp
}

Preferences SettingsStore::load() const {
    bool set = false;
    std::wstring override = environment(L"BIBLETIME_PREFS", &set);
    if (set) {
        try {
            json::Value v = json::parse(narrow(override));
            if (v.isObject()) return Preferences::fromJSON(v);
        } catch (const std::exception&) {
            // Malformed override: ignore it, like the Swift store.
        }
    }
    if (path_.empty()) return Preferences();
    auto text = bt::text::readFile(path_);
    return text ? Preferences::fromJSONText(*text) : Preferences();
}

bool SettingsStore::save(const Preferences& preferences) const {
    if (path_.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(path_.parent_path(), ec);
    std::filesystem::path temp = path_;
    temp += L".tmp";
    std::string data = preferences.normalized().toJSONText();
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
    ok = FlushFileBuffers(file) && ok;
    CloseHandle(file);
    if (!ok || !MoveFileExW(temp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

bool SettingsStore::changedSinceLastCheck() {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    bool exists = !path_.empty() && GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &data);
    FILETIME stamp = exists ? data.ftLastWriteTime : FILETIME{};
    bool changed = exists != existed_ || CompareFileTime(&stamp, &lastWrite_) != 0;
    existed_ = exists;
    lastWrite_ = stamp;
    return changed;
}

}  // namespace bt::app
