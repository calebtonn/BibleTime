#include "OptionsDialog.h"

#include <commctrl.h>
#include <shobjidl.h>

#include <cmath>
#include <memory>
#include <vector>

#include "Core.h"
#include "Platform.h"
#include "Resources.h"
#include "Typeface.h"
#include "resource.h"

namespace bt::app {

namespace {

constexpr UINT kMsgRefreshFonts = WM_APP + 10;
constexpr LRESULT kSeparatorItem = -1;
constexpr LRESULT kEmptyItem = -2;

// MARK: - Message boxes (TaskDialog when Common Controls v6 is active, MessageBox otherwise)

using TaskDialogIndirectFn = HRESULT(WINAPI*)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);

TaskDialogIndirectFn taskDialogIndirect() {
    // Resolved at runtime: the export exists only in comctl32 v6, and a missing import would stop the .scr loading.
    static TaskDialogIndirectFn fn = [] {
        HMODULE comctl = GetModuleHandleW(L"comctl32.dll");
        if (!comctl) comctl = LoadLibraryW(L"comctl32.dll");
        return comctl ? reinterpret_cast<TaskDialogIndirectFn>(reinterpret_cast<void*>(GetProcAddress(comctl, "TaskDialogIndirect")))
                      : nullptr;
    }();
    return fn;
}

void showError(HWND owner, const std::wstring& message, const std::wstring& detail) {
    if (auto fn = taskDialogIndirect()) {
        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.hwndParent = owner;
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        config.pszWindowTitle = L"BibleTime";
        config.pszMainIcon = TD_WARNING_ICON;
        config.pszMainInstruction = message.c_str();
        config.pszContent = detail.c_str();
        config.dwCommonButtons = TDCBF_OK_BUTTON;
        if (SUCCEEDED(fn(&config, nullptr, nullptr, nullptr))) return;
    }
    std::wstring text = message + L"\n\n" + detail;
    MessageBoxW(owner, text.c_str(), L"BibleTime", MB_OK | MB_ICONWARNING);
}

bool confirmRemoval(HWND owner, const std::wstring& message, const std::wstring& detail) {
    if (auto fn = taskDialogIndirect()) {
        TASKDIALOG_BUTTON buttons[] = {{IDOK, L"Remove"}, {IDCANCEL, L"Cancel"}};
        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.hwndParent = owner;
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        config.pszWindowTitle = L"BibleTime";
        config.pszMainInstruction = message.c_str();
        config.pszContent = detail.c_str();
        config.cButtons = 2;
        config.pButtons = buttons;
        config.nDefaultButton = IDCANCEL;
        int pressed = 0;
        if (SUCCEEDED(fn(&config, &pressed, nullptr, nullptr))) return pressed == IDOK;
    }
    std::wstring text = message + L"\n\n" + detail;
    return MessageBoxW(owner, text.c_str(), L"BibleTime", MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2) == IDOK;
}

/// Like macOS secondaryLabelColor: the dialog text color, 40% of the way toward the background (plain text in high contrast).
COLORREF secondaryTextColor() {
    HIGHCONTRASTW hc{};
    hc.cbSize = sizeof(hc);
    COLORREF text = GetSysColor(COLOR_BTNTEXT);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON)) return text;
    COLORREF face = GetSysColor(COLOR_BTNFACE);
    auto mix = [](int a, int b) { return static_cast<BYTE>((a * 60 + b * 40) / 100); };
    return RGB(mix(GetRValue(text), GetRValue(face)), mix(GetGValue(text), GetGValue(face)), mix(GetBValue(text), GetBValue(face)));
}

// MARK: - Dialog

class OptionsDialog {
public:
    OptionsDialog() {
        try {
            std::vector<std::filesystem::path> imports;
            if (auto dir = importDirectory(); !dir.empty()) imports.push_back(dir);
            clock_ = std::make_unique<VerseClock>(embeddedResourceLoader(), std::move(imports));
        } catch (const std::exception&) {
            clock_.reset();  // the dialog still works; the translation list is just empty
        }
        preferences_ = store_.load();
    }
    ~OptionsDialog() {
        if (titleFont_) DeleteObject(titleFont_);
    }

    static INT_PTR CALLBACK proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_INITDIALOG) {
            SetWindowLongPtrW(hwnd, DWLP_USER, lParam);
            auto* self = reinterpret_cast<OptionsDialog*>(lParam);
            self->hwnd_ = hwnd;
            self->initialize();
            return TRUE;
        }
        auto* self = reinterpret_cast<OptionsDialog*>(GetWindowLongPtrW(hwnd, DWLP_USER));
        return self ? self->handle(msg, wParam, lParam) : FALSE;
    }

    bool modeless = false;

private:
    HWND item(int id) const { return GetDlgItem(hwnd_, id); }

    void initialize() {
        SetDlgItemTextW(hwnd_, IDC_SUBTITLE,
                        L"The chapter is the hour and the verse is the minute. At 3:16 you’ll see John 3:16.");
        SetDlgItemTextW(hwnd_, IDC_IMPORT, L"&Import…");
        refreshFonts();

        auto fill = [&](int id, auto titles) {
            HWND combo = item(id);
            SendMessageW(combo, CB_RESETCONTENT, 0, 0);
            for (const std::wstring& t : titles) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t.c_str()));
        };
        auto titlesOf = [](auto cases) {
            std::vector<std::wstring> v;
            for (auto e : cases) v.push_back(widen(title(e)));
            return v;
        };
        fill(IDC_HOURS, titlesOf(allCases<HourStyle>()));
        fill(IDC_THEME, titlesOf(allCases<ThemeChoice>()));
        fill(IDC_LAYOUT, titlesOf(allCases<LayoutStyle>()));
        fill(IDC_DISPLAYS, titlesOf(allCases<DisplayMode>()));
        std::vector<std::wstring> faces;
        for (auto e : allCases<TypefaceChoice>()) faces.push_back(typefaceTitle(e));
        fill(IDC_TYPEFACE, faces);

        HWND slider = item(IDC_TEXTSIZE);
        SendMessageW(slider, TBM_SETRANGE, TRUE,
                     MAKELPARAM(static_cast<int>(Preferences::textScaleMin * 100), static_cast<int>(Preferences::textScaleMax * 100)));
        SendMessageW(slider, TBM_SETTICFREQ, 25, 0);
        SendMessageW(slider, TBM_SETLINESIZE, 0, 5);
        SendMessageW(slider, TBM_SETPAGESIZE, 0, 25);

        apply(preferences_);
    }

    void refreshFonts() {
        // Title: Segoe UI Semibold, about 1.5x the dialog font, recreated for the window's current DPI.
        if (titleFont_) DeleteObject(titleFont_);
        UINT dpi = GetDpiForWindow(hwnd_);
        LOGFONTW lf{};
        lf.lfHeight = -MulDiv(14, static_cast<int>(dpi ? dpi : 96), 72);
        lf.lfWeight = FW_SEMIBOLD;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        titleFont_ = CreateFontIndirectW(&lf);
        SendMessageW(item(IDC_TITLE), WM_SETFONT, reinterpret_cast<WPARAM>(titleFont_), TRUE);
    }

    void reloadTranslations(const std::string& selectID) {
        translations_.clear();
        if (clock_) translations_ = clock_->catalog().available();
        HWND combo = item(IDC_TRANSLATION);
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        auto add = [&](const std::wstring& text, LRESULT data) {
            LRESULT i = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
            SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(i), static_cast<LPARAM>(data));
        };
        if (translations_.empty()) add(L"No translations found", kEmptyItem);
        bool separatorAdded = false;
        for (size_t i = 0; i < translations_.size(); ++i) {
            if (translations_[i].isImported && !separatorAdded && i > 0) {
                add(L"────────────", kSeparatorItem);
                separatorAdded = true;
            }
            add(widen(translations_[i].displayName()), static_cast<LRESULT>(i));
        }
        int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
        int select = -1, firstReal = -1;
        for (int i = 0; i < count; ++i) {
            LRESULT data = SendMessageW(combo, CB_GETITEMDATA, i, 0);
            if (data < 0) continue;
            if (firstReal < 0) firstReal = i;
            if (translations_[static_cast<size_t>(data)].id == selectID) select = i;
        }
        int chosen = select >= 0 ? select : (firstReal >= 0 ? firstReal : 0);
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(chosen), 0);
        // CB_SETCURSEL sends no CBN_SELCHANGE: record the new selection so the separator skip knows where travel starts.
        lastTranslationSel_ = chosen;
        updateRemoveButton();
    }

    const TranslationInfo* selectedTranslation() const {
        HWND combo = item(IDC_TRANSLATION);
        LRESULT sel = SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (sel == CB_ERR) return nullptr;
        LRESULT data = SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(sel), 0);
        if (data < 0 || static_cast<size_t>(data) >= translations_.size()) return nullptr;
        return &translations_[static_cast<size_t>(data)];
    }

    void updateRemoveButton() {
        const TranslationInfo* info = selectedTranslation();
        EnableWindow(item(IDC_REMOVE), info && info->isImported);
    }

    template <class E> static int indexOf(E value) {
        const auto& cases = allCases<E>();
        for (size_t i = 0; i < cases.size(); ++i) {
            if (cases[i] == value) return static_cast<int>(i);
        }
        return 0;
    }
    template <class E> E selected(int id) const {
        const auto& cases = allCases<E>();
        LRESULT i = SendMessageW(item(id), CB_GETCURSEL, 0, 0);
        return cases[i >= 0 && static_cast<size_t>(i) < cases.size() ? static_cast<size_t>(i) : 0];
    }

    void apply(const Preferences& p) {
        reloadTranslations(p.translation);
        SendMessageW(item(IDC_HOURS), CB_SETCURSEL, indexOf(p.hourStyle), 0);
        SendMessageW(item(IDC_THEME), CB_SETCURSEL, indexOf(p.theme), 0);
        SendMessageW(item(IDC_LAYOUT), CB_SETCURSEL, indexOf(p.layout), 0);
        SendMessageW(item(IDC_TYPEFACE), CB_SETCURSEL, indexOf(p.typeface), 0);
        SendMessageW(item(IDC_DISPLAYS), CB_SETCURSEL, indexOf(p.displayMode), 0);
        SendMessageW(item(IDC_TEXTSIZE), TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(p.textScale * 100)));
        CheckDlgButton(hwnd_, IDC_SHOWCLOCK, p.showClock ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd_, IDC_FAMOUS, p.preferFamous ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd_, IDC_EXTEND, p.extendFragments ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd_, IDC_PAD, p.padReference ? BST_CHECKED : BST_UNCHECKED);
        updateSizeLabel();
    }

    Preferences read() const {
        Preferences p;
        if (const TranslationInfo* info = selectedTranslation()) p.translation = info->id;
        else p.translation = preferences_.translation;
        p.hourStyle = selected<HourStyle>(IDC_HOURS);
        p.theme = selected<ThemeChoice>(IDC_THEME);
        p.layout = selected<LayoutStyle>(IDC_LAYOUT);
        p.typeface = selected<TypefaceChoice>(IDC_TYPEFACE);
        p.displayMode = selected<DisplayMode>(IDC_DISPLAYS);
        p.textScale = static_cast<double>(SendMessageW(item(IDC_TEXTSIZE), TBM_GETPOS, 0, 0)) / 100.0;
        p.showClock = IsDlgButtonChecked(hwnd_, IDC_SHOWCLOCK) == BST_CHECKED;
        p.preferFamous = IsDlgButtonChecked(hwnd_, IDC_FAMOUS) == BST_CHECKED;
        p.extendFragments = IsDlgButtonChecked(hwnd_, IDC_EXTEND) == BST_CHECKED;
        p.padReference = IsDlgButtonChecked(hwnd_, IDC_PAD) == BST_CHECKED;
        return p.normalized();
    }

    void updateSizeLabel() {
        LRESULT pos = SendMessageW(item(IDC_TEXTSIZE), TBM_GETPOS, 0, 0);
        std::wstring text = std::to_wstring(pos) + L"%";
        SetDlgItemTextW(hwnd_, IDC_TEXTSIZE_LABEL, text.c_str());
    }

    void onTranslationChanged() {
        // The separator is not selectable: step past it in the direction of travel (keyboard Up/Down, the mouse wheel,
        // arrows in the open list, type-to-find), measured from the last real selection, never from a separator.
        HWND combo = item(IDC_TRANSLATION);
        LRESULT sel = SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (sel != CB_ERR && SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(sel), 0) == kSeparatorItem) {
            const LRESULT count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
            const bool movingUp = lastTranslationSel_ != CB_ERR && lastTranslationSel_ > sel;
            LRESULT next = movingUp ? sel - 1 : sel + 1;
            if (next < 0 || next >= count) next = movingUp ? sel + 1 : sel - 1;  // nothing that way: the other side
            if (next >= 0 && next < count) {
                SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(next), 0);
                sel = next;
            }
        }
        if (sel != CB_ERR && SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(sel), 0) != kSeparatorItem) {
            lastTranslationSel_ = sel;
        }
        updateRemoveButton();
    }

    void importTranslation() {
        if (!clock_) return;
        auto importer = clock_->importer();
        if (!importer) {
            showError(hwnd_, L"Could not import a translation", L"The folder for imported translations is not available.");
            return;
        }
        ComPtr<IFileOpenDialog> dialog;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return;
        COMDLG_FILTERSPEC filters[] = {{L"Translation files (*.json)", L"*.json"}};
        dialog->SetFileTypes(1, filters);
        dialog->SetTitle(L"Import a Translation");
        dialog->SetOkButtonLabel(L"Import");
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
        if (dialog->Show(hwnd_) != S_OK) return;
        ComPtr<IShellItem> result;
        PWSTR path = nullptr;
        if (FAILED(dialog->GetResult(&result)) || FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &path))) return;
        std::filesystem::path source(path);
        CoTaskMemFree(path);
        try {
            TranslationInfo info = importer->importFile(source);
            clock_->catalog().rescan();
            reloadTranslations(info.id);
        } catch (const std::exception& e) {
            showError(hwnd_, L"Could not import " + source.filename().wstring(), widen(e.what()));
        }
    }

    void removeTranslation() {
        const TranslationInfo* chosen = selectedTranslation();
        if (!clock_ || !chosen || !chosen->isImported) return;
        TranslationInfo info = *chosen;
        std::wstring name = widen(info.name);
        if (!confirmRemoval(hwnd_, L"Remove " + name + L"?",
                            L"The imported file will be deleted. The bundled translations are not affected.")) {
            return;
        }
        try {
            if (auto importer = clock_->importer()) importer->remove(info);
            clock_->catalog().rescan();
            reloadTranslations(Preferences().translation);
        } catch (const std::exception& e) {
            showError(hwnd_, L"Could not remove " + name, widen(e.what()));
        }
    }

    void finish(int result) {
        if (modeless) DestroyWindow(hwnd_);
        else EndDialog(hwnd_, result);
    }

    INT_PTR handle(UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_COMMAND: {
            int id = LOWORD(wParam), code = HIWORD(wParam);
            switch (id) {
            case IDOK:
                preferences_ = read();
                if (!store_.save(preferences_)) {
                    showError(hwnd_, L"Could not save your settings",
                              L"BibleTime could not write " + store_.path().wstring() + L".");
                    return TRUE;
                }
                finish(IDOK);
                return TRUE;
            case IDCANCEL: finish(IDCANCEL); return TRUE;
            case IDC_DEFAULTS: apply(Preferences()); return TRUE;
            case IDC_IMPORT: importTranslation(); return TRUE;
            case IDC_REMOVE: removeTranslation(); return TRUE;
            case IDC_TRANSLATION:
                // Closing the list (Escape restores the old selection without a CBN_SELCHANGE) re-syncs too, so the
                // recorded selection and the Remove button never describe an item that is no longer selected.
                if (code == CBN_SELCHANGE || code == CBN_CLOSEUP || code == CBN_SELENDCANCEL) onTranslationChanged();
                return TRUE;
            default: return FALSE;
            }
        }
        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lParam) == item(IDC_TEXTSIZE)) updateSizeLabel();
            return TRUE;
        case WM_CTLCOLORSTATIC: {
            // Secondary text (subtitle, size readout) in the system's gray, like secondaryLabelColor.
            HWND control = reinterpret_cast<HWND>(lParam);
            if (control == item(IDC_SUBTITLE) || control == item(IDC_TEXTSIZE_LABEL)) {
                HDC dc = reinterpret_cast<HDC>(wParam);
                SetTextColor(dc, secondaryTextColor());
                SetBkMode(dc, TRANSPARENT);
                return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
            }
            return FALSE;
        }
        case WM_DPICHANGED:
            // Windows rescales Per-Monitor V2 dialogs itself (and resets control fonts); restore the title font after.
            PostMessageW(hwnd_, kMsgRefreshFonts, 0, 0);
            return FALSE;
        case kMsgRefreshFonts: refreshFonts(); return TRUE;
        default: return FALSE;
        }
    }

    HWND hwnd_ = nullptr;
    std::unique_ptr<VerseClock> clock_;
    SettingsStore store_;
    Preferences preferences_;
    std::vector<TranslationInfo> translations_;
    HFONT titleFont_ = nullptr;
    LRESULT lastTranslationSel_ = -1;
};

void initCommonControls() {
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);
}

}  // namespace

int runOptions(HWND owner) {
    initCommonControls();
    OptionsDialog dialog;
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_OPTIONS), IsWindow(owner) ? owner : nullptr,
                    OptionsDialog::proc, reinterpret_cast<LPARAM>(&dialog));
    return 0;
}

HWND createOptionsDialogForCapture() {
    initCommonControls();
    // Lives as long as the process (the capture tool exits right after).
    static OptionsDialog dialog;
    dialog.modeless = true;
    return CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_OPTIONS), nullptr, OptionsDialog::proc,
                              reinterpret_cast<LPARAM>(&dialog));
}

}  // namespace bt::app
