cmake_minimum_required(VERSION 3.25)
project(slimr_text LANGUAGES CXX)

add_library(slimr_text INTERFACE)
add_library(slimr::text ALIAS slimr_text)

target_include_directories(slimr_text INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor>
    $<INSTALL_INTERFACE:include>
)

if(BUILD_TESTING OR CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR DEFINED SLIMR_BUILD_TESTS)
    add_executable(test_text tests/test_text.cpp)
    target_link_libraries(test_text PRIVATE slimr::text slimr::test)
    target_compile_features(test_text PRIVATE cxx_std_26)
    add_test(NAME test_text COMMAND test_text)
endif()
