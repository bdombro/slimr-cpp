cmake_minimum_required(VERSION 3.25)
project(slimr_bugsink LANGUAGES CXX)

find_package(CURL REQUIRED)

add_library(slimr_bugsink INTERFACE)
add_library(slimr::bugsink ALIAS slimr_bugsink)

target_include_directories(slimr_bugsink INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/glaze/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(slimr_bugsink INTERFACE CURL::libcurl)

if(BUILD_TESTING OR CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR DEFINED SLIMR_BUILD_TESTS)
    add_executable(test_bugsink tests/test_bugsink.cpp)
    target_link_libraries(test_bugsink PRIVATE slimr::bugsink slimr::test)
    target_compile_features(test_bugsink PRIVATE cxx_std_26)
    add_test(NAME test_bugsink COMMAND test_bugsink)
endif()
