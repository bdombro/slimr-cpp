cmake_minimum_required(VERSION 3.25)
project(slimr_time LANGUAGES CXX)

add_library(slimr_time INTERFACE)
add_library(slimr::time ALIAS slimr_time)

target_include_directories(slimr_time INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor>
    $<INSTALL_INTERFACE:include>
)

if(BUILD_TESTING OR CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR DEFINED SLIMR_BUILD_TESTS)
    add_executable(test_time tests/test_time.cpp)
    target_link_libraries(test_time PRIVATE slimr::time slimr::test)
    target_compile_features(test_time PRIVATE cxx_std_26)
    add_test(NAME test_time COMMAND test_time)
endif()
