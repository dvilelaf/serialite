cmake_minimum_required(VERSION 3.16)

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
add_compile_options(-I${CMAKE_CURRENT_LIST_DIR}/compat/include)
set(EXTRA_COMPONENT_DIRS
    "${REPO_ROOT}/components/web_server"
    "${REPO_ROOT}/components/credentials"
    "${REPO_ROOT}/components/diagnostics_export"
    "${REPO_ROOT}/components/event_log"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_kvm_http_harness)
