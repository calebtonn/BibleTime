// Developer / CI tools: render one frame or the Options dialog to PNG without a visible window.
#pragma once

#include <string>

namespace bt::app {

struct RenderOptions {
    std::wstring output;
    double width = 1440;   // DIPs
    double height = 900;   // DIPs
    double scale = 1;      // device pixels per DIP
    bool preview = false;
};

/// /render: draws one frame (honouring BIBLETIME_TIME, BIBLETIME_PREFS, BIBLETIME_REF, BIBLETIME_DISPLAY). Returns 0 on success.
int renderFrame(const RenderOptions& options);

/// /render-options: captures the Options dialog (PrintWindow) to a PNG. Returns 0 on success.
int renderOptionsDialog(const std::wstring& output);

}  // namespace bt::app
