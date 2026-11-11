if(NOT TARGET slimr::time)
    add_library(slimr::time INTERFACE IMPORTED)
    set_target_properties(slimr::time PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../include"
        INTERFACE_COMPILE_FEATURES cxx_std_26)
endif()
