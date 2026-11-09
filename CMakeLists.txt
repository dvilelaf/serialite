cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tools/cmake/strict_warnings.cmake)
project(serialite)

serialite_strict_warnings(main ${CMAKE_CURRENT_LIST_DIR}/components)
