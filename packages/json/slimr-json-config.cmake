if(NOT TARGET slimr::json)
    add_library(slimr::json INTERFACE IMPORTED)
    set_target_properties(slimr::json PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../include"
        INTERFACE_COMPILE_FEATURES cxx_std_26)
endif()
