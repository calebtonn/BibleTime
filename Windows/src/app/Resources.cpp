#include "Resources.h"

#include "Win32.h"
#include "resource.h"

namespace bt::app {

namespace {
struct Entry {
    const char* name;
    int id;
};
constexpr Entry kEntries[] = {
    {"books.json", IDR_BOOKS_JSON},
    {"curated.json", IDR_CURATED_JSON},
    {"hour_verses.json", IDR_HOUR_VERSES_JSON},
    {"Translations/kjv.json", IDR_KJV_JSON},
    {"Translations/dby.json", IDR_DBY_JSON},
};
}  // namespace

std::optional<std::string_view> embeddedResource(std::string_view name) {
    for (const Entry& e : kEntries) {
        if (name != e.name) continue;
        HMODULE module = GetModuleHandleW(nullptr);
        HRSRC info = FindResourceW(module, MAKEINTRESOURCEW(e.id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
        if (!info) return std::nullopt;
        HGLOBAL handle = LoadResource(module, info);
        const void* data = handle ? LockResource(handle) : nullptr;
        if (!data) return std::nullopt;
        // Resources live in the mapped image for the life of the process; no free is needed.
        return std::string_view(static_cast<const char*>(data), SizeofResource(module, info));
    }
    return std::nullopt;
}

ResourceLoader embeddedResourceLoader() {
    return [](const std::string& name) -> std::optional<std::string> {
        auto view = embeddedResource(name);
        if (!view) return std::nullopt;
        return std::string(*view);
    };
}

}  // namespace bt::app
