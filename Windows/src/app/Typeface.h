// DirectWrite font selection, the Windows counterpart of the Typeface enum in Sources/BibleTime/Theme.swift.
//
// Stored preference values stay newYork/charter/palatino/georgia/sans; the Windows menu titles are:
//   newYork  -> "Sitka"             (Sitka Text, Sitka, Cambria, Georgia)
//   charter  -> "Cambria"
//   palatino -> "Palatino Linotype"
//   georgia  -> "Georgia"
//   sans     -> "Segoe UI"
// A family that is not installed falls back to the serif default (the Sitka chain), then to whatever the system has.
#pragma once

#include <dwrite_1.h>

#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>

#include "Core.h"
#include "Win32.h"

namespace bt::app {

/// Menu title for the Options dialog.
const wchar_t* typefaceTitle(TypefaceChoice choice);

struct FontFace {
    std::wstring family;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_REGULAR;
    /// Apply OpenType 'tnum'.
    bool tabular = false;
    bool operator<(const FontFace& o) const {
        return std::tie(family, weight, tabular) < std::tie(o.family, o.weight, o.tabular);
    }
};

/// Vertical font metrics in em units (multiply by the font size).
struct LineMetrics {
    double ascent = 0.9;
    double descent = 0.25;
    double lineGap = 0;
    /// ascender - descender + leading, like NSFont's line height with usesFontLeading.
    double lineHeight() const { return ascent + descent + lineGap; }
};

/// Resolves families against the system font collection and caches the results. Thread-safe.
class Typography {
public:
    /// The process-wide instance (creates a shared IDWriteFactory1).
    static Typography& shared();

    IDWriteFactory1* factory() const { return factory_.Get(); }

    FontFace body(TypefaceChoice choice);
    /// Medium/semibold weight of the body family with tabular figures; falls back to the serif default, then Segoe UI,
    /// when the family's digits are not uniform width even with 'tnum'.
    FontFace reference(TypefaceChoice choice);
    /// Segoe UI with tabular figures.
    FontFace clock();
    LineMetrics lineMetrics(const FontFace& face);

    /// A text format for `face` at `size` DIPs (cached per face and size).
    ComPtr<IDWriteTextFormat> format(const FontFace& face, float size);
    /// Typography carrying 'tnum' (shared).
    IDWriteTypography* tabularTypography();

private:
    Typography();
    bool hasFamily(const std::wstring& family);
    /// First installed family from `candidates`, else the serif default, else any installed family.
    std::wstring firstInstalled(std::initializer_list<const wchar_t*> candidates);
    std::wstring serifDefault();
    std::wstring lastResort();
    bool hasUniformDigits(const FontFace& face);

    ComPtr<IDWriteFactory1> factory_;
    ComPtr<IDWriteFontCollection> collection_;
    ComPtr<IDWriteTypography> tabular_;
    std::recursive_mutex mutex_;
    std::map<std::wstring, bool> families_;
    std::map<FontFace, bool> uniformDigits_;
    std::map<FontFace, LineMetrics> lineMetrics_;
    std::map<std::pair<FontFace, float>, ComPtr<IDWriteTextFormat>> formats_;
};

}  // namespace bt::app
