cmake_minimum_required(VERSION 3.25)
project(slimr_curl LANGUAGES CXX)

find_package(CURL REQUIRED)

add_library(slimr_curl INTERFACE)
add_library(slimr::curl ALIAS slimr_curl)

target_include_directories(slimr_curl INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(slimr_curl INTERFACE CURL::libcurl)

if(BUILD_TESTING OR CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR DEFINED SLIMR_BUILD_TESTS)
    add_executable(test_curl tests/test_curl.cpp)
    target_link_libraries(test_curl PRIVATE slimr::curl slimr::test)
    target_compile_features(test_curl PRIVATE cxx_std_26)
    add_test(NAME test_curl COMMAND test_curl)
endif()
