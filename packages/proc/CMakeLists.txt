cmake_minimum_required(VERSION 3.25)
project(slimr_proc LANGUAGES CXX)

add_library(slimr_proc INTERFACE)
add_library(slimr::proc ALIAS slimr_proc)

target_include_directories(slimr_proc INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor>
    $<INSTALL_INTERFACE:include>
)

if(BUILD_TESTING OR CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR DEFINED SLIMR_BUILD_TESTS)
    add_executable(test_proc tests/test_proc.cpp)
    target_link_libraries(test_proc PRIVATE slimr::proc slimr::test)
    target_compile_features(test_proc PRIVATE cxx_std_26)
    add_test(NAME test_proc COMMAND test_proc)
endif()
