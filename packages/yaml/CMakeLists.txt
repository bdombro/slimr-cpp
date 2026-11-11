add_library(slimr_yaml INTERFACE)
add_library(slimr::yaml ALIAS slimr_yaml)
target_compile_features(slimr_yaml INTERFACE cxx_std_26)
target_include_directories(slimr_yaml INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
target_link_libraries(slimr_yaml INTERFACE slimr::json)
if(BUILD_TESTING)
    add_executable(yaml_test test.cpp)
    target_link_libraries(yaml_test PRIVATE slimr::yaml slimr::test slimr_options)
    add_test(NAME yaml COMMAND yaml_test)
    set_tests_properties(yaml PROPERTIES TIMEOUT 120
        ENVIRONMENT "TMPDIR=${CMAKE_BINARY_DIR}/test-tmp")
endif()
