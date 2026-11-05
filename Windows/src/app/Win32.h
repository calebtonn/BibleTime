// Common Windows includes and small helpers for the BibleTime screen saver app.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <wrl/client.h>

#include <string>
#include <string_view>

namespace bt::app {

using Microsoft::WRL::ComPtr;

/// UTF-8 -> UTF-16.
std::wstring widen(std::string_view utf8);
/// UTF-16 -> UTF-8.
std::string narrow(std::wstring_view wide);

/// Full path of this module (BibleTime.scr).
std::wstring modulePath();

/// Value of an environment variable, or empty when unset. `set` reports whether it exists at all.
std::wstring environment(const wchar_t* name, bool* set = nullptr);

/// Seconds from a monotonic high-resolution clock (QueryPerformanceCounter).
double monotonicSeconds();

}  // namespace bt::app
