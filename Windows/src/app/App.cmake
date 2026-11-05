# BibleTime.scr: the Windows screen saver (included from Windows/CMakeLists.txt on WIN32).
#
# A single self-contained executable: the verse data (books, curated lists, KJV/Darby) is embedded as RCDATA,
# the CRT is linked statically, and only DLLs that ship with Windows 10/11 are used.

set(BT_APP_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(BT_RES_DIR "${CMAKE_CURRENT_LIST_DIR}/../../res")
get_filename_component(BT_RES_DIR "${BT_RES_DIR}" ABSOLUTE)
get_filename_component(BT_RESOURCES_ABS "${BT_RESOURCES}" ABSOLUTE)

string(REPLACE "." ";" _bt_version_parts "${BT_VERSION}")
list(GET _bt_version_parts 0 BT_VERSION_MAJOR)
list(GET _bt_version_parts 1 BT_VERSION_MINOR)
list(GET _bt_version_parts 2 BT_VERSION_PATCH)

# Absolute, forward-slash paths work for both rc.exe and windres.
configure_file("${BT_RES_DIR}/BibleTime.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/BibleTime.rc" @ONLY)

set(BT_EMBEDDED_FILES
    "${BT_RESOURCES_ABS}/books.json"
    "${BT_RESOURCES_ABS}/curated.json"
    "${BT_RESOURCES_ABS}/hour_verses.json"
    "${BT_RESOURCES_ABS}/Translations/kjv.json"
    "${BT_RESOURCES_ABS}/Translations/dby.json"
    "${BT_RES_DIR}/BibleTime.manifest"
    "${BT_RES_DIR}/BibleTime.ico"
    "${BT_RES_DIR}/resource.h")

add_executable(BibleTime WIN32
    "${BT_APP_DIR}/Main.cpp"
    "${BT_APP_DIR}/Install.cpp"
    "${BT_APP_DIR}/OptionsDialog.cpp"
    "${BT_APP_DIR}/PageRenderer.cpp"
    "${BT_APP_DIR}/Platform.cpp"
    "${BT_APP_DIR}/Render.cpp"
    "${BT_APP_DIR}/Resources.cpp"
    "${BT_APP_DIR}/Saver.cpp"
    "${BT_APP_DIR}/Typeface.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/BibleTime.rc")

# Rebuild the resources when embedded data changes (the .rc only names the files).
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/BibleTime.rc" PROPERTIES OBJECT_DEPENDS "${BT_EMBEDDED_FILES}")

set_target_properties(BibleTime PROPERTIES OUTPUT_NAME "BibleTime" SUFFIX ".scr")
target_include_directories(BibleTime PRIVATE "${BT_APP_DIR}" "${BT_RES_DIR}")
target_compile_definitions(BibleTime PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000006)
target_link_libraries(BibleTime PRIVATE bibletime_core
    d2d1 dwrite windowscodecs comctl32 shlwapi shell32 ole32 uuid dwmapi advapi32 user32 gdi32)

if(MSVC)
    # The manifest is embedded by the .rc (resource 1); stop the linker generating a second one.
    target_link_options(BibleTime PRIVATE /SUBSYSTEM:WINDOWS /MANIFEST:NO /ENTRY:wWinMainCRTStartup)
else()
    # MinGW: Unicode entry point (wWinMain) and a fully static image (no libstdc++/libgcc/winpthread DLLs).
    target_link_options(BibleTime PRIVATE -municode -mwindows -static -static-libgcc -static-libstdc++ $<$<CONFIG:Release,MinSizeRel>:-s>)
endif()
