#include "Typeface.h"

#include <algorithm>
#include <stdexcept>

namespace bt::app {

const wchar_t* typefaceTitle(TypefaceChoice choice) {
    switch (choice) {
    case TypefaceChoice::NewYork: return L"Sitka";
    case TypefaceChoice::Charter: return L"Cambria";
    case TypefaceChoice::Palatino: return L"Palatino Linotype";
    case TypefaceChoice::Georgia: return L"Georgia";
    case TypefaceChoice::Sans: return L"Segoe UI";
    }
    return L"Sitka";
}

Typography& Typography::shared() {
    static Typography instance;
    return instance;
}

Typography::Typography() {
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory1),
                                     reinterpret_cast<IUnknown**>(factory_.GetAddressOf()));
    if (FAILED(hr)) throw std::runtime_error("DirectWrite is not available");
    factory_->GetSystemFontCollection(collection_.GetAddressOf(), FALSE);
    if (SUCCEEDED(factory_->CreateTypography(tabular_.GetAddressOf()))) {
        tabular_->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1});
    }
}

bool Typography::hasFamily(const std::wstring& family) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = families_.find(family);
    if (it != families_.end()) return it->second;
    UINT32 index = 0;
    BOOL exists = FALSE;
    bool found = collection_ && SUCCEEDED(collection_->FindFamilyName(family.c_str(), &index, &exists)) && exists;
    families_[family] = found;
    return found;
}

std::wstring Typography::lastResort() {
    for (const wchar_t* name : {L"Segoe UI", L"Tahoma", L"Arial"}) {
        if (hasFamily(name)) return name;
    }
    // Anything the system has: the first family's first localized name.
    ComPtr<IDWriteFontFamily> family;
    ComPtr<IDWriteLocalizedStrings> names;
    if (collection_ && collection_->GetFontFamilyCount() > 0 && SUCCEEDED(collection_->GetFontFamily(0, &family)) &&
        SUCCEEDED(family->GetFamilyNames(&names))) {
        UINT32 length = 0;
        if (SUCCEEDED(names->GetStringLength(0, &length))) {
            std::wstring name(length + 1, L'\0');
            if (SUCCEEDED(names->GetString(0, name.data(), length + 1))) {
                name.resize(length);
                return name;
            }
        }
    }
    return L"Segoe UI";
}

std::wstring Typography::serifDefault() {
    for (const wchar_t* name : {L"Sitka Text", L"Sitka", L"Cambria", L"Georgia"}) {
        if (hasFamily(name)) return name;
    }
    return lastResort();
}

std::wstring Typography::firstInstalled(std::initializer_list<const wchar_t*> candidates) {
    for (const wchar_t* name : candidates) {
        if (hasFamily(name)) return name;
    }
    return serifDefault();
}

FontFace Typography::body(TypefaceChoice choice) {
    FontFace f;
    switch (choice) {
    case TypefaceChoice::NewYork: f.family = serifDefault(); break;
    case TypefaceChoice::Charter: f.family = firstInstalled({L"Cambria"}); break;
    case TypefaceChoice::Palatino: f.family = firstInstalled({L"Palatino Linotype", L"Palatino"}); break;
    case TypefaceChoice::Georgia: f.family = firstInstalled({L"Georgia"}); break;
    case TypefaceChoice::Sans: f.family = hasFamily(L"Segoe UI") ? std::wstring(L"Segoe UI") : lastResort(); break;
    }
    return f;
}

FontFace Typography::reference(TypefaceChoice choice) {
    // Semibold resolves to the family's bold where no semibold face exists (Georgia, Palatino Linotype, Cambria,
    // Sitka), like Charter-Bold / Georgia-Bold on macOS; Segoe UI has a true Semibold.
    FontFace candidate = body(choice);
    candidate.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    candidate.tabular = true;
    if (hasUniformDigits(candidate)) return candidate;
    FontFace serif{serifDefault(), DWRITE_FONT_WEIGHT_SEMI_BOLD, true};
    if (hasUniformDigits(serif)) return serif;
    std::wstring sans = hasFamily(L"Segoe UI") ? std::wstring(L"Segoe UI") : lastResort();
    return FontFace{sans, DWRITE_FONT_WEIGHT_SEMI_BOLD, true};
}

FontFace Typography::clock() {
    return FontFace{hasFamily(L"Segoe UI") ? std::wstring(L"Segoe UI") : lastResort(), DWRITE_FONT_WEIGHT_REGULAR, true};
}

IDWriteTypography* Typography::tabularTypography() { return tabular_.Get(); }

ComPtr<IDWriteTextFormat> Typography::format(const FontFace& face, float size) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto key = std::make_pair(face, size);
    auto it = formats_.find(key);
    if (it != formats_.end()) return it->second;
    if (formats_.size() > 256) formats_.clear();  // sizes come from a 0.5 pt search, so this rarely triggers
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(factory_->CreateTextFormat(face.family.c_str(), collection_.Get(), face.weight, DWRITE_FONT_STYLE_NORMAL,
                                          DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &format))) {
        return nullptr;
    }
    formats_[key] = format;
    return format;
}

bool Typography::hasUniformDigits(const FontFace& face) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = uniformDigits_.find(face);
    if (it != uniformDigits_.end()) return it->second;
    // Measure each digit at 100 DIPs with the face's features; uniform means within 0.2% of an em
    // (macOS allows 0.05 pt at reference sizes of ~12-20 pt).
    constexpr float size = 100;
    auto format = this->format(face, size);
    float lo = 1e9f, hi = -1e9f;
    bool ok = format != nullptr;
    for (wchar_t digit = L'0'; ok && digit <= L'9'; ++digit) {
        ComPtr<IDWriteTextLayout> layout;
        DWRITE_TEXT_METRICS m{};
        ok = SUCCEEDED(factory_->CreateTextLayout(&digit, 1, format.Get(), 1000, 1000, &layout));
        if (ok && face.tabular && tabular_) layout->SetTypography(tabular_.Get(), {0, 1});
        ok = ok && SUCCEEDED(layout->GetMetrics(&m));
        if (ok) {
            lo = std::min(lo, m.widthIncludingTrailingWhitespace);
            hi = std::max(hi, m.widthIncludingTrailingWhitespace);
        }
    }
    bool uniform = ok && hi - lo < 0.2f;
    uniformDigits_[face] = uniform;
    return uniform;
}

LineMetrics Typography::lineMetrics(const FontFace& face) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = lineMetrics_.find(face);
    if (it != lineMetrics_.end()) return it->second;
    LineMetrics result;
    UINT32 index = 0;
    BOOL exists = FALSE;
    ComPtr<IDWriteFontFamily> family;
    ComPtr<IDWriteFont> font;
    if (collection_ && SUCCEEDED(collection_->FindFamilyName(face.family.c_str(), &index, &exists)) && exists &&
        SUCCEEDED(collection_->GetFontFamily(index, &family)) &&
        SUCCEEDED(family->GetFirstMatchingFont(face.weight, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font))) {
        DWRITE_FONT_METRICS m{};
        font->GetMetrics(&m);
        if (m.designUnitsPerEm > 0) {
            double em = m.designUnitsPerEm;
            result.ascent = m.ascent / em;
            result.descent = m.descent / em;
            result.lineGap = m.lineGap / em;
        }
    }
    lineMetrics_[face] = result;
    return result;
}

}  // namespace bt::app
