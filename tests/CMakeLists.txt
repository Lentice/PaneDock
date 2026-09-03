# Table-driven test registration.
#
# Each entry is "<test name>|<source file>|<libraries, space separated>".
# One list plus one loop keeps the per-target facts visible in one place. The
# drift this prevents is silent: a target defined by hand can end up compiled
# under different flags with no error, and nothing reports it.
#
# Add a row here rather than writing another add_executable block.

set(PANEDOCK_TESTS
    "diagnostic_flag|unit/diagnostic_flag_check.cpp|"
    "tab_overflow|unit/tab_overflow_test.cpp|"
    "window_placement|unit/window_placement_test.cpp|"
    "core_model|unit/core_model_test.cpp|panedock_core"
    "core_layout|unit/core_layout_test.cpp|panedock_core"
    "core_realization_plan|unit/core_realization_plan_test.cpp|panedock_core"
    "core_session|unit/core_session_test.cpp|panedock_core"
    "core_navigation|unit/core_navigation_test.cpp|panedock_core"
    "core_shutdown|unit/core_shutdown_test.cpp|panedock_core"
    "pane_chrome|unit/pane_chrome_test.cpp|panedock_pane_chrome"
    "pane_control_id|unit/pane_control_id_test.cpp|"
    "shell_core_view_mode|unit/shell_core_view_mode_test.cpp|panedock_shell_core"
    "file_operation_effect|unit/file_operation_effect_test.cpp|panedock_file_operations"
)

foreach(entry IN LISTS PANEDOCK_TESTS)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 test_name)
    list(GET parts 1 test_source)
    list(GET parts 2 test_libs)
    string(REPLACE " " ";" test_libs "${test_libs}")

    add_executable(panedock_${test_name}_test ${test_source})
    target_include_directories(panedock_${test_name}_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(panedock_${test_name}_test PRIVATE ${test_libs})
    panedock_set_warnings(panedock_${test_name}_test)

    add_test(NAME panedock_${test_name}
             COMMAND panedock_${test_name}_test)
endforeach()

if(WIN32)
    add_test(
        NAME panedock_shutdown_state
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/release/shutdown_state_check.ps1"
                -SourcePath "${PROJECT_SOURCE_DIR}/src/app_shell/main.cpp,${PROJECT_SOURCE_DIR}/src/app_shell/transfer_close_dialog.cpp")
    add_test(
        NAME panedock_single_instance_relay
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/release/single_instance_relay_check.ps1"
                -SourcePath "${PROJECT_SOURCE_DIR}/src/app_shell/main.cpp")
    add_test(
        NAME panedock_shell_reentry_gate
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/release/shell_reentry_gate_check.ps1"
                -SourcePath "${PROJECT_SOURCE_DIR}/src/app_shell/main.cpp")
    add_test(
        NAME panedock_address_bar_failure
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/release/address_bar_failure_check.ps1"
                -SourcePath "${PROJECT_SOURCE_DIR}/src/app_shell/main.cpp")
    add_test(
        NAME panedock_shell_core_boundary
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/release/shell_core_boundary_check.ps1")
    add_test(
        NAME panedock_startup_frame_order
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/release/startup_frame_order_check.ps1"
                -SourcePath "${PROJECT_SOURCE_DIR}/src/app_shell/main.cpp,${PROJECT_SOURCE_DIR}/src/app_shell/startup_notification.cpp")
    add_test(
        NAME panedock_launch_smoke
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/release/launch_smoke.ps1"
                -AppPath "$<TARGET_FILE:PaneDock>")
    set_tests_properties(panedock_launch_smoke PROPERTIES TIMEOUT 70)
endif()
