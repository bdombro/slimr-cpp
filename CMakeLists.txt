cmake_minimum_required(VERSION 3.30)
project(slimr_cpp VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(BUILD_TESTING)
    enable_testing()
endif()

set(SLIMR_PACKAGES
    json
    reflect
    yaml
    test
    sqlite
    curl
    bugsink
    ws
    mcp
    encoding
    proc
    time
    text
)

foreach(package IN LISTS SLIMR_PACKAGES)
    add_subdirectory("packages/${package}")
    set_target_properties("slimr_${package}" PROPERTIES EXPORT_NAME "${package}")
    target_compile_features("slimr_${package}" INTERFACE cxx_std_26)
endforeach()

add_library(slimr_all INTERFACE)
add_library(slimr::all ALIAS slimr_all)
set_target_properties(slimr_all PROPERTIES EXPORT_NAME all)
target_link_libraries(slimr_all INTERFACE
    slimr_json
    slimr_reflect
    slimr_yaml
    slimr_test
    slimr_sqlite
    slimr_curl
    slimr_bugsink
    slimr_ws
    slimr_mcp
    slimr_encoding
    slimr_proc
    slimr_time
    slimr_text
)

install(FILES include/slimr/pch.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/slimr")

foreach(package IN LISTS SLIMR_PACKAGES)
    install(DIRECTORY "packages/${package}/include/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(TARGETS "slimr_${package}" EXPORT slimr-cpp-targets)
endforeach()

install(DIRECTORY vendor/glaze/include/glaze DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES vendor/ryml/ryml_all.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES vendor/doctest/doctest.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/doctest")
install(TARGETS slimr_all EXPORT slimr-cpp-targets)
install(EXPORT slimr-cpp-targets
    FILE slimr-cpp-targets.cmake
    NAMESPACE slimr::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/slimr-cpp"
)

configure_package_config_file(
    cmake/slimr-cpp-config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/slimr-cpp-config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/slimr-cpp"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/slimr-cpp-config-version.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/slimr-cpp-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/slimr-cpp-config-version.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/slimr-cpp"
)
