cmake_minimum_required(VERSION 3.25)
project(slimr_mcp LANGUAGES CXX)

find_package(CURL REQUIRED)

add_library(slimr_mcp INTERFACE)
add_library(slimr::mcp ALIAS slimr_mcp)

target_include_directories(slimr_mcp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/glaze/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(slimr_mcp INTERFACE
    slimr::json
    slimr::reflect
    slimr::bugsink
    slimr::curl
    CURL::libcurl
)

if(BUILD_TESTING OR CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR DEFINED SLIMR_BUILD_TESTS)
    add_executable(test_mcp tests/test_mcp.cpp)
    target_link_libraries(test_mcp PRIVATE slimr::mcp slimr::test)
    target_compile_features(test_mcp PRIVATE cxx_std_26)
    add_test(NAME test_mcp COMMAND test_mcp)
endif()
