cmake_minimum_required(VERSION 3.25)
project(slimr_test LANGUAGES CXX)

add_library(slimr_test INTERFACE)
add_library(slimr::test ALIAS slimr_test)

target_include_directories(slimr_test INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor>
    $<INSTALL_INTERFACE:include>
)

if(BUILD_TESTING OR CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR DEFINED SLIMR_BUILD_TESTS)
    add_executable(test_test tests/test_test.cpp)
    target_link_libraries(test_test PRIVATE slimr::test)
    target_compile_features(test_test PRIVATE cxx_std_26)
    add_test(NAME test_test COMMAND test_test)
endif()
