if(NOT TARGET slimr::text)
    add_library(slimr::text INTERFACE IMPORTED)
    set_target_properties(slimr::text PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../include"
        INTERFACE_COMPILE_FEATURES cxx_std_26)
endif()
