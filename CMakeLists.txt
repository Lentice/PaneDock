cmake_minimum_required(VERSION 3.25)

project(PaneDock LANGUAGES CXX RC)

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
    src/core/group_transition.cpp
    src/core/key_routing.cpp
    src/core/layout.cpp
    src/core/model.cpp
    src/core/session.cpp
    src/core/shutdown.cpp
    src/core/tab_drag.cpp
)
target_include_directories(panedock_core PUBLIC src)
panedock_set_warnings(panedock_core)

# ---------------------------------------------------------------------------
# Shell-facing modules. These do link Windows libraries; core does not.
# PD-007 creates the first single ExplorerBrowser host and app shell.
# ---------------------------------------------------------------------------
add_library(panedock_shell_core STATIC
    src/shell_core/shell_core.cpp
)
target_include_directories(panedock_shell_core PUBLIC src)
target_compile_definitions(panedock_shell_core PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
)
target_link_libraries(panedock_shell_core PRIVATE shell32 ole32 uuid)
panedock_set_warnings(panedock_shell_core)
# Generic Win32 window helpers. Its own library because both app_shell and
# explorer_host use it: it used to be compiled into panedock_explorer_host,
# which made an app_shell file part of a lower layer's target and left the
# library boundary saying something the build did not do.
add_library(panedock_window_helpers STATIC
    src/app_shell/window_helpers.h
    src/app_shell/window_helpers.cpp
)
target_include_directories(panedock_window_helpers PUBLIC src)
target_compile_definitions(panedock_window_helpers PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
)
target_link_libraries(panedock_window_helpers PUBLIC user32)
panedock_set_warnings(panedock_window_helpers)

add_library(panedock_explorer_host STATIC
    src/explorer_host/explorer_host.cpp
    src/explorer_host/pane_error_overlay.cpp
)
target_include_directories(panedock_explorer_host PUBLIC src)
target_compile_definitions(panedock_explorer_host PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
)
target_link_libraries(panedock_explorer_host PUBLIC panedock_window_helpers)
target_link_libraries(panedock_explorer_host PRIVATE
    panedock_shell_core comctl32 ole32 propsys shell32 shlwapi user32 uuid
)
panedock_set_warnings(panedock_explorer_host)
add_library(panedock_file_operations STATIC
    src/file_operations/file_operations.cpp
)
target_include_directories(panedock_file_operations PUBLIC src)
target_compile_definitions(panedock_file_operations PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
)
target_link_libraries(panedock_file_operations PRIVATE shell32 ole32 uuid)
panedock_set_warnings(panedock_file_operations)
add_library(panedock_sidebar STATIC
    src/sidebar/sidebar.cpp
)
target_include_directories(panedock_sidebar PUBLIC src)
target_compile_definitions(panedock_sidebar PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
)
target_link_libraries(panedock_sidebar PRIVATE user32 gdi32)
panedock_set_warnings(panedock_sidebar)

add_library(panedock_pane STATIC
    src/app_shell/pane_host.h
    src/app_shell/pane.cpp
    src/app_shell/pane_tab_strip.h
    src/app_shell/pane_tab_strip.cpp
)
target_include_directories(panedock_pane PUBLIC src)
target_compile_definitions(panedock_pane PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
)
target_link_libraries(panedock_pane PUBLIC user32 ole32)
panedock_set_warnings(panedock_pane)

# Its own library so the unit test can link it. Nothing else moved.
add_library(panedock_session_writer STATIC
    src/app_shell/session_writer.h
    src/app_shell/session_writer.cpp
)
target_include_directories(panedock_session_writer PUBLIC src)
target_compile_definitions(panedock_session_writer PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
)
target_link_libraries(panedock_session_writer PUBLIC panedock_core kernel32)
panedock_set_warnings(panedock_session_writer)

add_executable(PaneDock WIN32
    src/app_shell/main.cpp
    src/app_shell/pinned_locations_dialog.cpp
    src/app_shell/startup_notification.cpp
    src/app_shell/transfer_close_dialog.cpp
    resources/panedock.rc
)
target_include_directories(PaneDock PRIVATE resources)
target_compile_definitions(PaneDock PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
)
target_link_options(PaneDock PRIVATE -municode -static)
target_link_libraries(PaneDock PRIVATE
    panedock_core panedock_shell_core panedock_explorer_host panedock_pane
    panedock_file_operations panedock_sidebar panedock_session_writer
    shell32 ole32 shlwapi user32 gdi32 comctl32 uuid
)
panedock_set_warnings(PaneDock)

add_executable(panedock_explorer_host_lifetime_check
    tests/unit/explorer_host_lifetime_check.cpp
)
target_include_directories(panedock_explorer_host_lifetime_check PRIVATE
    src tests
)
target_link_libraries(panedock_explorer_host_lifetime_check PRIVATE
    panedock_explorer_host ole32 user32
)
panedock_set_warnings(panedock_explorer_host_lifetime_check)

add_subdirectory(tests)
