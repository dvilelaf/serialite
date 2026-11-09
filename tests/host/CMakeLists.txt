cmake_minimum_required(VERSION 3.16)
project(serialite_host_tests C)

# Host tests for the hardware-independent modules. Each test links only the
# sources it exercises, straight from the components.

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
add_compile_options(-Wall -Wextra -Wshadow -Werror)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(C ${REPO_ROOT}/components)

enable_testing()

# serialite_test(<name> SOURCES <files> INCLUDES <dirs> [LIBS <libs>]) builds test_<name>.c.
function(serialite_test name)
    cmake_parse_arguments(T "" "" "SOURCES;INCLUDES;LIBS" ${ARGN})
    add_executable(test_${name} test_${name}.c ${T_SOURCES})
    target_include_directories(test_${name} PRIVATE ${T_INCLUDES})
    if(T_LIBS)
        target_link_libraries(test_${name} PRIVATE ${T_LIBS})
    endif()
    add_test(NAME ${name} COMMAND test_${name})
endfunction()

serialite_test(button_gesture
    SOURCES ${C}/buttons/src/button_gesture.c
    INCLUDES ${C}/buttons/include)

serialite_test(credential_boot_policy
    SOURCES ${REPO_ROOT}/main/credential_boot_policy.c
    INCLUDES ${REPO_ROOT}/main)

serialite_test(ap_exposure_policy
    SOURCES ${REPO_ROOT}/main/ap_exposure_policy.c
    INCLUDES ${REPO_ROOT}/main)

serialite_test(credentials
    SOURCES ${C}/credentials/src/credentials.c ${C}/credentials/src/credentials_wordlist.c
    INCLUDES ${C}/credentials/include)

serialite_test(storage_config
    SOURCES ${C}/storage/src/storage_config.c
    INCLUDES ${C}/storage/include)

serialite_test(config_transfer
    SOURCES ${C}/config_transfer/src/config_transfer.c ${C}/storage/src/storage_config.c
    INCLUDES ${C}/config_transfer/include ${C}/storage/include)

serialite_test(https_fingerprint
    SOURCES ${C}/https_fingerprint/src/https_fingerprint.c
    INCLUDES ${C}/https_fingerprint/include)

serialite_test(json_writer
    SOURCES ${C}/json_writer/src/json_writer.c
    INCLUDES ${C}/json_writer/include)

serialite_test(event_log
    SOURCES ${C}/event_log/src/event_log.c
    INCLUDES ${C}/event_log/include
    LIBS Threads::Threads)

serialite_test(diagnostics_export
    SOURCES ${C}/diagnostics_export/src/diagnostics_export.c ${C}/json_writer/src/json_writer.c
    INCLUDES ${C}/diagnostics_export/include ${C}/event_log/include ${C}/json_writer/include)

serialite_test(terminal_scrollback
    SOURCES ${C}/terminal_bridge/src/terminal_scrollback.c
    INCLUDES ${C}/terminal_bridge/include)

serialite_test(ota_update_policy
    SOURCES ${C}/ota_update/src/ota_update_policy.c
    INCLUDES ${C}/ota_update/include)

serialite_test(network_identity_policy
    SOURCES ${C}/network_identity/src/network_identity_policy.c
    INCLUDES ${C}/network_identity/include)

serialite_test(wifi_ap_status_policy
    SOURCES ${C}/wifi_ap/src/wifi_ap_status_policy.c
    INCLUDES ${C}/wifi_ap/include)

serialite_test(secret_display
    SOURCES ${C}/secret_display/src/secret_display.c
    INCLUDES ${C}/secret_display/include)

serialite_test(ui_status_format
    SOURCES ${C}/lvgl_ui/src/ui_status_format.c
    INCLUDES ${C}/lvgl_ui/include)

serialite_test(web_request
    SOURCES ${C}/web_server/src/web_request.c
    INCLUDES ${C}/web_server/include)

serialite_test(web_guard
    SOURCES ${C}/web_server/src/web_guard.c
    INCLUDES ${C}/web_server/include)

serialite_test(web_security
    SOURCES ${C}/web_server/src/web_security.c ${C}/web_server/src/web_request.c
    INCLUDES ${C}/web_server/include)

serialite_test(web_input_policy
    SOURCES ${C}/web_server/src/web_input_policy.c
    INCLUDES ${C}/web_server/include)

serialite_test(web_terminal_ansi
    SOURCES ${C}/web_server/src/web_terminal_ansi.c
    INCLUDES ${C}/web_server/include)

serialite_test(http_rate_limit
    SOURCES ${C}/web_server/src/http_rate_limit.c
    INCLUDES ${C}/web_server/include)

# Needs the host mbedTLS 3.x development package (libmbedtls-dev); CI installs it.
find_path(HOST_MBEDTLS_INCLUDE_DIR mbedtls/pk.h)
find_library(HOST_MBEDTLS_LIB mbedtls)
find_library(HOST_MBEDX509_LIB mbedx509)
find_library(HOST_MBEDCRYPTO_LIB mbedcrypto)
if(HOST_MBEDTLS_INCLUDE_DIR AND HOST_MBEDTLS_LIB AND HOST_MBEDX509_LIB AND HOST_MBEDCRYPTO_LIB)
    serialite_test(local_tls_identity
        SOURCES ${C}/local_tls_identity/src/local_tls_identity.c ${C}/https_fingerprint/src/https_fingerprint.c
        INCLUDES ${C}/local_tls_identity/include ${C}/https_fingerprint/include ${HOST_MBEDTLS_INCLUDE_DIR}
        LIBS ${HOST_MBEDTLS_LIB} ${HOST_MBEDX509_LIB} ${HOST_MBEDCRYPTO_LIB})
elseif(DEFINED ENV{CI})
    message(FATAL_ERROR "CI must install mbedTLS development files to run the local_tls_identity test")
else()
    message(WARNING "Skipping local_tls_identity: mbedTLS development files not found")
endif()
