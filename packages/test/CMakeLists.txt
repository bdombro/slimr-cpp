add_library(slimr_test_headers INTERFACE)
add_library(slimr::test ALIAS slimr_test_headers)
target_compile_features(slimr_test_headers INTERFACE cxx_std_26)
target_include_directories(slimr_test_headers INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
if(BUILD_TESTING)
    add_executable(test_test test.cpp)
    target_link_libraries(test_test PRIVATE slimr::test slimr_options)
    add_test(NAME test COMMAND test_test)
    set_tests_properties(test PROPERTIES TIMEOUT 120
        ENVIRONMENT "TMPDIR=${CMAKE_BINARY_DIR}/test-tmp")
endif()
