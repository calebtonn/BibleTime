// BibleTime.scr entry point: parses the screen saver command line and dispatches.
//
//   /s                 full screen on every monitor
//   /p <hwnd>          preview inside the Screen Saver Settings thumbnail
//   /c[:hwnd] or none  Options dialog
//   /setactive         make BibleTime the current user's screen saver (installer)
//   /unsetactive       undo /setactive if BibleTime is still the chosen saver (uninstaller)
//   /render <out.png> [--width W] [--height H] [--preview] [--scale S]   render one frame offscreen (CI)
//   /render-options <out.png>                                           capture the Options dialog (CI)
//
// Switches are case-insensitive, start with / or -, and take their value either after ':' or as the next argument.
#include <objbase.h>
#include <shellapi.h>

#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <optional>
#include <string>
#include <vector>

#include "Install.h"
#include "OptionsDialog.h"
#include "Render.h"
#include "Saver.h"
#include "Win32.h"

using namespace bt::app;

namespace {

std::wstring lower(std::wstring s) {
    for (wchar_t& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

std::optional<unsigned long long> parseNumber(const std::wstring& s) {
    if (s.empty()) return std::nullopt;
    wchar_t* end = nullptr;
    unsigned long long v = std::wcstoull(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != L'\0') return std::nullopt;
    return v;
}

HWND toHwnd(unsigned long long v) { return reinterpret_cast<HWND>(static_cast<uintptr_t>(v)); }

/// Write console output when launched from a terminal (the .scr is a GUI app, so attach to the parent console).
void attachConsole() {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stderr);
        freopen_s(&f, "CONOUT$", "w", stdout);
    }
}

int render(const std::vector<std::wstring>& args, size_t i, const std::wstring& inlineValue) {
    attachConsole();
    RenderOptions o;
    o.output = !inlineValue.empty() ? inlineValue : (i < args.size() ? args[i++] : L"");
    for (; i < args.size(); ++i) {
        std::wstring a = lower(args[i]);
        auto next = [&]() -> double { return i + 1 < args.size() ? std::wcstod(args[++i].c_str(), nullptr) : 0; };
        if (a == L"--width") o.width = next();
        else if (a == L"--height") o.height = next();
        else if (a == L"--scale") o.scale = next();
        else if (a == L"--preview") o.preview = true;
    }
    if (o.output.empty() || o.width < 1 || o.height < 1 || o.scale <= 0 || o.scale > 8) {
        std::fprintf(stderr, "usage: BibleTime.scr /render <out.png> [--width W] [--height H] [--preview] [--scale S]\n");
        return 2;
    }
    return renderFrame(o);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    for (int k = 1; k < argc; ++k) args.emplace_back(argv[k]);
    LocalFree(argv);

    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    std::wstring command;
    std::wstring inlineValue;
    size_t next = 0;
    if (!args.empty() && (args[0][0] == L'/' || args[0][0] == L'-')) {
        command = lower(args[0].substr(1));
        size_t colon = command.find(L':');
        if (colon != std::wstring::npos) {
            inlineValue = args[0].substr(colon + 2);  // original case, after "/x:"
            command.resize(colon);
        }
        next = 1;
    }
    // The value after a switch: "/p:123" or "/p 123".
    auto value = [&]() -> std::wstring {
        if (!inlineValue.empty()) return inlineValue;
        return next < args.size() ? args[next] : std::wstring();
    };

    int code = 0;
    if (command == L"s") {
        code = runFullScreen();
    } else if (command == L"p" || command == L"l") {
        auto hwnd = parseNumber(value());
        code = hwnd ? runPreview(toHwnd(*hwnd)) : 2;
    } else if (command == L"setactive") {
        code = setActive();
    } else if (command == L"unsetactive") {
        code = unsetActive();
    } else if (command == L"render") {
        code = render(args, next, inlineValue);
    } else if (command == L"render-options") {
        attachConsole();
        std::wstring out = value();
        code = out.empty() ? 2 : renderOptionsDialog(out);
    } else if (command == L"a") {
        code = 0;  // Windows 9x password change: not applicable
    } else {
        // "/c", "/c:hwnd", "/c hwnd", no arguments (right-click > Configure), or anything unknown: the Options dialog.
        auto owner = command == L"c" ? parseNumber(value()) : std::nullopt;
        code = runOptions(owner ? toHwnd(*owner) : nullptr);
    }

    if (SUCCEEDED(com)) CoUninitialize();
    return code;
}
