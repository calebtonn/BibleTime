// Bundled data embedded in BibleTime.scr as RCDATA (see Windows/res/BibleTime.rc.in).
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "Core.h"

namespace bt::app {

/// The bytes of an embedded resource ("books.json", "Translations/kjv.json", ...), pointing straight into the
/// mapped image (no copy). nullopt when the name is unknown or the resource is missing.
std::optional<std::string_view> embeddedResource(std::string_view name);

/// A core ResourceLoader backed by embeddedResource (the core API takes ownership, so this copies once at load).
ResourceLoader embeddedResourceLoader();

}  // namespace bt::app
