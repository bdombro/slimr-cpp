cmake_minimum_required(VERSION 3.31)
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_SYSROOT)
    execute_process(COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE CMAKE_OSX_SYSROOT OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    set(CMAKE_OSX_SYSROOT "${CMAKE_OSX_SYSROOT}" CACHE PATH "macOS SDK")
endif()
project(slimr_cpp VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test-tmp")
option(ENABLE_SANITIZERS "Instrument first-party tests with ASan and fatal UBSan" ON)
option(BUILD_FUZZING "Build Clang libFuzzer parser/encoding targets" OFF)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-ftrivial-auto-var-init=zero" SLIMR_HAS_AUTO_INIT)
if(NOT SLIMR_HAS_AUTO_INIT)
    message(FATAL_ERROR "The compiler must support -ftrivial-auto-var-init=zero")
endif()
add_library(slimr_options INTERFACE)
target_compile_options(slimr_options INTERFACE -ftrivial-auto-var-init=zero)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#include <version>
#ifndef _LIBCPP_VERSION
#error not libc++
#endif
int main() {}" SLIMR_USES_LIBCXX)
if(SLIMR_USES_LIBCXX)
    set(CMAKE_REQUIRED_DEFINITIONS -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST)
    check_cxx_source_compiles("
#include <vector>
#if !defined(_LIBCPP_HARDENING_MODE_FAST) || _LIBCPP_HARDENING_MODE != _LIBCPP_HARDENING_MODE_FAST
#error FAST hardening unavailable
#endif
int main() { std::vector<int> v{1}; return v[0] - 1; }" SLIMR_HAS_FAST_HARDENING)
    unset(CMAKE_REQUIRED_DEFINITIONS)
    if(NOT SLIMR_HAS_FAST_HARDENING)
        message(FATAL_ERROR "libc++ FAST hardening is required")
    endif()
    target_compile_definitions(slimr_options INTERFACE
        _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST)
else()
    message(STATUS "Not using libc++; libc++-specific hardening is not applied")
endif()
if(ENABLE_SANITIZERS)
    target_compile_options(slimr_options INTERFACE
        -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(slimr_options INTERFACE
        -fsanitize=address,undefined -fno-sanitize-recover=all)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT SLIMR_HAS_IPO OUTPUT SLIMR_IPO_ERROR LANGUAGES CXX)
    if(NOT SLIMR_HAS_IPO)
        message(FATAL_ERROR "Release IPO unavailable: ${SLIMR_IPO_ERROR}")
    endif()
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    target_compile_options(slimr_options INTERFACE -O3)
endif()

add_subdirectory(packages/test)
add_subdirectory(packages/json)
add_subdirectory(packages/reflect)
add_subdirectory(packages/yaml)
add_subdirectory(packages/sqlite)
add_subdirectory(packages/curl)
add_subdirectory(packages/bugsink)
add_subdirectory(packages/ws)
add_subdirectory(packages/mcp)
add_subdirectory(packages/encoding)
add_subdirectory(packages/proc)
add_subdirectory(packages/time)
add_subdirectory(packages/text)
if(BUILD_FUZZING)
    add_subdirectory(fuzz)
endif()

if(BUILD_TESTING)
    add_executable(slimr_test tests/test_main.cpp)
    target_link_libraries(slimr_test PRIVATE slimr_options
        slimr::test slimr::json slimr::reflect slimr::yaml slimr::sqlite
        slimr::curl slimr::bugsink slimr::ws slimr::mcp slimr::encoding
        slimr::proc slimr::time slimr::text)
    if(TARGET slimr::mcp_http)
        target_link_libraries(slimr_test PRIVATE slimr::mcp_http)
    endif()
    add_test(NAME aggregate COMMAND slimr_test)
    set_tests_properties(aggregate PROPERTIES TIMEOUT 120
        ENVIRONMENT "TMPDIR=${CMAKE_BINARY_DIR}/test-tmp")
endif()
