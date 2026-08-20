cmake_minimum_required(VERSION 3.25)

project(PaneDock LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# LLVM-MinGW Clang is the validated Windows toolchain; see PD-012 and
# docs/development.md for the compatibility evidence.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(WARNING
        "PaneDock is validated with LLVM-MinGW Clang; configure with "
        "cmake/llvm-mingw.cmake for the supported Windows toolchain. See PD-012.")
endif()

# Warning flags are applied per target rather than globally, so that a target
# added later cannot silently inherit a weaker set.
function(panedock_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# core — the only automated test seam in this project (docs/testing.md).
#
# This target must never link a Windows library and its sources must never
# include windows.h. AGENTS.md states the rule; the absence of any Windows
# dependency here is what enforces it at link time.
# ---------------------------------------------------------------------------
add_library(panedock_core STATIC
    # PD-004 adds the model, PD-005 the layout maths, PD-006 the session document.
    src/core/placeholder.cpp
)
target_include_directories(panedock_core PUBLIC src)
panedock_set_warnings(panedock_core)

# ---------------------------------------------------------------------------
# Shell-facing modules. These do link Windows libraries; core does not.
# PD-007 creates the first single ExplorerBrowser host and app shell.
# ---------------------------------------------------------------------------
# add_library(panedock_shell_core STATIC src/shell_core/...)
add_library(panedock_explorer_host STATIC
    src/explorer_host/explorer_host.cpp
)
target_include_directories(panedock_explorer_host PUBLIC src)
target_compile_definitions(panedock_explorer_host PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
)
target_link_libraries(panedock_explorer_host PRIVATE
    ole32 shell32 shlwapi user32 uuid
)
panedock_set_warnings(panedock_explorer_host)
# add_library(panedock_file_operations STATIC src/file_operations/...)
# add_library(panedock_sidebar STATIC src/sidebar/...)

add_executable(PaneDock WIN32 src/app_shell/main.cpp)
target_compile_definitions(PaneDock PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
)
target_link_options(PaneDock PRIVATE -municode)
target_link_libraries(PaneDock PRIVATE
    panedock_core panedock_explorer_host
    shell32 ole32 shlwapi user32 gdi32 uuid
)
panedock_set_warnings(PaneDock)

add_executable(panedock_explorer_host_lifetime_check
    tests/unit/explorer_host_lifetime_check.cpp
)
target_include_directories(panedock_explorer_host_lifetime_check PRIVATE
    src tests
)
panedock_set_warnings(panedock_explorer_host_lifetime_check)

add_executable(panedock_quadrant_layout_check
    tests/unit/quadrant_layout_check.cpp
)
target_include_directories(panedock_quadrant_layout_check PRIVATE
    src tests
)
panedock_set_warnings(panedock_quadrant_layout_check)

add_subdirectory(tests)
