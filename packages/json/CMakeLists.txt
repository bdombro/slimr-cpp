cmake_minimum_required(VERSION 3.25)
project(slimr_json LANGUAGES CXX)

add_library(slimr_json INTERFACE)
add_library(slimr::json ALIAS slimr_json)

target_include_directories(slimr_json INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/glaze/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor>
    $<INSTALL_INTERFACE:include>
)

if(BUILD_TESTING OR CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR DEFINED SLIMR_BUILD_TESTS)
    add_executable(test_json tests/test_json.cpp)
    target_link_libraries(test_json PRIVATE slimr::json slimr::test)
    target_compile_features(test_json PRIVATE cxx_std_26)
    add_test(NAME test_json COMMAND test_json)
endif()
