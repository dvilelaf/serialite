cmake_minimum_required(VERSION 3.16)
project(esp32_kvm_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()

add_executable(test_terminal_bridge_ring
    test_terminal_bridge_ring.c
    ../../components/terminal_bridge/src/terminal_bridge_ring.c
)

target_include_directories(test_terminal_bridge_ring PRIVATE
    ../../components/terminal_bridge/include
)

add_test(NAME terminal_bridge_ring COMMAND test_terminal_bridge_ring)

add_executable(test_storage_config
    test_storage_config.c
    ../../components/storage/src/storage_config.c
)

target_include_directories(test_storage_config PRIVATE
    ../../components/storage/include
)

add_test(NAME storage_config COMMAND test_storage_config)
