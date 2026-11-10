cmake_minimum_required(VERSION 3.25)
project(slimr_reflect LANGUAGES CXX)

add_library(slimr_reflect INTERFACE)
add_library(slimr::reflect ALIAS slimr_reflect)

target_include_directories(slimr_reflect INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/glaze/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor>
    $<INSTALL_INTERFACE:include>
)

if(BUILD_TESTING OR CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR DEFINED SLIMR_BUILD_TESTS)
    add_executable(test_reflect tests/test_reflect.cpp)
    target_link_libraries(test_reflect PRIVATE slimr::reflect slimr::test)
    target_compile_features(test_reflect PRIVATE cxx_std_26)
    add_test(NAME test_reflect COMMAND test_reflect)
endif()
