cmake_minimum_required(VERSION 3.16)
project(esp32_kvm_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

enable_testing()

add_executable(test_terminal_bridge_ring
    test_terminal_bridge_ring.c
    ../../components/terminal_bridge/src/terminal_bridge_ring.c
)

target_include_directories(test_terminal_bridge_ring PRIVATE
    ../../components/terminal_bridge/include
)

add_test(NAME terminal_bridge_ring COMMAND test_terminal_bridge_ring)

add_executable(test_terminal_scrollback
    test_terminal_scrollback.c
    ../../components/terminal_bridge/src/terminal_scrollback.c
)

target_include_directories(test_terminal_scrollback PRIVATE
    ../../components/terminal_bridge/include
)

add_test(NAME terminal_scrollback COMMAND test_terminal_scrollback)

add_executable(test_storage_config
    test_storage_config.c
    ../../components/storage/src/storage_config.c
)

target_include_directories(test_storage_config PRIVATE
    ../../components/storage/include
)

add_test(NAME storage_config COMMAND test_storage_config)

add_executable(test_web_security
    test_web_security.c
    ../../components/web_server/src/web_password_hash.c
    ../../components/web_server/src/web_security.c
)

target_include_directories(test_web_security PRIVATE
    ../../components/web_server/include
)

target_link_libraries(test_web_security PRIVATE OpenSSL::Crypto)

add_test(NAME web_security COMMAND test_web_security)

add_executable(test_web_input_policy
    test_web_input_policy.c
    ../../components/web_server/src/web_input_policy.c
)

target_include_directories(test_web_input_policy PRIVATE
    ../../components/web_server/include
)

add_test(NAME web_input_policy COMMAND test_web_input_policy)

add_executable(test_web_terminal_contract
    test_web_terminal_contract.c
    ../../components/web_server/src/web_terminal_contract.c
)

target_include_directories(test_web_terminal_contract PRIVATE
    ../../components/web_server/include
)

add_test(NAME web_terminal_contract COMMAND test_web_terminal_contract)

add_executable(test_web_terminal_ansi
    test_web_terminal_ansi.c
    ../../components/web_server/src/web_terminal_ansi.c
)

target_include_directories(test_web_terminal_ansi PRIVATE
    ../../components/web_server/include
)

add_test(NAME web_terminal_ansi COMMAND test_web_terminal_ansi)

add_executable(test_network_identity_policy
    test_network_identity_policy.c
    ../../components/network_identity/src/network_identity_policy.c
)

target_include_directories(test_network_identity_policy PRIVATE
    ../../components/network_identity/include
)

add_test(NAME network_identity_policy COMMAND test_network_identity_policy)

add_executable(test_ap_exposure_policy
    test_ap_exposure_policy.c
    ../../components/web_server/src/ap_exposure_policy.c
)

target_include_directories(test_ap_exposure_policy PRIVATE
    ../../components/web_server/include
)

add_test(NAME ap_exposure_policy COMMAND test_ap_exposure_policy)

add_executable(test_http_route_policy
    test_http_route_policy.c
    ../../components/web_server/src/http_route_policy.c
)

target_include_directories(test_http_route_policy PRIVATE
    ../../components/web_server/include
    ../../components/ota_update/include
)

add_test(NAME http_route_policy COMMAND test_http_route_policy)

add_executable(test_ota_update_policy
    test_ota_update_policy.c
    ../../components/ota_update/src/ota_update_policy.c
)

target_include_directories(test_ota_update_policy PRIVATE
    ../../components/ota_update/include
)

add_test(NAME ota_update_policy COMMAND test_ota_update_policy)

add_executable(test_http_rate_limit
    test_http_rate_limit.c
    ../../components/web_server/src/http_rate_limit.c
)

target_include_directories(test_http_rate_limit PRIVATE
    ../../components/web_server/include
)

add_test(NAME http_rate_limit COMMAND test_http_rate_limit)

add_executable(test_event_log
    test_event_log.c
    ../../components/event_log/src/event_log.c
)

target_include_directories(test_event_log PRIVATE
    ../../components/event_log/include
)

target_link_libraries(test_event_log PRIVATE Threads::Threads)

add_test(NAME event_log COMMAND test_event_log)

add_executable(test_diagnostics_export
    test_diagnostics_export.c
    ../../components/diagnostics_export/src/diagnostics_export.c
)

target_include_directories(test_diagnostics_export PRIVATE
    ../../components/diagnostics_export/include
    ../../components/event_log/include
)

add_test(NAME diagnostics_export COMMAND test_diagnostics_export)

add_executable(test_secret_display
    test_secret_display.c
    ../../components/secret_display/src/secret_display.c
)

target_include_directories(test_secret_display PRIVATE
    ../../components/secret_display/include
)

add_test(NAME secret_display COMMAND test_secret_display)

add_executable(test_ui_status_format
    test_ui_status_format.c
    ../../components/lvgl_ui/src/ui_status_format.c
)

target_include_directories(test_ui_status_format PRIVATE
    ../../components/lvgl_ui/include
)

add_test(NAME ui_status_format COMMAND test_ui_status_format)

add_executable(test_reset_gesture
    test_reset_gesture.c
    ../../components/reset_control/src/reset_gesture.c
)

target_include_directories(test_reset_gesture PRIVATE
    ../../components/reset_control/include
)

add_test(NAME reset_gesture COMMAND test_reset_gesture)

add_executable(test_emergency_lock_gesture
    test_emergency_lock_gesture.c
    ../../components/reset_control/src/emergency_lock_gesture.c
)

target_include_directories(test_emergency_lock_gesture PRIVATE
    ../../components/reset_control/include
)

add_test(NAME emergency_lock_gesture COMMAND test_emergency_lock_gesture)

add_executable(test_startup_policy
    test_startup_policy.c
    ../../components/startup_policy/src/startup_policy.c
)

target_include_directories(test_startup_policy PRIVATE
    ../../components/startup_policy/include
)

add_test(NAME startup_policy COMMAND test_startup_policy)
