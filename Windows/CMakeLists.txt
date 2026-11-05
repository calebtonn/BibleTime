# BibleTime for Windows.
#
#   Windows (MSVC, Visual Studio 2022):
#     cmake -S Windows -B build/windows -A x64
#     cmake --build build/windows --config Release
#   Cross-compile from Linux/macOS (mingw-w64):
#     cmake -S Windows -B build/windows-mingw -DCMAKE_TOOLCHAIN_FILE="$PWD/Windows/cmake/mingw-w64.cmake" -DCMAKE_BUILD_TYPE=Release
#     cmake --build build/windows-mingw
#   Core tests only, on any OS with a C++17 compiler:
#     cmake -S Windows -B build/core && cmake --build build/core && build/core/bin/core_tests Resources
#
# Every executable lands in <build>/bin: BibleTime.scr (Windows only), core_tests, crosscheck.
cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" BT_VERSION)
string(STRIP "${BT_VERSION}" BT_VERSION)
project(BibleTime VERSION ${BT_VERSION} LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(BT_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(BT_RESOURCES "${BT_REPO_ROOT}/Resources")

# One output directory, also for multi-config generators (no Release/ subfolder).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} "${CMAKE_BINARY_DIR}/bin")
endforeach()

if(MSVC)
    # Static CRT: the .scr must run on a clean PC with no Visual C++ redistributable installed.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W4 /WX /permissive- /utf-8 /EHsc /Zc:__cplusplus)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Werror -Wno-unused-parameter)
endif()

# --- Core (portable) ---------------------------------------------------------
file(GLOB BT_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp")
add_library(bibletime_core STATIC ${BT_CORE_SOURCES})
target_include_directories(bibletime_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
if(WIN32)
    target_compile_definitions(bibletime_core PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
if(MINGW)
    target_link_options(bibletime_core PUBLIC -static -static-libgcc -static-libstdc++)
endif()

add_executable(core_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/CoreTests.cpp")
target_link_libraries(core_tests PRIVATE bibletime_core)

add_executable(crosscheck "${CMAKE_CURRENT_SOURCE_DIR}/tests/CrossCheck.cpp")
target_link_libraries(crosscheck PRIVATE bibletime_core)

# --- Screen saver (Windows only) ---------------------------------------------
if(WIN32)
    include("${CMAKE_CURRENT_SOURCE_DIR}/src/app/App.cmake")
endif()
