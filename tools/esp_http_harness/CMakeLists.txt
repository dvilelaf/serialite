cmake_minimum_required(VERSION 3.16)

# Builds the real web_server component for the ESP-IDF linux target. Hardware
# components come from ./components as stubs compiled against the real headers.
set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
add_compile_options(-I${CMAKE_CURRENT_LIST_DIR}/compat/include)
set(EXTRA_COMPONENT_DIRS
    "${REPO_ROOT}/components/diagnostics_export"
    "${REPO_ROOT}/components/event_log"
    "${REPO_ROOT}/components/https_fingerprint"
    "${REPO_ROOT}/components/json_writer"
    "${REPO_ROOT}/components/web_server"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include(${REPO_ROOT}/tools/cmake/strict_warnings.cmake)
project(serialite_http_harness)

serialite_strict_warnings(main ${EXTRA_COMPONENT_DIRS} ${CMAKE_CURRENT_LIST_DIR}/components)
