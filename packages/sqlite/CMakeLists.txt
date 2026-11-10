cmake_minimum_required(VERSION 3.25)
project(slimr_sqlite LANGUAGES CXX)

find_package(SQLite3 REQUIRED)

add_library(slimr_sqlite INTERFACE)
add_library(slimr::sqlite ALIAS slimr_sqlite)

target_include_directories(slimr_sqlite INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/glaze/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(slimr_sqlite INTERFACE SQLite3::SQLite3)

if(BUILD_TESTING OR CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR DEFINED SLIMR_BUILD_TESTS)
    add_executable(test_sqlite tests/test_sqlite.cpp)
    target_link_libraries(test_sqlite PRIVATE slimr::sqlite slimr::test)
    target_compile_features(test_sqlite PRIVATE cxx_std_26)
    add_test(NAME test_sqlite COMMAND test_sqlite)
endif()
