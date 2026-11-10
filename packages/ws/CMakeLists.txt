cmake_minimum_required(VERSION 3.25)
project(slimr_ws LANGUAGES CXX)

find_package(CURL REQUIRED)

add_library(slimr_ws INTERFACE)
add_library(slimr::ws ALIAS slimr_ws)

target_include_directories(slimr_ws INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/glaze/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(slimr_ws INTERFACE CURL::libcurl)

if(BUILD_TESTING OR CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR DEFINED SLIMR_BUILD_TESTS)
    add_executable(test_ws tests/test_ws.cpp)
    target_link_libraries(test_ws PRIVATE slimr::ws slimr::test)
    target_compile_features(test_ws PRIVATE cxx_std_26)
    add_test(NAME test_ws COMMAND test_ws)
endif()
