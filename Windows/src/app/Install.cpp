#include "Install.h"

#include <string>

#include "Win32.h"

namespace bt::app {

namespace {

constexpr wchar_t kDesktopKey[] = L"Control Panel\\Desktop";

bool setString(HKEY key, const wchar_t* name, const std::wstring& value) {
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

std::wstring readString(HKEY key, const wchar_t* name, bool* exists) {
    DWORD size = 0;
    *exists = RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, nullptr, &size) == ERROR_SUCCESS;
    if (!*exists || size < sizeof(wchar_t)) return {};
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, value.data(), &size) != ERROR_SUCCESS) {
        return {};
    }
    value.resize(wcsnlen(value.c_str(), value.size()));
    return value;
}

/// Calls a Win32 "fill this buffer" path function into a heap buffer (paths can be up to 32767 characters).
template <class F> std::wstring pathCall(const std::wstring& in, F f) {
    std::wstring out(32768, L'\0');
    DWORD n = f(in.c_str(), out.data(), static_cast<DWORD>(out.size()));
    if (n == 0 || n >= out.size()) return in;
    out.resize(n);
    return out;
}

/// Canonical form for comparing paths: environment variables expanded, absolute, long names, no quotes.
std::wstring canonicalPath(std::wstring path) {
    if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"') path = path.substr(1, path.size() - 2);
    path = pathCall(path, [](const wchar_t* i, wchar_t* o, DWORD n) {
        DWORD r = ExpandEnvironmentStringsW(i, o, n);
        return r > 0 ? r - 1 : 0;  // count includes the terminator
    });
    path = pathCall(path, [](const wchar_t* i, wchar_t* o, DWORD n) { return GetFullPathNameW(i, n, o, nullptr); });
    path = pathCall(path, [](const wchar_t* i, wchar_t* o, DWORD n) { return GetLongPathNameW(i, o, n); });
    return path;
}

bool samePath(const std::wstring& a, const std::wstring& b) {
    std::wstring ca = canonicalPath(a), cb = canonicalPath(b);
    if (CompareStringOrdinal(ca.c_str(), -1, cb.c_str(), -1, TRUE) == CSTR_EQUAL) return true;
    // Short (8.3) forms of both, for when long-name expansion did not apply.
    auto shortOf = [](const std::wstring& p) {
        return pathCall(p, [](const wchar_t* i, wchar_t* o, DWORD n) { return GetShortPathNameW(i, o, n); });
    };
    std::wstring sa = shortOf(ca), sb = shortOf(cb);
    return CompareStringOrdinal(sa.c_str(), -1, sb.c_str(), -1, TRUE) == CSTR_EQUAL;
}

}  // namespace

int setActive() {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kDesktopKey, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
        return 1;
    }
    bool ok = setString(key, L"SCRNSAVE.EXE", modulePath());
    ok = setString(key, L"ScreenSaveActive", L"1") && ok;
    // Only seed a timeout when none is set at all. Whatever type an existing value has (REG_SZ as Windows writes it,
    // or a REG_DWORD from a policy script), it is the user's choice and stays as it is.
    LSTATUS timeout = RegQueryValueExW(key, L"ScreenSaveTimeOut", nullptr, nullptr, nullptr, nullptr);
    if (timeout == ERROR_FILE_NOT_FOUND) ok = setString(key, L"ScreenSaveTimeOut", L"600") && ok;
    RegCloseKey(key);
    // Apply to the running session and broadcast WM_SETTINGCHANGE so an open Screen Saver Settings page updates.
    SystemParametersInfoW(SPI_SETSCREENSAVEACTIVE, TRUE, nullptr, SPIF_SENDCHANGE);
    return ok ? 0 : 1;
}

int unsetActive() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kDesktopKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS) return 0;
    bool exists = false;
    std::wstring current = readString(key, L"SCRNSAVE.EXE", &exists);
    bool removed = false;
    if (exists && !current.empty() && samePath(current, modulePath())) {
        removed = RegDeleteValueW(key, L"SCRNSAVE.EXE") == ERROR_SUCCESS;
    }
    RegCloseKey(key);
    if (removed) {
        DWORD_PTR result = 0;
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETSCREENSAVEACTIVE,
                            reinterpret_cast<LPARAM>(L"Control Panel\\Desktop"), SMTO_ABORTIFHUNG, 2000, &result);
    }
    return 0;
}

}  // namespace bt::app
