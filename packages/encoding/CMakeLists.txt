cmake_minimum_required(VERSION 3.25)
project(slimr_encoding LANGUAGES CXX)

add_library(slimr_encoding INTERFACE)
add_library(slimr::encoding ALIAS slimr_encoding)

target_include_directories(slimr_encoding INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor>
    $<INSTALL_INTERFACE:include>
)

if(BUILD_TESTING OR CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR DEFINED SLIMR_BUILD_TESTS)
    add_executable(test_encoding tests/test_encoding.cpp)
    target_link_libraries(test_encoding PRIVATE slimr::encoding slimr::test)
    target_compile_features(test_encoding PRIVATE cxx_std_26)
    add_test(NAME test_encoding COMMAND test_encoding)
endif()
