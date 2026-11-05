// The Options dialog (/c), mirroring Sources/BibleTime/ConfigureSheet.swift.
#pragma once

#include "Win32.h"

namespace bt::app {

/// Modal Options dialog owned by `owner` (may be null). Saves settings.json on OK. Returns 0.
int runOptions(HWND owner);

/// Creates the dialog modeless and hidden (for /render-options). Destroy it with DestroyWindow.
HWND createOptionsDialogForCapture();

}  // namespace bt::app
