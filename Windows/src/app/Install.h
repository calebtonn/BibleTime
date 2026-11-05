// /setactive and /unsetactive: choose or clear BibleTime as the current user's screen saver (used by the installer).
#pragma once

namespace bt::app {

/// HKCU\Control Panel\Desktop: SCRNSAVE.EXE = this file, ScreenSaveActive = 1, ScreenSaveTimeOut = 600 if unset;
/// then tells the system. Returns 0 on success.
int setActive();

/// Removes SCRNSAVE.EXE when it points at this file (any spelling of the path). Returns 0 (also when nothing to do).
int unsetActive();

}  // namespace bt::app
