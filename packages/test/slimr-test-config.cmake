if(NOT TARGET slimr::test)
    add_library(slimr::test INTERFACE IMPORTED)
    set_target_properties(slimr::test PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../include"
        INTERFACE_COMPILE_FEATURES cxx_std_26)
endif()
