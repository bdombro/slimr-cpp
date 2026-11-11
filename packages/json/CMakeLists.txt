add_library(slimr_json INTERFACE)
add_library(slimr::json ALIAS slimr_json)
target_compile_features(slimr_json INTERFACE cxx_std_26)
target_include_directories(slimr_json INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/vendor/glaze/include")
if(BUILD_TESTING)
    add_executable(json_test test.cpp)
    target_link_libraries(json_test PRIVATE slimr::json slimr::test slimr_options)
    add_test(NAME json COMMAND json_test)
    set_tests_properties(json PROPERTIES TIMEOUT 120
        ENVIRONMENT "TMPDIR=${CMAKE_BINARY_DIR}/test-tmp")
endif()
