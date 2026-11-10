cmake_minimum_required(VERSION 3.25)
project(slimr_yaml LANGUAGES CXX)

add_library(slimr_yaml INTERFACE)
add_library(slimr::yaml ALIAS slimr_yaml)

target_include_directories(slimr_yaml INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/glaze/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/ryml>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../vendor>
    $<INSTALL_INTERFACE:include>
)

if(BUILD_TESTING OR CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OR DEFINED SLIMR_BUILD_TESTS)
    add_executable(test_yaml tests/test_yaml.cpp)
    target_link_libraries(test_yaml PRIVATE slimr::yaml slimr::test)
    target_compile_features(test_yaml PRIVATE cxx_std_26)
    add_test(NAME test_yaml COMMAND test_yaml)
endif()
